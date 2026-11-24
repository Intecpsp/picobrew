package server

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
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

func TestGeminiConnectionAndRecipeValidation(t *testing.T) {
	s, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	const key = "gemini-project-test-key"
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("x-goog-api-key") != key || r.Header.Get("Authorization") != "" {
			t.Error("request did not use the Gemini project's credential")
		}
		switch r.URL.Path {
		case "/models":
			_, _ = io.WriteString(w, `{"models":[{"name":"models/gemini-test","displayName":"Test model","supportedGenerationMethods":["generateContent"]}]}`)
		case "/models/gemini-test:generateContent":
			_, _ = io.WriteString(w, `{"candidates":[{"finishReason":"STOP","content":{"parts":[{"text":"{\"text\":\"Invalid draft\",\"recipe\":{\"name\":\"Oops\",\"batch_liters\":-1}}"}]}}]}`)
		default:
			t.Errorf("unexpected model request %q", r.URL.Path)
		}
	}))
	defer provider.Close()
	s.aiClient.GeminiModelsURL = provider.URL + "/models"
	api := h.URL + "/api/v1/ai/"
	cfg := map[string]any{"provider": "gemini", "auth_mode": "api_key", "model": "gemini-test", "enabled": true, "api_key": key}
	status, body := request(t, c, "PUT", api+"settings", csrf, cfg, nil)
	if status != 200 || strings.Contains(string(body), key) {
		t.Fatalf("saving Gemini returned %d", status)
	}
	status, body = request(t, c, "GET", api+"models", csrf, nil, nil)
	if status != 200 || !strings.Contains(string(body), "gemini-test") {
		t.Fatalf("listing Gemini models: %d %s", status, body)
	}
	status, body = request(t, c, "POST", api+"assist", csrf, map[string]string{"mode": "recipe", "prompt": "Draft a recipe"}, nil)
	if status != 502 || !strings.Contains(string(body), "generated recipe failed validation") {
		t.Fatalf("invalid Gemini recipe: %d %s", status, body)
	}
	for _, attempt := range []struct {
		method, path string
		data         any
	}{
		{"GET", "subscription?provider=gemini", nil},
		{"GET", "models?provider=gemini&auth_mode=subscription", nil},
		{"PUT", "settings", map[string]any{"provider": "gemini", "auth_mode": "subscription"}},
	} {
		status, _ = request(t, c, attempt.method, api+attempt.path, csrf, attempt.data, nil)
		if status != 400 {
			t.Errorf("%s %s status %d", attempt.method, attempt.path, status)
		}
	}
}
