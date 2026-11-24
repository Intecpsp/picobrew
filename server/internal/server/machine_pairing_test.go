package server

import (
	"context"
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/usbservice"
)

func TestRegisterPicoFermWithUSBHandoff(t *testing.T) {
	s, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	key, uid := strings.Repeat("ab", 32), "ac1234567890"
	job := usbservice.USBJob{ID: strings.Repeat("d", 32), State: "complete", USBInput: usbservice.USBInput{Action: "install", Model: "picoferm"}}
	if _, err := s.db.Put("usb-job", job.ID, job, 0); err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join(s.cfg.DataDir, "usb-jobs", job.ID)
	if err := privatefs.EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	for name, value := range map[string]string{"device-key": key, "device-uid": uid} {
		f, err := privatefs.OpenFile(filepath.Join(dir, name), os.O_CREATE|os.O_EXCL|os.O_WRONLY)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := f.WriteString(value); err != nil {
			t.Fatal(err)
		}
		if err := f.Close(); err != nil {
			t.Fatal(err)
		}
	}
	address := "http://keg-monitor.local"
	monitor := &identityAdapter{}
	s.newDeviceAdapter = func(base, credential string) (device.Adapter, error) {
		if base != address || credential != key {
			t.Error("registration probe did not use the provisioned key at the selected address")
		}
		return monitor, nil
	}
	register := func(reportedUID string) (int, []byte) {
		t.Helper()
		monitor.mu.Lock()
		monitor.uid = reportedUID
		monitor.mu.Unlock()
		return request(t, c, "POST", h.URL+"/api/v1/devices", csrf, map[string]any{
			"name": "Keg monitor", "model": "picoferm", "adapter": "pico-standalone", "base_url": address, "usb_job_id": job.ID, "enabled": false,
		}, nil)
	}
	if code, raw := register("ac1234567899"); code != 409 || !strings.Contains(string(raw), "usb_identity") || len(s.fleet()) != 0 {
		t.Fatalf("different physical monitor: %d %s", code, raw)
	}
	code, raw := register(uid)
	if code != 201 || strings.Contains(string(raw), key) || len(s.fleet()) != 1 {
		t.Fatalf("registration with verified USB credentials: %d %s", code, raw)
	}
	id := mapFromJSON(t, raw)["id"].(string)
	if saved, err := s.readSecret("device:" + id); err != nil || saved != key {
		t.Fatal("Hub did not retain the provisioned key")
	}
}

type dialApproval struct {
	nonce, key string
	approved   bool
	claims     int
}

func (p *dialApproval) Request(_ context.Context, nonce string) error { p.nonce = nonce; return nil }
func (p *dialApproval) Claim(_ context.Context, nonce string) (string, error) {
	p.claims++
	if p.approved && p.nonce == nonce {
		p.approved = false
		return p.key, nil
	}
	return "", nil
}

func TestHubPairingStoresTheDeviceKeyOnceWithoutUserEntry(t *testing.T) {
	s, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	dial := &dialApproval{key: strings.Repeat("ab", 32)}
	s.newPairingClient = func(string) (registrationPairer, error) { return dial, nil }
	root := h.URL + "/api/v1/devices"
	input := keyDeviceInput("Dial paired", "")
	base := input["base_url"].(string)
	_, raw := request(t, c, "POST", root+"/pair", csrf, map[string]string{"base_url": base}, nil)
	reg := mapFromJSON(t, raw)
	id, ok := reg["pairing_id"].(string)
	if !ok || len(dial.nonce) != 64 || reg["code"] != dial.nonce[:8] || strings.Contains(string(raw), dial.nonce) {
		t.Fatal("owner code or request handle missing")
	}
	input["pairing_id"] = id
	input["base_url"] = "http://other.example"
	if code, _ := request(t, c, "POST", root, csrf, input, nil); code != 409 || dial.claims != 0 {
		t.Fatal("claim at another address")
	}
	input["base_url"] = base
	if code, _ := request(t, c, "POST", root, csrf, input, nil); code != 202 || len(s.fleet()) != 0 {
		t.Fatal("registered before physical approval")
	}
	dial.approved = true
	code, raw := request(t, c, "POST", root, csrf, input, nil)
	if code != 201 || strings.Contains(string(raw), dial.key) {
		t.Fatal("registration failed or leaked credentials")
	}
	devID := mapFromJSON(t, raw)["id"].(string)
	code, _ = request(t, c, "POST", root, csrf, input, nil)
	if code != 200 || len(s.fleet()) != 1 || dial.claims != 2 {
		t.Fatal("register retry duplicated machine or consumed claim")
	}
	path := root + "/" + devID + "/key"
	if code, _ := request(t, &http.Client{}, "POST", path, "", nil, nil); code == 200 {
		t.Fatal("anonymous key retrieval")
	}
	if code, _ := request(t, c, "POST", path, "", nil, nil); code == 200 {
		t.Fatal("key retrieval without CSRF")
	}
	code, raw = request(t, c, "POST", path, csrf, nil, nil)
	var key struct {
		Value string `json:"api_key"`
	}
	_ = json.Unmarshal(raw, &key)
	if code != 200 || key.Value != dial.key {
		t.Fatal("owner could not retrieve saved key")
	}
	s.deviceMu.Lock()
	s.pairings[id].expires = time.Now().Add(-time.Second)
	s.deviceMu.Unlock()
	if code, _ := request(t, c, "POST", root, csrf, input, nil); code != 409 {
		t.Fatal("expired pairing accepted")
	}
}
