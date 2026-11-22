package device

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestDisplayGeometry(t *testing.T) {
	for _, tc := range []struct {
		name     string
		height   int
		rotation *int
		variant  string
		storage  int
		transfer int
	}{
		{name: "legacy S and Pro", height: 64},
		{name: "C SSD1325", height: 64, rotation: displayRotation(2), variant: "ssd1325", storage: 1024, transfer: 1024},
		{name: "C SSD1309", height: 32, rotation: displayRotation(0), variant: "ssd1309", storage: 1024, transfer: 1024},
	} {
		t.Run(tc.name, func(t *testing.T) {
			payload := map[string]any{"width": 128, "height": tc.height, "format": "page-lsb-hex", "data": strings.Repeat("a5", 1024)}
			if tc.rotation != nil {
				payload["rotation"] = *tc.rotation
				payload["display_variant"] = tc.variant
				payload["storage_bytes"] = tc.storage
				payload["transfer_bytes"] = tc.transfer
			}
			p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
				if r.Method != http.MethodGet || r.URL.Path != "/api/v1/display" {
					t.Fatalf("unexpected display request: %s %s", r.Method, r.URL.Path)
				}
				replyJSON(w, http.StatusOK, payload)
			})
			display, err := p.Display(context.Background())
			if err != nil {
				t.Fatal(err)
			}
			if display.Height != tc.height || display.StorageBytes != tc.storage || display.TransferBytes != tc.transfer || display.DisplayVariant != tc.variant || display.Data != payload["data"] {
				t.Fatalf("display geometry or raw storage changed: %+v", display)
			}
			if tc.rotation == nil {
				if display.Rotation != nil {
					t.Fatal("legacy response gained an explicit rotation")
				}
			} else if display.Rotation == nil || *display.Rotation != *tc.rotation {
				t.Fatal("explicit rotation was lost")
			}
			encoded, err := json.Marshal(display)
			if err != nil {
				t.Fatal(err)
			}
			if tc.rotation != nil && *tc.rotation == 0 && !strings.Contains(string(encoded), `"rotation":0`) {
				t.Fatal("SSD1309 rotation zero must survive Hub serialization")
			}
		})
	}
}

func TestDisplayRejectsInconsistentGeometry(t *testing.T) {
	for _, tc := range []struct {
		name  string
		field string
		value any
	}{
		{"wrong width", "width", 256}, {"wrong height", "height", 48},
		{"wrong format", "format", "rgba"}, {"invalid rotation", "rotation", 1},
		{"short visible storage", "storage_bytes", 511}, {"oversized storage", "storage_bytes", 2048},
		{"short transfer", "transfer_bytes", 511}, {"oversized transfer", "transfer_bytes", 1025},
		{"truncated raw frame", "data", strings.Repeat("00", 512)}, {"invalid hex", "data", strings.Repeat("xx", 1024)},
	} {
		t.Run(tc.name, func(t *testing.T) {
			payload := map[string]any{"width": 128, "height": 32, "rotation": 0, "storage_bytes": 1024, "transfer_bytes": 1024, "format": "page-lsb-hex", "data": strings.Repeat("00", 1024)}
			payload[tc.field] = tc.value
			p := testPico(t, func(w http.ResponseWriter, r *http.Request) { replyJSON(w, http.StatusOK, payload) })
			_, err := p.Display(context.Background())
			if failure := deviceFailure(t, err); failure.Code != "invalid_response" {
				t.Fatalf("unexpected error: %v", failure)
			}
		})
	}
}

func displayRotation(value int) *int { return &value }

func TestZymaticReadOnlyDisplayAndBusyProgram(t *testing.T) {
	t.Run("complete authenticated frame", func(t *testing.T) {
		pages := 0
		p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
			offset := pages * 128
			expected := "/api/v1/display"
			if offset != 0 {
				expected += fmt.Sprintf("/%d?revision=113855602", offset)
			}
			if r.URL.RequestURI() != expected || r.Method != http.MethodGet {
				t.Fatalf("unexpected display request at offset %d", offset)
			}
			page := map[string]any{"width": 128, "height": 64, "format": "page-lsb-hex",
				"total_bytes": 1024, "offset": offset, "revision": 113855602,
				"data": strings.Repeat(fmt.Sprintf("%02x", pages), 128)}
			if pages < 7 {
				page["next_offset"] = offset + 128
			}
			pages++
			replyJSON(w, 200, page)
		})
		p.capabilities = &domain.Capabilities{Model: "zymatic", Features: map[string]bool{"front_panel": false, "display": true}}
		frame, err := p.Display(context.Background())
		if err != nil || pages != 8 || len(frame.Data) != 2048 {
			t.Fatalf("assembled %d pages, %d hex digits: %v", pages, len(frame.Data), err)
		}
		for page := 0; page < 8; page++ {
			if frame.Data[page*256:(page+1)*256] != strings.Repeat(fmt.Sprintf("%02x", page), 128) {
				t.Fatalf("incorrect pixel data on page %d", page)
			}
		}
	})
	t.Run("busy program", func(t *testing.T) {
		calls := 0
		p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
			calls++
			replyJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "display_busy", "message": "Display resumes when the program exits."})
		})
		p.capabilities = &domain.Capabilities{Features: map[string]bool{"display": true}}
		frame, err := p.Display(context.Background())
		fault := deviceFailure(t, err)
		if calls != 1 || frame.Data != "" || fault.StatusCode != 503 || fault.Code != "display_busy" {
			t.Fatalf("busy poll = %d requests, %q, %v", calls, frame.Data, fault)
		}
	})
}
