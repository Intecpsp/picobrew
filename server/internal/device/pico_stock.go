package device

import (
	"context"
	"encoding/hex"
	"net/http"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
)

type StockRestorer interface {
	RestoreStock(context.Context, string, string, stockfirmware.RestoreImages, string, func(string) error) error
}

type stockState struct {
	OK        bool
	UID       string `json:"device_uid"`
	App       bool   `json:"app_verified"`
	Recovery  bool   `json:"recovery_verified"`
	Committed bool   `json:"boot_committed"`
}

func (p *pico) stockStatus(ctx context.Context, uid string) (stockState, error) {
	var reply struct {
		OK        bool
		UID       string `json:"device_uid"`
		App       *bool  `json:"app_verified"`
		Recovery  *bool  `json:"recovery_verified"`
		Committed *bool  `json:"boot_committed"`
	}
	if err := p.request(ctx, "GET", "/api/v1/stock/status", nil, &reply, 200); err != nil {
		return stockState{}, err
	}
	if !reply.OK || reply.UID != uid || reply.App == nil || reply.Recovery == nil || reply.Committed == nil ||
		*reply.Committed && (!*reply.App || !*reply.Recovery) {
		return stockState{}, otaFailure("The stock restoration worker did not verify the expected brewer and slot state")
	}
	return stockState{true, reply.UID, *reply.App, *reply.Recovery, *reply.Committed}, nil
}

func (p *pico) stockPost(ctx context.Context, uid, operation string, body map[string]any) (int, error) {
	body["device_uid"] = uid
	var ack struct {
		OK     bool
		Offset *int `json:"next_offset"`
	}
	if err := p.request(ctx, "POST", "/api/v1/stock/"+operation, body, &ack, 200); err != nil {
		return 0, err
	}
	if !ack.OK || ack.Offset == nil || *ack.Offset < 0 {
		return 0, otaFailure("The brewer did not acknowledge the stock restoration operation")
	}
	return *ack.Offset, nil
}

func (p *pico) restoreSlot(ctx context.Context, uid, target string, data []byte) error {
	offset, err := p.stockPost(ctx, uid, "begin", map[string]any{"target": target})
	if err != nil {
		return err
	}
	if offset == len(data) {
		return nil
	}
	if offset != 0 {
		return otaFailure("The stock restoration worker returned an unexpected starting offset")
	}
	for offset < len(data) {
		end := min(offset+1024, len(data))
		ack, err := p.stockPost(ctx, uid, "chunk", map[string]any{"offset": offset, "data": hex.EncodeToString(data[offset:end])})
		if err != nil {
			return err
		}
		if ack != end {
			return otaFailure("The stock restoration worker returned an unexpected upload offset")
		}
		offset = end
	}
	ack, err := p.stockPost(ctx, uid, "finish", map[string]any{})
	if err == nil && ack != len(data) {
		return otaFailure("The stock restoration worker did not acknowledge the complete image")
	}
	return err
}

const stockWorkerVersion = "stock-restore-v1"

func (p *pico) startStockWorker(ctx context.Context, uid, model string, worker []byte, previous string, checkpoint func(string) error) error {
	// The preparatory app alone may allocate the third slot, after independently
	// qualifying the flash and current state. Recovery can only write an existing slot.
	if previous == "" || previous == "preparing" {
		var app otaState
		err := p.request(ctx, "GET", "/api/v1/ota", nil, &app, 200)
		if err != nil && previous == "" {
			return err
		}
		if err == nil {
			var caps domain.Capabilities
			if app.Trial || !app.OK || app.UID != uid || app.Model != model || !firmware.ValidID(app.Digest) || app.Size <= 0 || app.Size > firmware.MaxBinaryBytes {
				return otaFailure("The current application did not pass the stock restore preflight")
			}
			if err := p.request(ctx, "GET", "/api/v1", nil, &caps, 200); err != nil {
				return err
			}
			if caps.Model != model || !caps.HubOTAV1 || !caps.StockRestoreOTAV1 {
				return otaFailure("The running firmware cannot prepare a wireless stock restore")
			}
			if err := checkpoint("preparing"); err != nil {
				return err
			}
			var ack otaState
			if err := p.request(ctx, "POST", "/api/v1/ota/stock/prepare", app.otaIdentity, &ack, 200); err != nil {
				return err
			}
			if !ack.OK || ack.otaIdentity != app.otaIdentity {
				return otaFailure("The expected brewer did not acknowledge stock restoration preparation")
			}
		}
	}
	var info recoveryInventory
	var workerReady bool
	if err := awaitBoot(ctx, func(c context.Context) bool {
		if previous == "selecting_worker" {
			if _, err := p.stockStatus(c, uid); err == nil {
				workerReady = true
				return true
			}
		}
		var ok bool
		info, ok = p.readRecovery(c, uid, 3, len(worker))
		return ok
	}); err != nil {
		return err
	}
	if workerReady {
		return nil
	}
	hash := firmware.Digest(worker)
	if info.Upload.Active {
		if previous != "staging_worker" || !info.Plan.Active || info.Plan.Slot != 3 || info.Plan.Version != stockWorkerVersion || info.Upload.ID == 0 || info.Plan.ID != info.Upload.ID || info.Upload.Target != 0 || len(info.Plan.Files) != 1 || info.Plan.Files[0].Target != 0 || info.Plan.Files[0].SHA256 != hash || info.Plan.Files[0].Size != len(worker) {
			return otaFailure("A different recovery upload is active")
		}
		if _, err := p.recoveryPost(ctx, "abort", info.Plan.ID, map[string]any{}); err != nil {
			return err
		}
	}
	if err := checkpoint("staging_worker"); err != nil {
		return err
	}
	session, err := p.recoveryPost(ctx, "plan", 0, map[string]any{"slot": 3, "web": false, "version": stockWorkerVersion, "size": len(worker), "sha256": hash})
	if err != nil {
		return err
	}
	if err = p.transferRecoveryFile(ctx, session.ID, 0, worker); err != nil {
		return err
	}
	if err = checkpoint("selecting_worker"); err != nil {
		return err
	}
	ack, err := p.recoveryPost(ctx, "select", session.ID, map[string]any{"trial": false})
	if err != nil {
		return err
	}
	if !ack.Boot || ack.Mask != 1 {
		return otaFailure("Recovery did not verify and select the stock restoration worker")
	}
	return awaitBoot(ctx, func(c context.Context) bool { _, err := p.stockStatus(c, uid); return err == nil })
}

func (p *pico) RestoreStock(ctx context.Context, uid, model string, images stockfirmware.RestoreImages, previous string, checkpoint func(string) error) error {
	if err := p.lock(ctx); err != nil {
		return err
	}
	defer p.unlock()
	remote := &pico{base: p.base, key: p.key, client: &http.Client{Transport: p.client.Transport, CheckRedirect: p.client.CheckRedirect, Timeout: 30 * time.Second}}
	switch previous {
	case "", "preparing", "staging_worker", "selecting_worker":
		if err := remote.startStockWorker(ctx, uid, model, images.Worker, previous, checkpoint); err != nil {
			return err
		}
	case "application", "recovery", "committing", "rebooting":
	default:
		return otaFailure("Unrecognized stock restoration checkpoint")
	}
	state, err := remote.stockStatus(ctx, uid)
	if err != nil {
		return err
	}
	if previous == "rebooting" && !state.Committed {
		return otaFailure("The previously selected stock boot could not be verified")
	}
	for _, slot := range []struct {
		name  string
		bytes []byte
	}{{"application", images.Application}, {"recovery", images.Recovery}} {
		if slot.name == "application" && state.App || slot.name == "recovery" && state.Recovery {
			continue
		}
		if err = checkpoint(slot.name); err != nil {
			return err
		}
		if err = remote.restoreSlot(ctx, uid, slot.name, slot.bytes); err != nil {
			return err
		}
		state, err = remote.stockStatus(ctx, uid)
		if err != nil {
			return err
		}
		if !state.App || slot.name == "recovery" && !state.Recovery {
			return otaFailure("The brewer did not independently verify the original firmware after writing")
		}
	}
	if !state.Committed {
		if err = checkpoint("committing"); err != nil {
			return err
		}
		if _, err = remote.stockPost(ctx, uid, "commit", map[string]any{}); err != nil {
			return err
		}
		state, err = remote.stockStatus(ctx, uid)
		if err != nil {
			return err
		}
		if !state.Committed {
			return otaFailure("The worker did not confirm the original boot selection")
		}
	}
	// Save the verified handoff before reboot. The stock application cannot
	// acknowledge a Hub request. A transport error must retain this distinction.
	if err = checkpoint("rebooting"); err != nil {
		return err
	}
	_, err = remote.stockPost(ctx, uid, "reboot", map[string]any{})
	return err
}

func (p *pico) VerifyStableApplication(ctx context.Context, uid, model string) error {
	if err := p.lock(ctx); err != nil {
		return err
	}
	defer p.unlock()
	var state otaState
	if err := p.request(ctx, "GET", "/api/v1/ota", nil, &state, 200); err != nil {
		return err
	}
	if !state.OK || state.Trial || state.UID != uid || state.Model != model || !firmware.ValidID(state.Digest) || state.Size <= 0 || state.Size > firmware.MaxBinaryBytes {
		return otaFailure("The paired brewer did not confirm a stable custom boot")
	}
	return nil
}
