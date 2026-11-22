package server

import (
	"context"
	"encoding/json"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware/firmwaretest"
	"net/http/httptest"
	"strings"
	"testing"
)

func cPairingFixture(t *testing.T, display string) (*Server, *runtimeDevice, *pairingAdapter) {
	s, d, a := pairingFixture(t, "")
	a.caps.Model, a.caps.ModelName, a.caps.ModelFamily, a.caps.DisplayVariant = "pico-c", "Pico C", "pico-c", display
	a.status.Model, a.status.ModelName, a.status.ModelFamily, a.status.DisplayVariant = "pico-c", "Pico C", "pico-c", display
	return s, d, a
}

func TestCAutomaticPairingSetsUpHubWithoutHardwareConfirmation(t *testing.T) {
	for _, display := range []string{"ssd1325", "ssd1309"} {
		t.Run(display, func(t *testing.T) {
			s, d, a := cPairingFixture(t, display)
			pairedRefresh(t, s, d)
			if d.view.Model != "pico-c" || d.view.ModelSource != "firmware" || d.view.ModelSelectionRequired || !s.currentDevice(d) {
				t.Fatalf("C pairing did not identify: %+v", d.view)
			}
			if a.writes != 1 || !a.target.Enabled || a.target.Host != "192.0.2.20" || a.target.Port != 8080 {
				t.Fatalf("C telemetry was not paired: %+v writes=%d", a.target, a.writes)
			}
			if verified, err := s.verifiedFirmwareDisplay(d.view.ID, a.status.DeviceUID); err != nil || verified != "" {
				t.Fatalf("compiled display was mistaken for hardware verification: %q %v", verified, err)
			}
			var saved domain.Device
			if _, err := s.db.Get("device", d.view.ID, &saved); err != nil || saved.Model != "pico-c" {
				t.Fatalf("C identity not saved: %+v %v", saved, err)
			}
			alternate := "ssd1309"
			if display == alternate {
				alternate = "ssd1325"
			}
			a.status.DisplayVariant = alternate
			if _, err := s.verifiedStatus(context.Background(), d); err == nil || s.currentDevice(d) {
				t.Fatal("conflicting C status/discovery variants permitted control")
			}
		})
	}
}

func TestCDisplayInventoryIsIndependentOfWirelessFirmwareSupport(t *testing.T) {
	s, d, a := cPairingFixture(t, "ssd1309")
	pairedRefresh(t, s, d)
	for _, display := range []string{"ssd1325", "ssd1309"} {
		raw := firmwaretest.Archive(firmwaretest.CFiles(display))
		metadata, err := firmware.ValidateArchive(raw)
		if err != nil {
			t.Fatal(err)
		}
		if _, _, err := s.saveFirmwarePackage(raw, metadata, firmware.Provenance{Source: "upload"}); err != nil {
			t.Fatal(err)
		}
	}
	read := func() firmware.MachineFirmware {
		t.Helper()
		w := httptest.NewRecorder()
		s.machineFirmware(w, identityRequest("GET", "/api/v1/devices/registered/firmware", ""))
		var got firmware.MachineFirmware
		if err := json.Unmarshal(w.Body.Bytes(), &got); err != nil || w.Code != 200 {
			t.Fatalf("C firmware view %d %s: %v", w.Code, w.Body, err)
		}
		if got.InstallSupported {
			t.Fatal("package management advertised installation")
		}
		return got
	}
	got := read()
	if got.Model != "pico-c" || got.DisplayVariant != "ssd1309" || !got.HardwareVerificationRequired || len(got.Packages) != 0 {
		t.Fatalf("unverified C applicability: %+v", got)
	}
	verify := func(uid, display string) int {
		t.Helper()
		w := httptest.NewRecorder()
		body := string(firmwaretest.JSON(map[string]string{"device_uid": uid, "display_variant": display}))
		s.verifyFirmwareHardware(w, identityRequest("PUT", "/api/v1/devices/registered/firmware/hardware", body))
		return w.Code
	}
	writes := a.writes
	if code := verify(a.status.DeviceUID, "ssd1309"); code != 200 {
		t.Fatalf("verification status %d", code)
	}
	got = read()
	if got.HardwareVerificationRequired || got.VerifiedDisplayVariant != "ssd1309" || len(got.Packages) != 0 || !got.StockRestoreGuide {
		t.Fatalf("verified C inventory and wireless eligibility: %+v", got)
	}
	if a.writes != writes || a.commands != 0 {
		t.Fatal("hardware inventory wrote to the machine")
	}
	if code := verify(strings.Repeat("b", 32), "ssd1325"); code != 409 {
		t.Fatalf("stale UID verification status %d", code)
	}
	if code := verify(a.status.DeviceUID, "128x32"); code != 400 {
		t.Fatalf("inferred display accepted: %d", code)
	}
	if code := verify(a.status.DeviceUID, "ssd1325"); code != 200 {
		t.Fatalf("independent controller inventory rejected: %d", code)
	}
	got = read()
	if !got.HardwareVerificationRequired || got.VerifiedDisplayVariant != "ssd1325" || len(got.Packages) != 0 {
		t.Fatalf("contradictory hardware and running firmware matched: %+v", got)
	}
	if verified, err := s.verifiedFirmwareDisplay(d.view.ID, strings.Repeat("b", 32)); err != nil || verified != "" {
		t.Fatal("hardware record transferred to another UID")
	}
	records, err := s.listFirmwarePackages()
	if err != nil || len(records) != 2 {
		t.Fatal("verification affected ordinary package management")
	}
}
