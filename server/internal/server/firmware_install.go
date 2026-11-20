package server

import (
	"context"
	"errors"
	"net/http"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

func otaEligible(v domain.DeviceView) bool {
	return v.Adapter == "pico-standalone" && v.Enabled && v.Online && !v.KeyReused &&
		v.ModelSource == "firmware" && v.ModelError == "" && v.Capabilities != nil && v.Capabilities.HubOTAV1 &&
		(v.Capabilities.Model == "pico-s" || v.Capabilities.Model == "pico-pro") &&
		v.Status != nil && v.Status.Ready && !v.Status.Busy && !v.Status.Stopped && v.Status.DeviceUID != ""
}

// Selecting and confirming are persisted before the respective boot mutations.
// A lost reply can leave the current application in trial mode.
func pendingTrial(receipt firmware.InstallReceipt) bool {
	return receipt.State != "installed" && (receipt.Stage == "selecting" || receipt.Stage == "confirming")
}

func (s *Server) savedFirmwareIdentity(id, uid string) (bool, error) {
	var pin deviceIdentity
	_, err := s.db.Get("device-identity", id, &pin)
	if errors.Is(err, store.ErrNotFound) {
		return false, nil
	}
	return err == nil && uid != "" && pin.UID == uid, err
}

func (s *Server) installFirmware(w http.ResponseWriter, r *http.Request) {
	if s.cfg.Demo {
		s.fail(w, 409, "demo_device", "Firmware installation requires a physical brewer")
		return
	}
	var in struct {
		PackageID string `json:"package_id"`
		UID       string `json:"device_uid"`
		Confirm   bool   `json:"confirm"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if !firmware.ValidID(in.PackageID) || !in.Confirm || in.UID == "" {
		s.fail(w, 400, "invalid_install", "Confirm the selected package and brewer")
		return
	}
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	updater, ok := d.adapter.(device.FirmwareUpdater)
	if !ok {
		s.fail(w, 409, "unsupported_firmware", "This adapter cannot install firmware")
		return
	}
	// Never queue a second installation behind another update.
	d.mu.Lock()
	if d.updatingFirmware {
		d.mu.Unlock()
		s.fail(w, 409, "update_running", "A firmware update is already in progress")
		return
	}
	d.updatingFirmware = true
	d.mu.Unlock()
	release := func() { d.mu.Lock(); d.updatingFirmware = false; d.mu.Unlock(); d.commandMu.Unlock() }
	d.commandMu.Lock()
	dispatched := false
	defer func() {
		if !dispatched {
			release()
		}
	}()
	if !s.registeredDevice(d) {
		s.fail(w, 409, "device_changed", "The registration changed")
		return
	}
	d.mu.RLock()
	view := d.view
	mismatch, awaitingIdentity := d.identityMismatch, d.awaitingIdentity
	d.mu.RUnlock()
	if !view.Enabled || view.KeyReused || view.Adapter != "pico-standalone" {
		s.fail(w, 409, "ota_unavailable", "Use an enabled physical brewer with a unique key")
		return
	}
	var prior firmware.InstallReceipt
	priorVersion, err := s.db.Get("firmware-install", view.ID, &prior)
	if err != nil && !errors.Is(err, store.ErrNotFound) {
		s.storeError(w, err)
		return
	}
	resume := prior.PackageID == in.PackageID && prior.UID == in.UID && prior.State != "installed"
	if mismatch && !(resume && awaitingIdentity) {
		s.fail(w, 409, "ota_unavailable", "The registered brewer identity could not be verified")
		return
	}
	if pendingTrial(prior) && !resume {
		s.fail(w, 409, "trial_pending", "Finish verifying the previous firmware package before installing another version")
		return
	}
	model := prior.Model
	if resume {
		// A restarted Hub can find the brewer in recovery, which has no normal
		// status endpoint. Require the original persisted pin; the adapter
		// independently verifies the live application or recovery identity.
		matches, err := s.savedFirmwareIdentity(view.ID, in.UID)
		if err != nil {
			s.storeError(w, err)
			return
		}
		if !matches {
			s.fail(w, 409, "identity_mismatch", "The update does not match the registered brewer identity")
			return
		}
	} else {
		status, err := s.verifiedStatus(r.Context(), d)
		if err != nil {
			s.deviceError(w, err)
			return
		}
		d.mu.RLock()
		ready := otaEligible(d.view)
		d.mu.RUnlock()
		if !ready || status.DeviceUID != in.UID {
			s.fail(w, 409, "ota_unavailable", "Use an idle S/Pro with Hub OTA support, its pinned identity and a unique device key")
			return
		}
		model = status.Model
	}
	var saved firmware.Package
	s.firmwareMu.Lock()
	_, err = s.db.Get(firmwareRecordKind, in.PackageID, &saved)
	var archive []byte
	if err == nil {
		archive, err = s.readFirmwareArchive(in.PackageID)
	}
	s.firmwareMu.Unlock()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	payload, err := firmware.ReadUpdatePayload(archive)
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	if payload.Model != model {
		s.fail(w, 409, "firmware_model", "The package model does not match this brewer")
		return
	}
	receipt := firmware.InstallReceipt{UID: in.UID, Model: model, PackageID: in.PackageID, Started: time.Now().UTC(), State: "running", Message: "Checking recovery, transferring the package and confirming the new boot."}
	previousStage := ""
	if resume {
		previousStage = prior.Stage
		if previousStage == "" {
			previousStage = "transferring"
		}
	}
	receipt.Stage = previousStage
	version, err := s.db.Put("firmware-install", view.ID, receipt, priorVersion)
	if err != nil {
		s.storeError(w, err)
		return
	}
	dispatched = true
	s.wg.Add(1)
	go func(result firmware.InstallReceipt) {
		defer s.wg.Done()
		defer release()
		ctx, cancel := context.WithTimeout(s.ctx, 12*time.Minute)
		defer cancel()
		checkpoint := func(stage string) error {
			result.Stage = stage
			next, err := s.db.Put("firmware-install", view.ID, result, version)
			if err == nil {
				version = next
			}
			return err
		}
		if err := updater.InstallFirmware(ctx, in.UID, payload, previousStage, checkpoint); err != nil {
			result.State = "unconfirmed"
			result.Message = safeMessage(err.Error())
		} else {
			result.State = "installed"
			result.Message = "The brewer verified the installed package and confirmed its new boot selection."
		}
		if _, err := s.db.Put("firmware-install", view.ID, result, version); err != nil {
			s.cfg.Logger.Error("Could not record firmware installation result", "device", view.ID, "error", err)
		}
	}(receipt)
	s.json(w, 202, receipt)
}
