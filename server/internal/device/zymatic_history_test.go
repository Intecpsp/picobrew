package device

import (
	"context"
	"net/http"
	"reflect"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestZymaticHistoryDiscoversAndPagesOnlyAdvertisedChannels(t *testing.T) {
	fields := []string{"elapsed_ms", "wort_f", "thermoblock_f", "target_f", "compartment", "current_step"}
	p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.RequestURI() {
		case "/api/v1":
			replyJSON(w, 200, map[string]any{"mode": "standalone", "api_version": 1, "model": "zymatic", "model_family": "zymatic", "capabilities": map[string]bool{"history_persistent": true}})
		case "/api/v1/history":
			replyJSON(w, 200, map[string]any{"sessions": []any{map[string]any{"id": 42, "record_count": 3, "persisted_record_count": 3, "sample_interval_ms": 60000, "available": true}}, "fields": fields})
		case "/api/v1/history/samples/0?id=42":
			replyJSON(w, 200, map[string]any{"id": 42, "offset": 0, "next_offset": 2, "samples": [][]any{{0, nil, nil, nil, nil, nil}, {60000, 65.1, 149.2, 153, 0, 0}}})
		case "/api/v1/history/samples/2?id=42":
			replyJSON(w, 200, map[string]any{"id": 42, "offset": 2, "next_offset": nil, "samples": [][]any{{72000, 145.6, 154.1, 155, 5, 24}}})
		default:
			t.Errorf("unexpected request %s", r.URL)
			w.WriteHeader(404)
		}
	})
	ctx := context.Background()
	if _, err := p.History(ctx); err == nil {
		t.Fatal("accepted a compact schema without verified Zymatic capabilities")
	}
	if _, err := p.Discover(ctx); err != nil {
		t.Fatal(err)
	}
	catalog, err := p.History(ctx)
	if err != nil || len(catalog.Sessions) != 1 || catalog.Sessions[0].ID != 42 {
		t.Fatalf("catalog: %+v, %v", catalog, err)
	}
	page, err := p.HistoryPageWithSchema(ctx, 42, 0)
	if err != nil || page.Next == nil || *page.Next != 2 || len(page.Samples) != 2 || !reflect.DeepEqual(page.Fields, fields) {
		t.Fatalf("page: %+v, %v", page, err)
	}
	first, second := page.Samples[0], page.Samples[1]
	if !first.ZymaticHistory || first.Compartment != -1 || first.CurrentStep != -1 || first.WortF != nil || second.Compartment != 0 || second.CurrentStep != 0 || *second.WortF != 65.1 {
		t.Fatal("lost nulls, zeros or temperatures", page)
	}
	last, err := p.HistoryPageWithSchema(ctx, 42, *page.Next)
	if err != nil || last.Next != nil || len(last.Samples) != 1 || last.Samples[0].Compartment != 5 || last.Samples[0].CurrentStep != 24 {
		t.Fatal(last, err)
	}
	for _, caps := range []*domain.Capabilities{{Model: "pico-s", HistoryPersistent: true}, {Model: "zymatic", ModelFamily: "zymatic"}, {Model: "zymatic", ModelFamily: "pico", HistoryPersistent: true}} {
		p.capabilities = caps
		if _, err := p.History(ctx); err == nil {
			t.Fatalf("accepted compact schema with %+v", caps)
		}
	}
}

func TestZymaticHistoryRejectsInvalidFieldsAndSamples(t *testing.T) {
	fields := []string{"elapsed_ms", "wort_f", "thermoblock_f", "target_f", "compartment", "current_step"}
	for _, tc := range []struct {
		name string
		row  []any
	}{
		{"invalid time", []any{nil, 72, 73, 150, 0, 1}},
		{"fractional time", []any{1.5, 72, 73, 150, 0, 1}},
		{"out of band temperature", []any{60000, 72, 401, 150, 1, 2}},
		{"target overflow", []any{60000, 72, 73, 256, 1, 2}},
		{"unknown route", []any{60000, 72, 73, 150, 7, 2}},
		{"nonexistent step", []any{60000, 72, 73, 150, 3, 25}},
		{"fractional step", []any{60000, 72, 73, 150, 3, 1.5}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
				replyJSON(w, 200, map[string]any{"id": 1, "offset": 0, "fields": fields, "samples": [][]any{tc.row}})
			})
			p.capabilities = &domain.Capabilities{Model: "zymatic", ModelFamily: "zymatic", HistoryPersistent: true}
			if _, err := p.HistoryPageWithSchema(context.Background(), 1, 0); err == nil {
				t.Fatal("accepted malformed sample")
			}
		})
	}
	p := pico{capabilities: &domain.Capabilities{Model: "zymatic", ModelFamily: "zymatic", HistoryPersistent: true}}
	if p.validHistoryFields(historyFields) || p.validHistoryFields(append(fields, "flags")) || p.validHistoryFields(fields[:4]) {
		t.Fatal("accepted undeclared history fields")
	}
}
