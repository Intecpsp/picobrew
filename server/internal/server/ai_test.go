package server

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/ai"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
)

func TestAIMonitorIsOptInBoundedAndStoresAdvice(t *testing.T) {
	s, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	simulator := device.NewSimulator("demo-pro", "pico-pro")
	installTestDevice(t, s, "busy", simulator)
	calls := 0
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		if r.Header.Get("Authorization") != "Bearer test-ai-key" {
			t.Error("missing provider key")
		}
		var wire map[string]json.RawMessage
		if err := json.NewDecoder(r.Body).Decode(&wire); err != nil {
			t.Fatal(err)
		}
		if bytes.Contains(wire["messages"], []byte("test-ai-key")) {
			t.Error("credential leaked into AI context")
		}
		if _, ok := wire["tools"]; ok {
			t.Error("AI was granted tools")
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"choices": []any{map[string]any{"message": map[string]string{"content": "Recorded temperature is near target. Continue observing."}}}})
	}))
	defer provider.Close()
	s.aiClient = ai.New()
	s.aiClient.OpenAIURL = provider.URL
	s.monitorTick(context.Background())
	if calls != 0 {
		t.Fatal("monitor called provider before configuration")
	}
	status, p := request(t, c, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "model": "chosen-model", "api_key": "test-ai-key", "enabled": true, "monitor_enabled": false}, nil)
	if status != 200 {
		t.Fatalf("settings %d %s", status, p)
	}
	s.monitorTick(context.Background())
	if calls != 0 {
		t.Fatal("monitor ran without opt-in")
	}
	status, p = request(t, c, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "model": "chosen-model", "enabled": true, "monitor_enabled": true}, nil)
	if status != 200 {
		t.Fatalf("settings %d %s", status, p)
	}
	s.monitorTick(context.Background())
	if calls != 0 {
		t.Fatal("legacy global flag enabled monitoring without consent for this brew")
	}
	status, p = request(t, c, "GET", h.URL+"/api/v1/devices/busy/ai-monitor", csrf, nil, nil)
	var current aiMonitorStatus
	if status != 200 || json.Unmarshal(p, &current) != nil || current.CurrentBrewID == "" || !current.Configured || current.Enabled {
		t.Fatalf("current brew: %d %s", status, p)
	}
	status, p = request(t, c, "PUT", h.URL+"/api/v1/devices/busy/ai-monitor", csrf, map[string]any{"enabled": true, "brew_id": current.CurrentBrewID}, nil)
	if status != 200 {
		t.Fatalf("explicit brew consent: %d %s", status, p)
	}
	s.monitorTick(context.Background())
	s.monitorTick(context.Background())
	if calls != 1 {
		t.Fatalf("five minute monitor limit did not hold: %d calls", calls)
	}
	status, p = request(t, c, "GET", h.URL+"/api/v1/ai/advice?device_id=busy", csrf, nil, nil)
	if status != 200 {
		t.Fatalf("advice %d %s", status, p)
	}
	var out []adviceRecord
	if err := json.Unmarshal(p, &out); err != nil || len(out) != 1 || out[0].Status != "complete" || out[0].Text == "" || out[0].BrewID != current.CurrentBrewID {
		t.Fatalf("missing visible advice: %s (%v)", p, err)
	}
	// A monitor tick does not issue stop/encoder/temperature commands.
	st, err := simulator.Status(context.Background())
	if err != nil || !st.Busy || st.Stopped {
		t.Fatalf("monitor changed machine: %+v %v", st, err)
	}
}

func TestInvalidAIRecipeIsNeverSaved(t *testing.T) {
	s, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(map[string]any{"choices": []any{map[string]any{"message": map[string]string{"content": `{"text":"Invalid draft","recipe":{"name":"Oops","batch_liters":-1}}`}}}})
	}))
	defer provider.Close()
	s.aiClient = ai.New()
	s.aiClient.OpenAIURL = provider.URL
	status, p := request(t, c, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "model": "m", "api_key": "test-ai-key", "enabled": true}, nil)
	if status != 200 {
		t.Fatal(status, string(p))
	}
	status, p = request(t, c, "POST", h.URL+"/api/v1/ai/assist", csrf, map[string]string{"mode": "recipe", "prompt": "Draft a recipe"}, nil)
	if status != 502 {
		t.Fatalf("invalid AI recipe accepted: %d %s", status, p)
	}
	records, err := s.db.List("recipe")
	if err != nil || len(records) != 0 {
		t.Fatal("AI wrote an unreviewed recipe", err)
	}
}
