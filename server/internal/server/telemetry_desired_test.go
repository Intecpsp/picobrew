package server

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func savedReceiverRequest(t *testing.T, s *Server, method string, target telemetryTarget) domain.TelemetryStatus {
	t.Helper()
	body := ""
	if method == http.MethodPut {
		data, err := json.Marshal(target.settings())
		if err != nil {
			t.Fatal(err)
		}
		body = string(data)
	}
	w := httptest.NewRecorder()
	s.machineTelemetry(w, identityRequest(method, "/api/v1/devices/registered/telemetry", body))
	if w.Code != http.StatusOK {
		t.Fatalf("receiver %s: %d %s", method, w.Code, w.Body.String())
	}
	var result domain.TelemetryStatus
	if err := json.Unmarshal(w.Body.Bytes(), &result); err != nil {
		t.Fatal(err)
	}
	var fields map[string]any
	_ = json.Unmarshal(w.Body.Bytes(), &fields)
	for _, name := range []string{"enabled", "host", "port", "state", "message"} {
		if _, exists := fields[name]; !exists {
			t.Fatalf("saved settings omitted %s: %s", name, w.Body.String())
		}
	}
	if _, exists := fields["mode"]; exists {
		t.Fatal("receiver mode remains in the API response")
	}
	return result
}

func statusTarget(status domain.TelemetryStatus) telemetryTarget {
	return telemetryTarget{Enabled: status.Enabled, Host: status.Host, Port: status.Port}
}

func TestReceiverSaveOfflinePersistsAndAppliesOnReconnect(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	target := telemetryTarget{Enabled: true, Host: "custom.example", Port: 9123}
	s.telemetryClient = &http.Client{Transport: receiverTransport(func(*http.Request) (*http.Response, error) {
		t.Error("saved custom target was replaced by inference")
		return nil, errors.New("unexpected network")
	})}
	result := savedReceiverRequest(t, s, http.MethodPut, target)
	if result.State != "pending" || !statusTarget(result).sameConfig(target) || a.statusCalls != 0 || a.reads != 0 || a.writes != 0 {
		t.Fatalf("offline save touched the machine or lost settings: %+v calls=%d/%d/%d", result, a.statusCalls, a.reads, a.writes)
	}
	record, err := s.readTelemetryRecord(d.view.ID)
	if err != nil || record.Desired == nil || !record.Desired.sameConfig(target) || !record.Apply {
		t.Fatalf("desired settings were not durable: %+v %v", record, err)
	}
	got := savedReceiverRequest(t, s, http.MethodGet, telemetryTarget{})
	if !statusTarget(got).sameConfig(target) || a.statusCalls != 0 {
		t.Fatal("offline GET depended on machine state")
	}
	s.refresh(context.Background(), d)
	if a.writes != 1 || !a.target.sameConfig(target) || d.view.Telemetry.State != "configured" {
		t.Fatalf("reconnect failed: %+v writes=%d", d.view.Telemetry, a.writes)
	}
}

func TestReceiverSavePersistsBeforeIOAndDefersWhileBusy(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	pairedRefresh(t, s, d)
	target := telemetryTarget{Enabled: true, Host: "custom.example", Port: 9123}
	writes := a.writes
	checked := false
	a.status.Busy = true
	a.onStatus = func() {
		record, err := s.readTelemetryRecord(d.view.ID)
		if err != nil || record.Desired == nil || !record.Desired.sameConfig(target) {
			t.Error("machine I/O preceded durable desired settings")
		}
		checked = true
	}
	result := savedReceiverRequest(t, s, http.MethodPut, target)
	if !checked || result.State != "waiting_idle" || a.writes != writes || !statusTarget(result).sameConfig(target) {
		t.Fatalf("busy save: %+v writes=%d", result, a.writes)
	}
	a.onStatus = nil
	a.status.Busy = false
	pairedRefresh(t, s, d)
	if a.writes != writes+1 || !a.target.sameConfig(target) {
		t.Fatal("queued setting did not apply once idle")
	}
	pairedRefresh(t, s, d)
	if a.writes != writes+1 {
		t.Fatal("unchanged receiver was rewritten")
	}
}

func TestSavedDisabledReceiverSurvivesReconnectAndRuntimeRestart(t *testing.T) {
	for _, host := range []string{"", "custom.example"} {
		t.Run(map[bool]string{true: "empty host", false: "custom host"}[host == ""], func(t *testing.T) {
			s, d, a := pairingFixture(t, "")
			pairedRefresh(t, s, d)
			target := telemetryTarget{Enabled: false, Host: host, Port: 9123}
			result := savedReceiverRequest(t, s, http.MethodPut, target)
			if result.State != "configured" || a.target.Enabled || !a.target.sameConfig(target) {
				t.Fatal("disabled desired setting was not applied")
			}
			a.target = telemetryTarget{Enabled: true, Host: "other.example", Port: 8080}
			writes := a.writes
			a.statusErr = errors.New("disconnected")
			s.refresh(context.Background(), d)
			a.statusErr = nil
			s.refresh(context.Background(), d)
			if a.writes != writes+1 || !a.target.sameConfig(target) {
				t.Fatal("reconnect enabled or replaced the saved disabled setting")
			}
			a.target = telemetryTarget{Enabled: true, Host: "other.example", Port: 8080}
			next := &runtimeDevice{adapter: a, cancel: func() {}, identityMismatch: true, view: domain.DeviceView{Device: d.view.Device}}
			s.devices[d.view.ID] = next
			s.initTelemetry(next)
			s.refresh(context.Background(), next)
			if a.writes != writes+2 || !a.target.sameConfig(target) || next.view.Telemetry.Enabled {
				t.Fatal("Hub restart lost disabled desired value")
			}
			got := savedReceiverRequest(t, s, http.MethodGet, telemetryTarget{})
			if !statusTarget(got).sameConfig(target) {
				t.Fatal("GET returned observed/default settings instead of desired")
			}
		})
	}
}

func TestReceiverReappliesOnOfflineRecoveryAndUptimeRollback(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	a.status.UptimeMS = 10000
	pairedRefresh(t, s, d)
	target := a.target
	a.target = telemetryTarget{Enabled: true, Host: "other.example", Port: 8123}
	pairedRefresh(t, s, d)
	if a.writes != 1 || d.view.Telemetry.State != "pending" {
		t.Fatal("ordinary polling fought the changed receiver")
	}
	d.telemetryNext = time.Now().Add(time.Hour)
	a.statusErr = errors.New("network interrupted")
	s.refresh(context.Background(), d)
	a.statusErr = nil
	a.status.UptimeMS = 12000
	s.refresh(context.Background(), d)
	if a.writes != 2 || !a.target.sameConfig(target) {
		t.Fatal("genuine reconnect did not bypass old throttle and reapply")
	}
	a.target = telemetryTarget{Enabled: true, Host: "other.example", Port: 8123}
	d.telemetryNext = time.Now().Add(time.Hour)
	a.status.UptimeMS = 10
	s.refresh(context.Background(), d)
	if a.writes != 3 || !a.target.sameConfig(target) {
		t.Fatal("reboot between successful polls did not reapply")
	}
	pairedRefresh(t, s, d)
	if a.writes != 3 {
		t.Fatal("steady polling rewrote matching settings")
	}
}

func TestReceiverSaveOnWrongUIDQueuesOnlyForOriginalMachine(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	pairedRefresh(t, s, d)
	writes, reads := a.writes, a.reads
	a.status.DeviceUID = strings.Repeat("b", 32)
	target := telemetryTarget{Enabled: true, Host: "custom.example", Port: 9123}
	result := savedReceiverRequest(t, s, http.MethodPut, target)
	if result.State != "pending" || a.writes != writes || a.reads != reads || !d.identityMismatch || d.telemetryReconnect {
		t.Fatal("replacement UID received receiver I/O")
	}
	record, err := s.readTelemetryRecord(d.view.ID)
	if err != nil || record.Desired == nil || !record.Desired.sameConfig(target) {
		t.Fatal("desired setting was lost while original machine was unavailable")
	}
	a.status.DeviceUID = strings.Repeat("a", 32)
	s.refresh(context.Background(), d)
	if a.writes != writes+1 || !a.target.sameConfig(target) {
		t.Fatal("original machine did not receive queued settings")
	}
}

func TestReceiverDefaultIsVerifiedAndSavedOnlyOnce(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	first := savedReceiverRequest(t, s, http.MethodGet, telemetryTarget{})
	target := statusTarget(first)
	if !target.Enabled || target.Host != "192.0.2.20" || target.Port != 8080 || a.statusCalls != 0 {
		t.Fatal("offline prefill did not save verified Hub address")
	}
	s.cfg.DeviceURL = "http://192.0.2.99:8123"
	s.telemetryClient = &http.Client{Transport: receiverTransport(func(*http.Request) (*http.Response, error) {
		t.Error("resolved default was inferred again")
		return nil, errors.New("not expected")
	})}
	s.requestTelemetryPairing(d, pairingRequest())
	s.refresh(context.Background(), d)
	got := savedReceiverRequest(t, s, http.MethodGet, telemetryTarget{})
	if !statusTarget(got).sameConfig(target) || !a.target.sameConfig(target) {
		t.Fatal("reconnect changed the saved default receiver")
	}
}

func TestLegacyReceiverMigrationPreservesDesiredValuesAndCleanupOutcomes(t *testing.T) {
	hub := telemetryTarget{Enabled: true, Host: "192.0.2.20", Port: 8080}
	cleanup := hub
	cleanup.Enabled = false
	disabled := telemetryTarget{Enabled: false, Host: "custom.example", Port: 9123}
	blankDisabled := telemetryTarget{Enabled: false, Port: 8123}
	custom := disabled
	custom.Enabled = true
	for _, tc := range []struct {
		name   string
		legacy telemetryRecord
		want   telemetryTarget
	}{
		{"manual custom", telemetryRecord{Mode: "manual", Manual: custom, Owned: hub}, custom},
		{"manual disabled", telemetryRecord{Mode: "manual", Manual: disabled, Owned: hub}, disabled},
		{"manual blank disabled", telemetryRecord{Mode: "manual", Manual: blankDisabled}, blankDisabled},
		{"resolved automatic", telemetryRecord{Mode: "automatic", Owned: hub}, hub},
		{"automatic cleanup confirmed", telemetryRecord{Mode: "automatic", Owned: cleanup}, hub},
		{"automatic cleanup uncertain", telemetryRecord{Mode: "automatic", Owned: hub, Pending: &cleanup}, hub},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, d, a := pairingFixture(t, "")
			if _, err := s.db.Put("device-telemetry", d.view.ID, tc.legacy, -1); err != nil {
				t.Fatal(err)
			}
			record, err := s.readTelemetryRecord(d.view.ID)
			if err != nil || record.Desired == nil || !record.Desired.sameConfig(tc.want) || record.Mode != "" || record.SchemaVersion != 2 {
				t.Fatalf("migration %+v err=%v", record, err)
			}
			if tc.legacy.Pending != nil && (record.Pending == nil || !record.Pending.sameConfig(*tc.legacy.Pending)) {
				t.Fatal("migration erased ambiguous cleanup outcome")
			}
			var stored telemetryRecord
			version, _ := s.db.Get("device-telemetry", d.view.ID, &stored)
			_, _ = s.readTelemetryRecord(d.view.ID)
			again, _ := s.db.Get("device-telemetry", d.view.ID, &stored)
			if version != again {
				t.Fatal("migration rewrote unchanged data on every read")
			}
			if strings.Contains(tc.name, "cleanup") {
				a.target = cleanup
				s.initTelemetry(d)
				s.refresh(context.Background(), d)
				if !a.target.sameConfig(hub) || a.writes != 1 {
					t.Fatal("legacy monitoring cleanup prevented re-enable")
				}
			}
		})
	}
}

func TestAmbiguousReceiverChangeSurvivesReconnectWithoutBlindRetry(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	a.applyWrite = false
	a.writeErr = &device.Error{Code: "transport_error", Message: "reply lost", UnknownOutcome: true}
	pairedRefresh(t, s, d)
	if a.writes != 1 || d.view.Telemetry.State != "outcome_unknown" {
		t.Fatal("expected one unconfirmed change")
	}
	a.statusErr = errors.New("disconnected")
	s.refresh(context.Background(), d)
	a.statusErr = nil
	s.refresh(context.Background(), d)
	if a.writes != 1 || d.view.Telemetry.State != "outcome_unknown" {
		t.Fatal("reconnect blindly retried ambiguous change")
	}
	record, err := s.readTelemetryRecord(d.view.ID)
	if err != nil || record.Pending == nil || record.Desired == nil {
		t.Fatal("unconfirmed write lost durable evidence")
	}
	a.applyWrite = true
	a.writeErr = nil
	target := telemetryTarget{Enabled: false, Host: "custom.example", Port: 9123}
	result := savedReceiverRequest(t, s, http.MethodPut, target)
	if result.State != "configured" || a.writes != 2 || !a.target.sameConfig(target) {
		t.Fatal("explicit saved replacement did not receive a checked retry")
	}
}

func TestInvalidReceiverSaveDoesNotChangeDesiredOrContactMachine(t *testing.T) {
	s, d, a := pairingFixture(t, "")
	target := telemetryTarget{Enabled: false, Host: "custom.example", Port: 9123}
	savedReceiverRequest(t, s, http.MethodPut, target)
	beforeCalls, beforeWrites := a.statusCalls, a.writes
	for _, body := range []string{
		"{\"enabled\":true,\"host\":\"\",\"port\":8080}",
		"{\"enabled\":true,\"host\":\"http://other.example\",\"port\":8080}",
		"{\"enabled\":false,\"host\":\"custom.example\",\"port\":0}",
		"{\"enabled\":false,\"host\":\"custom.example\"}",
	} {
		w := httptest.NewRecorder()
		s.machineTelemetry(w, identityRequest(http.MethodPut, "/api/v1/devices/registered/telemetry", body))
		if w.Code != 400 {
			t.Fatalf("invalid setting accepted: %d %s", w.Code, w.Body.String())
		}
		record, err := s.readTelemetryRecord(d.view.ID)
		if err != nil || record.Desired == nil || !record.Desired.sameConfig(target) || a.statusCalls != beforeCalls || a.writes != beforeWrites {
			t.Fatal("invalid request changed saved state or contacted machine")
		}
	}
}
