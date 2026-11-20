package device

import (
	"context"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"net/http"
	"testing"
)

func TestCIdentityRequiresExplicitController(t *testing.T) {
	for _, display := range []string{"ssd1325", "ssd1309"} {
		t.Run(display, func(t *testing.T) {
			p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
				replyJSON(w, 200, map[string]any{"api_version": 1, "mode": "standalone", "model": "pico-c", "model_name": "Pico C", "model_family": "pico-c", "display_variant": display, "commands": []string{"stop", "encoder"}})
			})
			caps, err := p.Discover(context.Background())
			if err != nil || caps.Model != "pico-c" || caps.DisplayVariant != display {
				t.Fatalf("C discovery: %+v %v", caps, err)
			}
		})
	}
	for _, caps := range []domain.Capabilities{
		{Model: "pico-c", ModelFamily: "pico-c"},
		{Model: "pico-c", ModelFamily: "pico-s-pro", DisplayVariant: "ssd1325"},
		{Model: "pico-c", ModelFamily: "pico-c", DisplayVariant: "128x64"},
		{DisplayVariant: "ssd1309"},
	} {
		if _, err := ReportedModel(caps); err == nil {
			t.Fatalf("accepted incomplete C build identity: %+v", caps)
		}
	}
	for _, model := range []string{"pico-s", "pico-pro"} {
		if _, err := ReportedModel(domain.Capabilities{Model: model, ModelFamily: "pico-s-pro"}); err != nil {
			t.Fatal(err)
		}
	}
}
