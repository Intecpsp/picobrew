package server

import (
	"context"
	"errors"
	"net/http"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

func (s *Server) stockReceipt(id string) (*firmware.StockReceipt, int64, error) {
	var receipt firmware.StockReceipt
	version, err := s.db.Get("firmware-stock-restore", id, &receipt)
	if errors.Is(err, store.ErrNotFound) {
		return nil, 0, nil
	}
	return &receipt, version, err
}

// These published S/Pro releases support Hub OTA and predate stock restore.
// A later or unidentified build must advertise the restoration capability.
func stockUpgradeAvailable(installed string) bool {
	switch installed {
	case "1.3.0", "1.3.1", "1.3.2", "1.3.3", "1.3.4", "1.3.5":
		return true
	}
	return false
}

func stockPending(receipt *firmware.StockReceipt) bool {
	return receipt != nil && receipt.Stage != "" && (receipt.State == "running" || receipt.State == "interrupted")
}

func stockIssue(stage, detail string) string {
	if stage == "committing" {
		return "The brewer verified both original images. The Hub could not confirm the stock boot selection. Check the brewer’s display. " + detail
	}
	if stage == "rebooting" {
		return "Both original images and the stock boot selection were verified. The Hub could not confirm the restart. Check the brewer’s display. " + detail
	}
	return detail
}

var stockStages = map[string]string{
	"preparing":        "Preparing the brewer and its separate restoration slot.",
	"staging_worker":   "Writing and verifying the restoration worker.",
	"selecting_worker": "Starting the verified restoration worker.",
	"application":      "Restoring the original application.",
	"recovery":         "Restoring the original recovery image.",
	"committing":       "Both original images verified. Selecting the stock application.",
	"rebooting":        "Original images and boot selection verified. Requesting restart into stock firmware.",
}

func (s *Server) restoreStock(w http.ResponseWriter, r *http.Request) {
	var in struct {
		UID         string `json:"device_uid"`
		Acknowledge bool   `json:"acknowledge_loss_of_hub"`
		Confirm     bool   `json:"confirm_restore"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if !in.Acknowledge || !in.Confirm || in.UID == "" {
		s.fail(w, 400, "invalid_restore", "Acknowledge the loss of Hub controls and confirm restoration on this brewer")
		return
	}
	if s.cfg.Demo {
		s.fail(w, 409, "demo_device", "Stock restoration requires a physical brewer")
		return
	}
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	updater, ok := d.adapter.(device.StockRestorer)
	if !ok {
		s.fail(w, 409, "unsupported_firmware", "This adapter cannot restore original firmware over Wi-Fi")
		return
	}
	d.mu.Lock()
	if d.updatingFirmware {
		d.mu.Unlock()
		s.fail(w, 409, "update_running", "A firmware operation is already running")
		return
	}
	d.updatingFirmware = true
	d.mu.Unlock()
	d.commandMu.Lock()
	released := false
	release := func() { d.mu.Lock(); d.updatingFirmware = false; d.mu.Unlock(); d.commandMu.Unlock() }
	defer func() {
		if !released {
			release()
		}
	}()
	if !s.registeredDevice(d) {
		s.fail(w, 409, "device_changed", "The registration changed")
		return
	}
	d.mu.RLock()
	view := d.view
	mismatch, awaiting := d.identityMismatch, d.awaitingIdentity
	d.mu.RUnlock()
	receipt, version, err := s.stockReceipt(view.ID)
	if err != nil {
		s.storeError(w, err)
		return
	}
	resume := stockPending(receipt)
	pin, err := s.savedFirmwareIdentity(view.ID, in.UID)
	if err != nil {
		s.storeError(w, err)
		return
	}
	if !pin || !view.Enabled || view.KeyReused || view.Adapter != "pico-standalone" || mismatch && !(resume && awaiting) || resume && receipt.UID != in.UID {
		s.fail(w, 409, "identity_mismatch", "The restoration must match the enabled, paired brewer and its unique device key")
		return
	}
	var normal firmware.InstallReceipt
	if _, err = s.db.Get("firmware-install", view.ID, &normal); err != nil && !errors.Is(err, store.ErrNotFound) {
		s.storeError(w, err)
		return
	}
	if conflict := pendingFirmwareConflict(normal); conflict != "" {
		s.fail(w, 409, conflict, "Finish the previous firmware update before restoring stock")
		return
	}
	stage := ""
	if resume {
		stage = receipt.Stage
	} else {
		status, err := s.verifiedStatus(r.Context(), d)
		if err != nil {
			s.deviceError(w, err)
			return
		}
		d.mu.RLock()
		eligible := otaEligible(d.view) && d.view.Capabilities.StockRestoreOTAV1
		d.mu.RUnlock()
		if !eligible || status.DeviceUID != in.UID || !stockfirmware.WirelessProfile(status.Model, in.UID) {
			s.fail(w, 409, "stock_restore_unavailable", "Wireless stock restoration requires a qualified, idle brewer with compatible custom firmware")
			return
		}
		receipt = &firmware.StockReceipt{Started: time.Now().UTC(), UID: in.UID, Model: status.Model}
	}
	if !stockfirmware.WirelessProfile(receipt.Model, receipt.UID) {
		s.fail(w, 409, "stock_restore_unavailable", "This brewer has no wireless stock restoration profile")
		return
	}
	receipt.State = "running"
	receipt.Message = "Checking the original firmware in Hub storage and the verified GitHub fallback."
	version, err = s.db.Put("firmware-stock-restore", view.ID, *receipt, version)
	if err != nil {
		s.storeError(w, err)
		return
	}
	released = true
	s.wg.Add(1)
	go func(result firmware.StockReceipt) {
		defer s.wg.Done()
		defer release()
		ctx, cancel := context.WithTimeout(s.ctx, 15*time.Minute)
		defer cancel()
		checkpoint := func(next string) error {
			msg, ok := stockStages[next]
			if !ok {
				return errors.New("unknown stock restoration stage")
			}
			changed := result
			changed.Stage = next
			changed.Message = msg
			v, e := s.db.Put("firmware-stock-restore", view.ID, changed, version)
			if e == nil {
				version = v
				result = changed
			}
			return e
		}
		images, e := stockfirmware.WirelessImages(ctx, s.cfg.DataDir, result.Model, in.UID, s.firmwareGitHub)
		if e != nil {
			result.State = "interrupted"
			result.Message = stockIssue(result.Stage, "The Hub could not obtain verified original firmware. Check the saved backup or GitHub connection and retry.")
		} else if e = updater.RestoreStock(ctx, in.UID, result.Model, images, stage, checkpoint); e != nil {
			result.State = "interrupted"
			result.Message = stockIssue(result.Stage, safeMessage(e.Error()))
		} else {
			result.State = "restart_requested"
			result.Message = "Both original images and the stock boot selection were verified. The brewer acknowledged the restart. Confirm stock startup on its display."
		}
		if _, e := s.db.Put("firmware-stock-restore", view.ID, result, version); e != nil {
			s.cfg.Logger.Error("Could not record stock restoration result", "device", view.ID, "error", e)
		}
	}(*receipt)
	s.json(w, 202, receipt)
}

// A receipts-only action for an OEM application which cannot call the Hub back.
// Bind the observation to the paired machine and this specific restoration.
func (s *Server) observeStockStartup(w http.ResponseWriter, r *http.Request) {
	var in struct {
		UID     string    `json:"device_uid"`
		Started time.Time `json:"started"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	d.commandMu.Lock()
	defer d.commandMu.Unlock()
	d.mu.RLock()
	id := d.view.ID
	allowed := !s.cfg.Demo && !d.updatingFirmware && d.view.Enabled && d.view.Adapter == "pico-standalone" && !d.view.KeyReused && (!d.identityMismatch || d.awaitingIdentity)
	d.mu.RUnlock()
	match, err := s.savedFirmwareIdentity(id, in.UID)
	if err != nil {
		s.storeError(w, err)
		return
	}
	receipt, version, err := s.stockReceipt(id)
	if err != nil {
		s.storeError(w, err)
		return
	}
	if !allowed || !s.registeredDevice(d) || !match || !canObserveStock(receipt) || receipt.UID != in.UID || !receipt.Started.Equal(in.Started) {
		s.fail(w, 409, "stock_handoff_unverified", "This restoration must have verified both original images and reached stock boot selection for the paired brewer")
		return
	}
	if receipt.Stage == "committing" {
		receipt.State = "owner_reported"
		receipt.Message = "The brewer verified both original images. You reported seeing the stock startup screen. The Hub did not confirm the boot selection."
	} else {
		receipt.State = "owner_confirmed"
		receipt.Message = "Both original images and boot selection were verified. You confirmed the stock startup screen on the brewer."
	}
	if _, err = s.db.Put("firmware-stock-restore", id, *receipt, version); err != nil {
		s.storeError(w, err)
		return
	}
	s.json(w, 200, receipt)
}
func canObserveStock(receipt *firmware.StockReceipt) bool {
	if receipt == nil {
		return false
	}
	switch receipt.Stage {
	case "committing":
		// The worker independently verified both images before this checkpoint;
		// the Hub has no durable acknowledgment of the stock selector.
		return receipt.State == "running" || receipt.State == "interrupted"
	case "rebooting":
		return receipt.State == "running" || receipt.State == "interrupted" || receipt.State == "restart_requested"
	default:
		return false
	}
}

// Abandon the previous stock journal only after observing this same paired
// machine running a normal, stable custom application again. This also covers
// a USB reconversion after a lost final stock reboot reply.
func (s *Server) reconcileCustomBoot(ctx context.Context, d *runtimeDevice, uid string, receipt *firmware.StockReceipt, version int64) error {
	observer, ok := d.adapter.(interface {
		VerifyStableApplication(context.Context, string, string) error
	})
	if !ok || receipt.UID != uid {
		return errors.New("Previous stock restoration is still pending")
	}
	status, err := s.verifiedStatus(ctx, d)
	if err != nil {
		return err
	}
	d.mu.RLock()
	ready := otaEligible(d.view) && !d.identityMismatch
	id := d.view.ID
	d.mu.RUnlock()
	if !ready || status.DeviceUID != uid || status.Model != receipt.Model {
		return errors.New("The expected idle custom application did not confirm its identity")
	}
	if err = observer.VerifyStableApplication(ctx, uid, receipt.Model); err != nil {
		return err
	}
	receipt.State = "custom_verified"
	receipt.Message = "The Hub verified this brewer is running stable custom firmware again. The previous stock restoration is closed."
	_, err = s.db.Put("firmware-stock-restore", id, *receipt, version)
	return err
}
