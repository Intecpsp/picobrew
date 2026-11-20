package server

import (
	"errors"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

func modelControlReady(v domain.DeviceView) bool {
	return v.Adapter != "pico-standalone" || device.SupportedModel(v.Model) && v.ModelError == ""
}

func validateStatusModel(status domain.Status, caps domain.Capabilities) error {
	if _, err := device.ReportedModel(caps); err != nil {
		return err
	}
	model, err := device.ReportedModel(domain.Capabilities{Model: status.Model, ModelName: status.ModelName, ModelFamily: status.ModelFamily, DisplayVariant: status.DisplayVariant})
	if err != nil {
		return err
	}
	if model == "" && (caps.Model == "" || caps.Model == "pico-s" || caps.Model == "pico-pro") {
		return nil
	} // Older status snapshots omitted identity fields.
	if model != caps.Model || status.ModelFamily != "" && caps.ModelFamily != "" && status.ModelFamily != caps.ModelFamily || status.ModelName != "" && caps.ModelName != "" && status.ModelName != caps.ModelName || model == "pico-c" && status.DisplayVariant != caps.DisplayVariant {
		return &device.Error{StatusCode: 409, Code: "invalid_model", Message: "The authenticated status and discovery report inconsistent firmware model identities"}
	}
	return nil
}

// registeredDevice deliberately excludes model/UID readiness: discovery must be
// able to establish both before currentDevice can permit any mutation.
func (s *Server) registeredDevice(d *runtimeDevice) bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	d.mu.RLock()
	defer d.mu.RUnlock()
	return s.devices[d.view.ID] == d && s.ctx.Err() == nil && d.view.Enabled
}

// reconcileModel is called only after an authenticated status has passed the
// create-only UID pin. Update the registration, never historical brew snapshots.
// The store comparison preserves concurrent connection/name edits.
func (s *Server) reconcileModel(d *runtimeDevice, caps domain.Capabilities) error {
	model, err := device.ReportedModel(caps)
	if err != nil {
		return err
	}
	s.mu.RLock()
	defer s.mu.RUnlock()
	d.mu.Lock()
	defer d.mu.Unlock()
	if s.devices[d.view.ID] != d || s.ctx.Err() != nil {
		return &device.Error{StatusCode: 409, Code: "connection_changed", Message: "The machine connection changed during identification"}
	}
	if model == "" {
		if (d.view.Model == "pico-s" || d.view.Model == "pico-pro") && d.view.ModelSource != "firmware" {
			d.view.ModelSource = "manual"
			d.view.ModelError, d.view.ModelErrorCode, d.view.ModelSelectionRequired = "", "", false
			return nil
		}
		d.view.ModelError = "This older firmware does not report a model. Select Pico S or Pico Pro in Edit connection, or update its firmware."
		d.view.ModelErrorCode, d.view.ModelSelectionRequired = "not_reported", true
		return &device.Error{StatusCode: 409, Code: "model_required", Message: d.view.ModelError}
	}
	if d.view.Model != model || d.view.ModelSource != "firmware" {
		var saved domain.Device
		version, err := s.db.Get("device", d.view.ID, &saved)
		if err == nil && (saved.Adapter != d.view.Adapter || saved.BaseURL != d.view.BaseURL) {
			err = store.ErrConflict
		}
		if err == nil {
			saved.Model, saved.ModelSource = model, "firmware"
			_, err = s.db.Put("device", saved.ID, saved, version)
		}
		if err != nil {
			d.view.ModelErrorCode, d.view.ModelSelectionRequired = "storage", false
			d.view.ModelError = "The detected model could not be saved; reconnect after the connection change completes."
			if !errors.Is(err, store.ErrConflict) {
				s.cfg.Logger.Debug("Model identification could not be saved", "error", err)
			}
			return &device.Error{StatusCode: 503, Code: "model_storage", Message: d.view.ModelError}
		}
		d.view.Model, d.view.ModelSource = model, "firmware"
	}
	d.view.ModelError, d.view.ModelErrorCode, d.view.ModelSelectionRequired = "", "", false
	return nil
}
