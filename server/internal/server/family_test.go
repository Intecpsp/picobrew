package server

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

type familyCommandAdapter struct {
	device.Adapter
	caps domain.Capabilities
	sent []domain.Command
}

func (a *familyCommandAdapter) Discover(context.Context) (domain.Capabilities, error) {
	return a.caps, nil
}
func (a *familyCommandAdapter) Command(_ context.Context, c domain.Command) error {
	a.sent = append(a.sent, c)
	return nil
}

func TestAccessoryCalibrationConsentAndPointerCommandIdempotency(t *testing.T) {
	s, h, client := newTestServer(t)
	csrf := signIn(t, client, h.URL)
	a := &familyCommandAdapter{Adapter: device.NewSimulator("accessory", "pico-pro"), caps: domain.Capabilities{Commands: []string{"calibrate", "set-pump"}, CommandFields: map[string]domain.CommandFields{"calibrate": {Allowed: []string{"confirm"}, Required: []string{"confirm"}}, "set-pump": {Allowed: []string{"enabled"}, Required: []string{"enabled"}}}}}
	installTestDevice(t, s, "accessory", a)
	for index, input := range []map[string]any{{"command": "calibrate", "confirm": true}, {"command": "set-pump", "enabled": false, "confirm": true}} {
		headers := map[string]string{"Idempotency-Key": fmt.Sprintf("family-command-%d", index)}
		for repeat := 0; repeat < 2; repeat++ {
			status, body := request(t, client, "POST", h.URL+"/api/v1/devices/accessory/commands", csrf, input, headers)
			want := 202
			if repeat > 0 {
				want = 200
			}
			if status != want {
				t.Fatalf("command %d repeat%d: %d %s", index, repeat, status, body)
			}
		}
	}
	if len(a.sent) != 2 || a.sent[0].Confirm == nil || !*a.sent[0].Confirm || a.sent[1].Enabled == nil || *a.sent[1].Enabled || a.sent[1].Confirm != nil {
		t.Fatalf("lost confirmation/false value or duplicate send: %+v", a.sent)
	}
	status, _ := request(t, client, "POST", h.URL+"/api/v1/devices/accessory/commands", csrf, map[string]any{"command": "calibrate"}, map[string]string{"Idempotency-Key": "unconfirmed-calibration"})
	if status != 400 || len(a.sent) != 2 {
		t.Fatal("calibrated without confirmation")
	}
}

func TestNewFamiliesRequireConsistentStatusIdentity(t *testing.T) {
	for _, model := range []string{"z-series", "picoferm", "picostill"} {
		t.Run(model, func(t *testing.T) {
			caps := domain.Capabilities{Model: model, ModelFamily: model}
			if err := validateStatusModel(domain.Status{}, caps); err == nil {
				t.Fatal("missing identity accepted")
			}
			if err := validateStatusModel(domain.Status{Model: model, ModelFamily: model}, caps); err != nil {
				t.Fatal(err)
			}
			if err := validateStatusModel(domain.Status{Model: "pico-s", ModelFamily: "pico-s-pro"}, caps); err == nil {
				t.Fatal("cross-family identity accepted")
			}
		})
	}
	if !profileMatchesModel("z", "z-series") || profileMatchesModel("pico-pro", "z-series") || profileMatchesModel("z", "zymatic") {
		t.Fatal("profile alias changed hardware boundaries")
	}
}

type schemaHistoryAdapter struct {
	*historyAdapter
	read func(context.Context, uint64, int) (domain.HistoryPage, error)
}

func (a *schemaHistoryAdapter) HistoryPageWithSchema(ctx context.Context, id uint64, offset int) (domain.HistoryPage, error) {
	return a.read(ctx, id, offset)
}

func TestHistoryStoresPerSessionSchemaExportsAndReusesVerifiedPrefix(t *testing.T) {
	s, d, a := historyFixture(t, ":memory:")
	fields := []string{"elapsed_ms", "wort_f", "thermoblock_f", "target_f", "step_seconds", "phase", "controller_status", "compartment", "current_step", "flags", "device_state", "temperature_f", "pressure_kpa", "battery_v", "sensor_pressure_native"}
	metadata := map[string]domain.FieldMetadata{"sensor_pressure_native": {Label: "Native pressure", Unit: "raw", Kind: "pressure"}}
	d.view.Model = "picoferm"
	h := historyDescription(1, 4)
	h.State = "recording"
	h.Command = "start"
	h.RecipeName = ""
	h.FirstElapsedMS = 1000
	h.SampleIntervalMS = 300000
	a.catalog = func(context.Context) (domain.History, error) {
		return domain.History{Sessions: []domain.HistorySession{h}, Fields: []string{"newer_catalog_fields"}}, nil
	}
	var offsets []int
	reader := &schemaHistoryAdapter{historyAdapter: a}
	reader.read = func(_ context.Context, _ uint64, offset int) (domain.HistoryPage, error) {
		offsets = append(offsets, offset)
		result := domain.HistoryPage{Fields: fields, FieldMetadata: metadata}
		for index := offset; index < offset+2 && index < h.RecordCount; index++ {
			value := 532.125 + float64(index)
			result.Samples = append(result.Samples, domain.Sample{ElapsedMS: int64(index+1) * 1000, CurrentStep: 255, Readings: map[string]*float64{"sensor_pressure_native": &value}})
		}
		if offset+len(result.Samples) < h.RecordCount {
			next := offset + len(result.Samples)
			result.Next = &next
		}
		return result, nil
	}
	d.adapter = reader
	if err := s.pullHistory(context.Background(), d); err != nil {
		t.Fatal(err)
	}
	h.RecordCount, h.PersistedRecordCount, h.DurationMS = 6, 6, 6000
	if err := s.pullHistory(context.Background(), d); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(offsets, []int{0, 2, 4}) {
		t.Fatalf("verified prefix reread or skipped: %v", offsets)
	}
	detail, err := s.brewDetail("machine-1")
	if err != nil || len(detail.Samples) != 6 || detail.SampleIntervalMS != 300000 || !reflect.DeepEqual(detail.SampleFields, fields) || !reflect.DeepEqual(detail.FieldMetadata, metadata) {
		t.Fatalf("history schema/value loss %+v %v", detail, err)
	}
	for _, format := range []string{"json", "csv"} {
		r := httptest.NewRequest("GET", "/brews/machine-1/export?format="+format, nil)
		route := chi.NewRouteContext()
		route.URLParams.Add("id", "machine-1")
		r = r.WithContext(context.WithValue(r.Context(), chi.RouteCtxKey, route))
		w := httptest.NewRecorder()
		s.exportBrew(w, r)
		if w.Code != 200 {
			t.Fatal(w.Code, w.Body.String())
		}
		if format == "json" {
			var out domain.BrewDetail
			if json.Unmarshal(w.Body.Bytes(), &out) != nil || out.SampleIntervalMS != 300000 || out.FieldMetadata["sensor_pressure_native"].Unit != "raw" || *out.Samples[0].Readings["sensor_pressure_native"] != 532.125 {
				t.Fatal("JSON lost readings or units")
			}
		} else {
			rows, err := csv.NewReader(strings.NewReader(w.Body.String())).ReadAll()
			if err != nil || rows[0][len(rows[0])-1] != "sensor_pressure_native" || rows[1][len(rows[1])-1] != "532.125" {
				t.Fatal("CSV lost precision or named columns", rows, err)
			}
		}
	}
	// A changed retained origin invalidates the prefix even if record count grows.
	h.FirstElapsedMS = 2000
	if err := s.pullHistory(context.Background(), d); err != nil {
		t.Fatal(err)
	}
	if len(offsets) < 4 || offsets[3] != 0 {
		t.Fatal("shifted history window reused stale offset", offsets)
	}
}

func TestHistoryCadenceChangeCannotReuseAnUnverifiedPrefix(t *testing.T) {
	before := historyDescription(1, 4)
	before.State = "recording"
	before.SampleIntervalMS = 300000
	after := before
	after.RecordCount++
	after.PersistedRecordCount++
	after.DurationMS += 300000
	if !historyAppendOnly(before, after) {
		t.Fatal("unchanged cadence lost append eligibility")
	}
	after.SampleIntervalMS = 60000
	if historyAppendOnly(before, after) {
		t.Fatal("changed cadence silently relabeled an existing import")
	}
}

func TestHistoryRejectsMidDownloadSchemaChange(t *testing.T) {
	s, d, a := historyFixture(t, ":memory:")
	h := historyDescription(1, 2)
	a.catalog = func(context.Context) (domain.History, error) {
		return domain.History{Sessions: []domain.HistorySession{h}}, nil
	}
	reader := &schemaHistoryAdapter{historyAdapter: a}
	reader.read = func(_ context.Context, _ uint64, offset int) (domain.HistoryPage, error) {
		fields := []string{"one"}
		if offset > 0 {
			fields = []string{"changed"}
		}
		result := domain.HistoryPage{Fields: fields, Samples: []domain.Sample{{ElapsedMS: int64(offset + 1)}}}
		if offset == 0 {
			result.Next = historyInt(1)
		}
		return result, nil
	}
	d.adapter = reader
	if err := s.pullHistory(context.Background(), d); err == nil || !strings.Contains(err.Error(), "schema changed") {
		t.Fatalf("schema change accepted: %v", err)
	}
	rows, err := s.db.Samples("machine-1")
	if err != nil || len(rows) != 1 {
		t.Fatal("inconsistent page was stored", rows, err)
	}
}
