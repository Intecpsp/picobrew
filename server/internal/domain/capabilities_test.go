package domain

import (
	"encoding/json"
	"testing"
)

func TestPairingDiscoveryUsesFirmwareFieldAndHonorsExplicitOverride(t *testing.T) {
	for _, test := range []struct {
		discovery string
		want      bool
	}{
		{`{"model":"pico-s","front_panel":true,"pairing_v1":true}`, true},
		{`{"model":"zymatic","front_panel":true,"pairing_v1":true,"capabilities":{"pairing_v1":false}}`, false},
		{`{"capabilities":{"pairing_v1":true}}`, true},
		{`{"front_panel":true}`, false},
	} {
		var discovery Capabilities
		if err := json.Unmarshal([]byte(test.discovery), &discovery); err != nil {
			t.Fatal(err)
		}
		if got := discovery.Supports("pairing_v1"); got != test.want {
			t.Errorf("Supports(pairing_v1) for %s = %v, want %v", test.discovery, got, test.want)
		}
	}
}
