package server

import (
	"context"
	"errors"
	"net/http"
	"reflect"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

type telemetryRecord struct {
	Mode    string           `json:"mode"`
	Manual  telemetryTarget  `json:"manual"`
	Owned   telemetryTarget  `json:"owned"`
	Origin  string           `json:"origin,omitempty"`
	Claim   bool             `json:"claim"`
	Pending *telemetryTarget `json:"pending,omitempty"`
}

func (s *Server) readTelemetryRecord(id string) (telemetryRecord, error) {
	var record telemetryRecord
	_, err := s.db.Get("device-telemetry", id, &record)
	if errors.Is(err, store.ErrNotFound) {
		err = nil
	}
	if record.Mode == "" {
		record.Mode = "automatic"
	}
	return record, err
}

func (s *Server) writeTelemetryRecord(id string, before, after telemetryRecord) error {
	if reflect.DeepEqual(before, after) {
		return nil
	}
	_, err := s.db.Put("device-telemetry", id, after, -1)
	return err
}

func (s *Server) initTelemetry(d *runtimeDevice) {
	v := domain.TelemetryStatus{Mode: "automatic", State: "pending", Message: "The Hub will set up its receiver after identifying the machine."}
	if d.view.Adapter == "simulator" {
		v.State, v.Message = "simulator", "Simulation stays local and does not send network telemetry."
		d.view.Telemetry = v
		return
	}
	record, err := s.readTelemetryRecord(d.view.ID)
	v.Mode = record.Mode
	d.telemetryClaim, d.telemetryOrigin = record.Claim, record.Origin
	target := record.Owned
	if record.Mode == "manual" {
		target = record.Manual
	}
	v.Enabled, v.Host, v.Port = target.Enabled, target.Host, target.Port
	if err != nil {
		v.State, v.Message = "error", "The saved receiver preference could not be read."
	}
	if !d.view.Enabled {
		v.State, v.Message = "disabled", "Monitoring is disabled; the machine receiver has not been checked."
	}
	d.view.Telemetry = v
}

// Explicit Add/Save/Test authorizes automatic pairing. Manual overrides are
// durable owner preferences, so ordinary connection work never replaces them.
func (s *Server) requestTelemetryPairing(d *runtimeDevice, r *http.Request) {
	d.commandMu.Lock()
	defer d.commandMu.Unlock()
	if !s.registeredDevice(d) {
		return
	}
	d.mu.RLock()
	id, physical := d.view.ID, d.view.Adapter == "pico-standalone"
	d.mu.RUnlock()
	if !physical {
		return
	}
	record, err := s.readTelemetryRecord(id)
	if err != nil {
		s.telemetryState(d, record.Mode, "error", "The saved receiver preference could not be read.", telemetryTarget{})
		return
	}
	if record.Mode == "manual" {
		return
	}
	before := record
	record.Claim, record.Pending, record.Origin = true, nil, requestReceiverOrigin(r)
	if err := s.writeTelemetryRecord(id, before, record); err != nil {
		s.telemetryState(d, record.Mode, "error", "The pairing request could not be saved.", telemetryTarget{})
		return
	}
	d.telemetryClaim, d.telemetryOrigin, d.telemetryAttempts, d.telemetryNext = true, record.Origin, 0, time.Time{}
	s.telemetryState(d, "automatic", "pending", "The Hub will set up this connection when the machine is idle.", telemetryTarget{})
}

func (s *Server) telemetryState(d *runtimeDevice, mode, state, message string, target telemetryTarget) {
	now := time.Now().UTC()
	d.mu.Lock()
	d.view.Telemetry = domain.TelemetryStatus{Mode: mode, Enabled: target.Enabled, State: state, Message: message, Host: target.Host, Port: target.Port, LastChecked: &now}
	d.mu.Unlock()
}

func readTelemetryTarget(value map[string]any) (telemetryTarget, error) {
	enabled, a := value["enabled"].(bool)
	host, b := value["host"].(string)
	var port int
	switch n := value["port"].(type) {
	case int:
		port = n
	case float64:
		if n != float64(int(n)) {
			return telemetryTarget{}, errors.New("invalid telemetry port")
		}
		port = int(n)
	}
	target := telemetryTarget{Enabled: enabled, Host: host, Port: port}
	if !a || !b || device.ValidateTelemetry(target.settings()) != nil {
		return telemetryTarget{}, errors.New("invalid telemetry configuration")
	}
	return target, nil
}

func (s *Server) observedTelemetry(ctx context.Context, d *runtimeDevice) (telemetryTarget, error) {
	value, err := d.adapter.Telemetry(ctx)
	if err != nil {
		return telemetryTarget{}, err
	}
	return readTelemetryTarget(value)
}

// syncTelemetry runs under commandMu. It only changes forwarding preferences;
// it never calls an actuator or promotes unsigned observations into live state.
func (s *Server) syncTelemetry(ctx context.Context, d *runtimeDevice, force bool) {
	if !s.currentDevice(d) {
		return
	}
	d.mu.RLock()
	view := d.view
	d.mu.RUnlock()
	if view.Adapter != "pico-standalone" {
		return
	}
	if !force && time.Now().Before(d.telemetryNext) {
		return
	}
	d.telemetryNext = time.Now().Add(time.Minute)
	record, err := s.readTelemetryRecord(view.ID)
	if err != nil {
		s.telemetryState(d, "automatic", "error", "The saved receiver preference could not be read.", telemetryTarget{})
		return
	}
	if view.Capabilities == nil || !view.Capabilities.TelemetryForwarding {
		s.telemetryState(d, record.Mode, "unsupported", "This firmware does not advertise telemetry forwarding; live readings and history remain available.", telemetryTarget{})
		return
	}
	ctx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	observed, err := s.observedTelemetry(ctx, d)
	if err != nil {
		s.telemetryState(d, record.Mode, "error", "The machine's receiver setting could not be read; no change was sent. Live readings and history remain available.", record.Owned)
		return
	}
	if record.Pending != nil {
		if observed.sameConfig(*record.Pending) {
			before := record
			record.Owned, record.Pending, record.Claim = observed, nil, false
			if err := s.writeTelemetryRecord(view.ID, before, record); err != nil {
				s.telemetryState(d, record.Mode, "error", "Receiver readback succeeded but its ownership could not be saved.", observed)
				return
			}
		} else {
			s.telemetryState(d, record.Mode, "outcome_unknown", "The previous receiver change was not confirmed. Reconnect in automatic mode, or save the manual setting again, to authorize another attempt.", observed)
			return
		}
	}
	desired := record.Manual
	if record.Mode == "automatic" {
		var reason string
		desired, reason = s.automaticReceiver(ctx, view.BaseURL, record.Origin)
		if reason != "" {
			s.telemetryState(d, record.Mode, "needs_hub_url", reason, observed)
			return
		}
	}
	if observed.sameConfig(desired) {
		before := record
		record.Owned, record.Claim = desired, false
		if err := s.writeTelemetryRecord(view.ID, before, record); err != nil {
			s.telemetryState(d, record.Mode, "error", "The receiver matches, but its ownership could not be saved.", observed)
			return
		}
		d.telemetryAttempts, d.telemetryClaim = 0, false
		message := "Machine reporting is set up for this Hub."
		if record.Mode == "manual" {
			message = "Manual connection settings are saved on the machine."
		}
		s.telemetryState(d, record.Mode, "configured", message, desired)
		return
	}
	if observed.Host != "" && !observed.sameReceiver(desired) && !record.Claim && !observed.sameReceiver(record.Owned) {
		message := "The machine's receiver was changed elsewhere. Reconnect this machine to pair it with this Hub again. Live readings and history remain available."
		if record.Mode == "manual" {
			message = "The machine's receiver was changed elsewhere. Save the manual override again to apply it; the saved preference is retained."
		}
		s.telemetryState(d, record.Mode, "conflict", message, desired)
		return
	}
	if d.telemetryAttempts >= 3 {
		s.telemetryState(d, record.Mode, "error", "Receiver setup paused after repeated failures. Reconnect or save the manual setting to try again; live readings and history remain available.", desired)
		return
	}
	status, err := s.verifiedStatus(ctx, d)
	if err != nil || !s.currentDevice(d) {
		s.telemetryState(d, record.Mode, "error", "The registered machine identity could not be verified before receiver setup; no change was sent.", desired)
		return
	}
	if status.Busy || status.Stopped || !status.Ready {
		d.telemetryNext = time.Now().Add(s.cfg.PollInterval)
		s.telemetryState(d, record.Mode, "waiting_idle", "The saved receiver setting will be applied when the machine returns to its main menu.", desired)
		return
	}
	before := record
	claim := record.Claim
	record.Pending, record.Claim = &desired, false
	if err := s.writeTelemetryRecord(view.ID, before, record); err != nil {
		s.telemetryState(d, record.Mode, "error", "The receiver change could not be recorded, so no change was sent.", desired)
		return
	}
	d.telemetryAttempts++
	writeErr := d.adapter.UpdateTelemetry(ctx, desired.settings())
	readback, readErr := s.observedTelemetry(ctx, d)
	if readErr == nil && readback.sameConfig(desired) {
		before = record
		record.Owned, record.Pending = desired, nil
		if err := s.writeTelemetryRecord(view.ID, before, record); err != nil {
			s.telemetryState(d, record.Mode, "error", "Receiver setup was verified but its ownership could not be saved.", desired)
			return
		}
		d.telemetryAttempts, d.telemetryClaim = 0, false
		message := "Machine reporting is set up for this Hub."
		if record.Mode == "manual" {
			message = "Manual connection settings are saved on the machine."
		}
		s.telemetryState(d, record.Mode, "configured", message, desired)
		return
	}
	var remote *device.Error
	if errors.As(writeErr, &remote) && !remote.UnknownOutcome {
		before = record
		record.Pending, record.Claim = nil, claim
		_ = s.writeTelemetryRecord(view.ID, before, record)
		s.telemetryState(d, record.Mode, "error", "The machine rejected receiver setup. The Hub will make at most three checked attempts; live readings and history remain available.", desired)
		return
	}
	s.telemetryState(d, record.Mode, "outcome_unknown", "Receiver setup was not confirmed. The Hub will read it back before another change; reconnect or save the setting again to authorize another attempt.", desired)
}

func (s *Server) machineTelemetry(w http.ResponseWriter, r *http.Request) {
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	d.commandMu.Lock()
	defer d.commandMu.Unlock()
	d.mu.RLock()
	view := d.view
	d.mu.RUnlock()
	record, err := s.readTelemetryRecord(view.ID)
	if err != nil {
		s.internal(w, err)
		return
	}
	if r.Method == http.MethodGet {
		target := telemetryTarget{Enabled: view.Telemetry.Enabled, Host: view.Telemetry.Host, Port: view.Telemetry.Port}
		if record.Mode == "manual" {
			target = record.Manual
		}
		out := target.settings()
		out["mode"], out["state"], out["message"] = record.Mode, view.Telemetry.State, view.Telemetry.Message
		s.json(w, 200, out)
		return
	}
	if view.Adapter != "pico-standalone" {
		s.fail(w, 409, "simulator", "Simulation stays local and has no network receiver")
		return
	}
	if !s.currentDevice(d) || d.adapter == nil {
		s.fail(w, 409, "connection", "Connect and identify the machine before changing its receiver")
		return
	}
	var in struct {
		Mode    string  `json:"mode"`
		Enabled *bool   `json:"enabled"`
		Host    *string `json:"host"`
		Port    *int    `json:"port"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if in.Mode == "" {
		in.Mode = "manual"
	}
	before := record
	switch in.Mode {
	case "automatic":
		if in.Enabled != nil || in.Host != nil || in.Port != nil {
			s.fail(w, 400, "telemetry", "Automatic mode determines the receiver from this Hub")
			return
		}
		record.Mode, record.Origin = "automatic", requestReceiverOrigin(r)
	case "manual":
		if in.Enabled == nil || in.Host == nil || in.Port == nil {
			s.fail(w, 400, "telemetry", "Manual mode requires enabled, host and port")
			return
		}
		target := telemetryTarget{Enabled: *in.Enabled, Host: *in.Host, Port: *in.Port}
		if err := device.ValidateTelemetry(target.settings()); err != nil {
			s.deviceError(w, err)
			return
		}
		record.Mode, record.Manual = "manual", target
	default:
		s.fail(w, 400, "telemetry", "Choose automatic or manual receiver mode")
		return
	}
	if _, err := s.verifiedStatus(r.Context(), d); err != nil {
		s.deviceError(w, err)
		return
	}
	record.Claim, record.Pending = true, nil
	if err := s.writeTelemetryRecord(view.ID, before, record); err != nil {
		s.internal(w, err)
		return
	}
	d.telemetryAttempts, d.telemetryNext, d.telemetryOrigin, d.telemetryClaim = 0, time.Time{}, record.Origin, true
	s.syncTelemetry(r.Context(), d, true)
	d.mu.RLock()
	result := d.view.Telemetry
	d.mu.RUnlock()
	s.json(w, 200, result)
}

// releaseTelemetry is best effort when disabling/removing a registration. It
// disables only the exact automatically managed receiver this Hub last owned.
// A manual override or another Hub's destination is deliberately left intact.
func (s *Server) releaseTelemetry(ctx context.Context, d *runtimeDevice) domain.TelemetryStatus {
	d.mu.RLock()
	view := d.view
	d.mu.RUnlock()
	result := domain.TelemetryStatus{Mode: view.Telemetry.Mode, State: "disabled", Message: "Monitoring is disabled."}
	if view.Adapter != "pico-standalone" {
		return result
	}
	record, err := s.readTelemetryRecord(view.ID)
	result.Mode = record.Mode
	if err != nil {
		result.Message = "Monitoring is disabled; the previous receiver setting could not be checked."
		return result
	}
	if record.Mode == "manual" {
		result.Message = "Monitoring is disabled; the saved manual receiver is retained."
		return result
	}
	if record.Owned.Host == "" && (record.Pending == nil || record.Pending.Host == "") {
		return result
	}
	if d.adapter == nil {
		result.Message = "Monitoring is disabled; the machine connection is unavailable, so its previous Hub receiver could not be checked."
		return result
	}
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	status, err := s.verifiedStatus(ctx, d)
	if err != nil || status.Busy || status.Stopped || !status.Ready {
		result.Message = "Monitoring is disabled; the machine could not confirm that its previous Hub receiver was disabled."
		return result
	}
	observed, err := s.observedTelemetry(ctx, d)
	if err != nil {
		result.Message = "Monitoring is disabled; the previous Hub receiver could not be read."
		return result
	}
	owned := observed.sameReceiver(record.Owned) || record.Pending != nil && observed.sameReceiver(*record.Pending)
	if !owned {
		result.Message = "Monitoring is disabled; a receiver configured elsewhere was left unchanged."
		return result
	}
	if !observed.Enabled {
		return result
	}
	desired := observed
	desired.Enabled = false
	before := record
	record.Claim, record.Pending = false, &desired
	if err := s.writeTelemetryRecord(view.ID, before, record); err != nil {
		result.Message = "Monitoring is disabled; the machine receiver could not safely be changed."
		return result
	}
	_ = d.adapter.UpdateTelemetry(ctx, desired.settings())
	readback, err := s.observedTelemetry(ctx, d)
	if err == nil && readback.sameConfig(desired) {
		before = record
		record.Owned, record.Pending = desired, nil
		_ = s.writeTelemetryRecord(view.ID, before, record)
		result.Message = "Monitoring and this Hub's machine receiver are disabled."
	} else {
		result.Message = "Monitoring is disabled; the machine did not confirm that its previous Hub receiver was disabled."
	}
	return result
}
