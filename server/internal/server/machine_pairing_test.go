package server

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
	"testing"
	"time"
)

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
