package server

import (
	"errors"
	"net/http"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

// This is a user's hardware observation, never an inference from firmware
// strings or screen dimensions. Bind it to the authenticated, pinned UID so an
// address change cannot transfer the observation to another appliance.
type firmwareHardware struct {
	DeviceUID      string    `json:"device_uid"`
	DisplayVariant string    `json:"display_variant"`
	VerifiedAt     time.Time `json:"verified_at"`
}

func (s *Server) verifyFirmwareHardware(w http.ResponseWriter, r *http.Request) {
	var in struct {
		DeviceUID      string `json:"device_uid"`
		DisplayVariant string `json:"display_variant"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if in.DeviceUID == "" || len(in.DeviceUID) > 256 || !firmware.ValidVariant("pico-c", in.DisplayVariant) {
		s.fail(w, 400, "firmware_hardware", "Choose the C display controller you independently verified and the current device identity")
		return
	}
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	d.commandMu.Lock()
	defer d.commandMu.Unlock()
	if !s.currentDevice(d) {
		s.fail(w, 409, "connection_changed", "Connect and identify this Pico C before recording its display controller")
		return
	}
	status, err := s.verifiedStatus(r.Context(), d)
	if err != nil {
		s.deviceError(w, err)
		return
	}
	d.mu.RLock()
	valid := d.view.Adapter == "pico-standalone" && d.view.Model == "pico-c" &&
		d.view.Capabilities != nil && status.DeviceUID == in.DeviceUID
	id := d.view.ID
	d.mu.RUnlock()
	if !valid || !s.currentDevice(d) {
		s.fail(w, 409, "firmware_hardware", "The machine identity changed; reload before recording its controller")
		return
	}
	record := firmwareHardware{DeviceUID: in.DeviceUID, DisplayVariant: in.DisplayVariant, VerifiedAt: time.Now().UTC()}
	if _, err := s.db.Put("firmware-hardware", id, record, -1); err != nil {
		s.storeError(w, err)
		return
	}
	s.json(w, http.StatusOK, record)
}

func (s *Server) verifiedFirmwareDisplay(id, uid string) (string, error) {
	var record firmwareHardware
	_, err := s.db.Get("firmware-hardware", id, &record)
	if errors.Is(err, store.ErrNotFound) {
		return "", nil
	}
	if err != nil {
		return "", err
	}
	if record.DeviceUID != uid || !firmware.ValidVariant("pico-c", record.DisplayVariant) {
		return "", nil
	}
	return record.DisplayVariant, nil
}
