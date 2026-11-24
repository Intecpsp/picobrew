package server

import (
	"encoding/json"
	"net/http"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestPicoFermBrewerAssociation(t *testing.T) {
	s, h, client := newTestServer(t)
	csrf := signIn(t, client, h.URL)
	devicesURL := h.URL + "/api/v1/devices"
	add := func(name, model string) domain.DeviceView {
		input := keyDeviceInput(name, strings.Repeat("ab", 32))
		input["model"] = model
		return saveKeyDevice(t, client, devicesURL, csrf, "", input)
	}
	ferm := add("Monitor", "picoferm")
	first := add("Pico S", "pico-s")
	second := add("Z module", "z-series")
	still := add("Still", "picostill")
	virtual := saveKeyDevice(t, client, devicesURL, csrf, "", map[string]any{
		"name": "Simulated brewer", "model": "pico-s", "adapter": "simulator", "enabled": false,
	})
	api := func(id, method string, payload any, expected int) picoFermBrewerLink {
		t.Helper()
		code, data := request(t, client, method, devicesURL+"/"+id+"/brewer", csrf, payload, nil)
		if code != expected {
			t.Fatalf("%s /brewer returned %d, want %d: %s", method, code, expected, data)
		}
		var link picoFermBrewerLink
		if expected == 200 {
			if err := json.Unmarshal(data, &link); err != nil {
				t.Fatal(err)
			}
		}
		return link
	}
	expect := func(actual picoFermBrewerLink, id string, revision int64) {
		t.Helper()
		if actual.BrewerID != id || actual.Revision != revision {
			t.Fatalf("unexpected association: %+v; want %s at %d", actual, id, revision)
		}
	}
	path := devicesURL + "/" + ferm.ID + "/brewer"
	if code, _ := request(t, http.DefaultClient, "GET", path, "", nil, nil); code != 401 {
		t.Fatalf("unauthenticated association GET returned %d", code)
	}
	if code, _ := request(t, client, "PUT", path, "", picoFermBrewerLink{first.ID, 0}, nil); code != 403 {
		t.Fatalf("association write without CSRF returned %d", code)
	}
	expect(api(ferm.ID, "GET", nil, 200), "", 0)
	api("missing", "GET", nil, 404)
	api(still.ID, "GET", nil, 400)
	api(ferm.ID, "PUT", map[string]any{"revision": 0}, 400)
	api(ferm.ID, "PUT", map[string]any{"brewer_id": first.ID}, 400)
	api(ferm.ID, "PUT", picoFermBrewerLink{first.ID, -1}, 400)
	api(ferm.ID, "PUT", picoFermBrewerLink{"missing", 0}, 404)
	api(ferm.ID, "PUT", picoFermBrewerLink{ferm.ID, 0}, 400)
	api(ferm.ID, "PUT", picoFermBrewerLink{still.ID, 0}, 400)
	api(ferm.ID, "PUT", picoFermBrewerLink{virtual.ID, 0}, 400)
	expect(api(ferm.ID, "PUT", picoFermBrewerLink{first.ID, 0}, 200), first.ID, 1)
	api(ferm.ID, "PUT", picoFermBrewerLink{second.ID, 0}, 409)
	expect(api(ferm.ID, "PUT", picoFermBrewerLink{second.ID, 1}, 200), second.ID, 2)
	expect(api(ferm.ID, "PUT", picoFermBrewerLink{"", 2}, 200), "", 3)
	expect(api(ferm.ID, "PUT", picoFermBrewerLink{second.ID, 3}, 200), second.ID, 4)

	restarted, err := New(s.cfg)
	if err != nil {
		t.Fatal(err)
	}
	var restored bool
	for _, machine := range restarted.fleet() {
		if machine.ID == ferm.ID {
			restored = machine.BrewerID == second.ID && machine.BrewerRevision == 4
		}
	}
	if err := restarted.Close(); err != nil {
		t.Fatal(err)
	}
	if !restored {
		t.Fatal("fleet did not restore the saved PicoFerm brewer")
	}
	if code, data := request(t, client, "DELETE", devicesURL+"/"+second.ID, csrf, nil, nil); code != 200 {
		t.Fatalf("remove target: %d %s", code, data)
	}
	expect(api(ferm.ID, "GET", nil, 200), "", 5)
	api(ferm.ID, "PUT", picoFermBrewerLink{first.ID, 4}, 409)
	expect(api(ferm.ID, "PUT", picoFermBrewerLink{first.ID, 5}, 200), first.ID, 6)

	// Resolve a manual brewer registration to its actual, ineligible model.
	s.mu.RLock()
	changed := s.devices[first.ID]
	s.mu.RUnlock()
	if err := s.reconcileModel(changed, domain.Capabilities{Model: "picostill", ModelFamily: "picostill"}); err != nil {
		t.Fatal(err)
	}
	expect(api(ferm.ID, "GET", nil, 200), "", 7)
	api(ferm.ID, "PUT", picoFermBrewerLink{first.ID, 7}, 400)
	var saved domain.Device
	if _, err := s.db.Get("device", ferm.ID, &saved); err != nil || saved.BrewerID != "" || saved.BrewerRevision != 7 {
		t.Fatalf("stale persisted association after model change: %+v, %v", saved, err)
	}
	if code, data := request(t, client, "DELETE", devicesURL+"/"+ferm.ID, csrf, nil, nil); code != 200 {
		t.Fatalf("remove PicoFerm: %d %s", code, data)
	}
	api(ferm.ID, "GET", nil, 404)
}
