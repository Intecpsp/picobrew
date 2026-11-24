package server

import (
	"encoding/json"
	"maps"
	"net/http"
	"os"
	"slices"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestAccessoryFirmwareTelemetry(t *testing.T) {
	s, h, client := newTestServer(t)
	for _, model := range []string{"picoferm", "picostill"} {
		t.Run(model, func(t *testing.T) {
			// These snapshots come from the 1.0.0 native controllers during an
			// active session, serialized by the firmware's shared status writer.
			frame, err := os.ReadFile("testdata/telemetry-" + model + "-1.0.0.json")
			if err != nil {
				t.Fatal(err)
			}
			var sent, saved struct {
				Status struct {
					DeviceUID    string           `json:"device_uid"`
					StorageReady *bool            `json:"storage_ready"`
					Sensors      []map[string]any `json:"sensors"`
					Actuators    []map[string]any `json:"actuators"`
				} `json:"status"`
				Trust string `json:"trust"`
			}
			if err := json.Unmarshal(frame, &sent); err != nil {
				t.Fatal(err)
			}
			url := h.URL + "/api/v1/telemetry"
			code, body := request(t, client, http.MethodPost, url, "", json.RawMessage(frame), nil)
			if code != http.StatusForbidden {
				t.Fatalf("unregistered accessory: %d %s", code, body)
			}
			uid := sent.Status.DeviceUID
			s.mu.Lock()
			s.devices[model] = &runtimeDevice{cancel: func() {}, view: domain.DeviceView{
				Device: domain.Device{ID: model, Model: model, Enabled: true, Adapter: "pico-standalone"},
				Status: &domain.Status{DeviceUID: uid},
			}}
			s.mu.Unlock()
			code, body = request(t, client, http.MethodPost, url, "", json.RawMessage(frame), nil)
			if code != http.StatusAccepted {
				t.Fatalf("registered accessory: %d %s", code, body)
			}
			if _, err := s.db.Get("forwarded-telemetry", uid, &saved); err != nil {
				t.Fatal(err)
			}
			if saved.Trust != "unsigned" || saved.Status.StorageReady == nil || !*saved.Status.StorageReady ||
				!slices.EqualFunc(saved.Status.Sensors, sent.Status.Sensors, maps.Equal) ||
				!slices.EqualFunc(saved.Status.Actuators, sent.Status.Actuators, maps.Equal) {
				t.Fatalf("stored observation lost firmware data: %+v", saved)
			}
		})
	}
}
