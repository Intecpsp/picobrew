package server

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestZymaticJournalRetainsNullableRouteAndExportsRecordedColumns(t *testing.T) {
	s, d, a := historyFixture(t, ":memory:")
	d.view.Model = "zymatic"
	h := historyDescription(3, 2)
	h.State, h.SampleIntervalMS = "interrupted", 60000
	h.RecoveredAfterRestart = true
	fields := []string{"elapsed_ms", "wort_f", "thermoblock_f", "target_f", "compartment", "current_step"}
	a.catalog = func(context.Context) (domain.History, error) {
		return domain.History{Sessions: []domain.HistorySession{h}, Fields: fields}, nil
	}
	tail := 146.2
	rows := []domain.Sample{
		{ZymaticHistory: true, ElapsedMS: 0, Compartment: -1, CurrentStep: -1},
		{ZymaticHistory: true, ElapsedMS: 60000, WortF: &tail, Compartment: 0, CurrentStep: 0},
	}
	d.adapter = &schemaHistoryAdapter{historyAdapter: a, read: func(_ context.Context, id uint64, offset int) (domain.HistoryPage, error) {
		page := domain.HistoryPage{Fields: fields, Samples: rows[offset : offset+1]}
		if offset == 0 {
			page.Next = historyInt(1)
		}
		return page, nil
	}}
	if err := s.pullHistory(context.Background(), d); err != nil {
		t.Fatal(err)
	}
	detail, err := s.brewDetail("machine-3")
	if err != nil || len(detail.Samples) != 2 || !reflect.DeepEqual(detail.SampleFields, fields) || detail.State != "interrupted" {
		t.Fatalf("round trip: %+v, %v", detail, err)
	}
	for _, format := range []string{"csv", "json"} {
		r := httptest.NewRequest("GET", "/brews/machine-3/export?format="+format, nil)
		route := chi.NewRouteContext()
		route.URLParams.Add("id", "machine-3")
		w := httptest.NewRecorder()
		s.exportBrew(w, r.WithContext(context.WithValue(r.Context(), chi.RouteCtxKey, route)))
		if w.Code != 200 {
			t.Fatal(w.Code, w.Body.String())
		}
		if format == "csv" {
			lines, err := csv.NewReader(strings.NewReader(w.Body.String())).ReadAll()
			want := [][]string{{"elapsed_ms", "observed_at", "wort_f", "thermoblock_f", "target_f", "compartment", "current_step", "source"}, {"0", "", "", "", "", "", "", "device"}, {"60000", "", "146.20", "", "", "0", "0", "device"}}
			if err != nil || !reflect.DeepEqual(lines, want) {
				t.Fatalf("CSV %v: %+v", err, lines)
			}
			continue
		}
		var doc struct {
			Samples []map[string]any `json:"samples"`
		}
		if err := json.Unmarshal(w.Body.Bytes(), &doc); err != nil || len(doc.Samples) != 2 {
			t.Fatal(doc, err)
		}
		for _, row := range doc.Samples {
			for _, key := range []string{"phase", "flags", "device_state", "step_seconds", "controller_status"} {
				if _, exists := row[key]; exists {
					t.Fatalf("invented %s: %+v", key, row)
				}
			}
		}
		for _, key := range []string{"compartment", "current_step"} {
			if value, exists := doc.Samples[0][key]; !exists || value != nil || doc.Samples[1][key] != float64(0) {
				t.Fatalf("lost null or zero for %s: %s", key, w.Body.String())
			}
		}
	}
}
