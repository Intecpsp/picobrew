package main

import "testing"

func TestListenDefaultsPermitMachineCallbacksAndKeepDemoLocal(t *testing.T) {
	for _, tc := range []struct {
		name, address, want      string
		demo, explicit, rejected bool
	}{
		{"authenticated default", "0.0.0.0:8080", "0.0.0.0:8080", false, false, false},
		{"demo default", "0.0.0.0:8080", "127.0.0.1:8080", true, false, false},
		{"explicit local authenticated", "127.0.0.1:9000", "127.0.0.1:9000", false, true, false},
		{"explicit local demo", "[::1]:9000", "[::1]:9000", true, true, false},
		{"network demo rejected", "0.0.0.0:8080", "", true, true, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got, err := selectListenAddress(tc.address, tc.demo, tc.explicit)
			if got != tc.want || (err != nil) != tc.rejected {
				t.Fatalf("got=%q err=%v", got, err)
			}
		})
	}
}
