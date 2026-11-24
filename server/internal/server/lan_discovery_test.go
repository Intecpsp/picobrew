package server

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/netip"
	"slices"
	"strings"
	"testing"
	"time"
)

type discoveryRoundTrip func(*http.Request) (*http.Response, error)

func (call discoveryRoundTrip) RoundTrip(request *http.Request) (*http.Response, error) {
	return call(request)
}

func TestLANDiscoveryPublicIdentityByModel(t *testing.T) {
	ip := netip.MustParseAddr("192.168.4.42")
	for _, machine := range []struct{ model, family, variant string }{
		{"pico-s", "pico-s-pro", ""}, {"pico-pro", "pico-s-pro", ""},
		{"pico-c", "pico-c", "ssd1325"}, {"pico-c", "pico-c", "ssd1309"},
		{"z-series", "z-series", ""}, {"picoferm", "picoferm", ""}, {"picostill", "picostill", ""},
	} {
		t.Run(machine.model+machine.variant, func(t *testing.T) {
			client := &http.Client{Transport: discoveryRoundTrip(func(r *http.Request) (*http.Response, error) {
				if r.Method != http.MethodGet || r.URL.Path != "/api/v1/health" || r.URL.Host != ip.String() || r.Header.Get("Authorization") != "" {
					t.Fatal(r)
				}
				body := fmt.Sprintf(`{"model":%q,"model_family":%q,"display_variant":%q,"model_name":"sample","api_version":1,"mode":"standalone","firmware_version":"1.0.0"}`, machine.model, machine.family, machine.variant)
				return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(body))}, nil
			})}
			got, ok := discoverDevice(t.Context(), client, ip)
			if !ok || got.Model != machine.model || got.FirmwareVersion != "1.0.0" || got.Source != "standalone" || got.BaseURL != "http://"+ip.String() {
				t.Fatal(got, ok)
			}
		})
	}
}

func TestLANDiscoveryZymaticRequiresBrandedSite(t *testing.T) {
	ip := netip.MustParseAddr("10.0.0.9")
	for _, tt := range []struct {
		name, page string
		found      bool
	}{
		{"compact", "<title>Zymatic</title><h1>PicoBrew Zymatic</h1>", true},
		{"extended", "<title>PicoBrew Zymatic</title><h1>PicoBrew Zymatic</h1>", true},
		{"unrelated", "<title>My router</title>", false},
		{"oversized", "<title>Zymatic</title><h1>PicoBrew Zymatic</h1>" + strings.Repeat("x", 4096), false},
	} {
		t.Run(tt.name, func(t *testing.T) {
			var paths []string
			client := &http.Client{Transport: discoveryRoundTrip(func(r *http.Request) (*http.Response, error) {
				paths = append(paths, r.URL.Path)
				body := tt.page
				if r.URL.Path == "/api/v1/health" {
					body = `{"ok":true}`
				}
				return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(body))}, nil
			})}
			got, ok := discoverDevice(t.Context(), client, ip)
			if ok != tt.found || ok && got.Model != "zymatic" || !slices.Equal(paths, []string{"/api/v1/health", "/"}) {
				t.Fatal(got, ok, paths)
			}
		})
	}
}

func TestLANDiscoveryBoundedNeighbors(t *testing.T) {
	local := netip.MustParseAddr("192.168.4.25")
	lan := lanInterface{local, netip.MustParsePrefix("192.168.4.0/23")}
	blocks := lan.neighborhoods(0)
	if len(blocks) != 2 || blocks[0].String() != "192.168.4.0/24" || blocks[1].String() != "192.168.5.0/24" {
		t.Fatal(blocks)
	}
	first, second := lan.peers(blocks[0]), lan.peers(blocks[1])
	if len(first) != 254 || len(second) != 255 || slices.Contains(first, local) || first[0].String() != "192.168.4.1" || second[len(second)-1].String() != "192.168.5.254" {
		t.Fatal(first, second)
	}
	large := lanInterface{local, netip.MustParsePrefix("192.168.4.0/22")}
	distinct := map[netip.Prefix]bool{}
	for attempt := range uint32(3) {
		blocks := large.neighborhoods(attempt)
		if len(blocks) != 2 || !large.network.Contains(blocks[1].Addr()) || blocks[1] == blocks[0] {
			t.Fatal(blocks)
		}
		distinct[blocks[1]] = true
	}
	if len(distinct) != 3 {
		t.Fatal(distinct)
	}
	tiny := lanInterface{local, netip.MustParsePrefix("192.168.4.24/31")}
	peers := tiny.peers(tiny.neighborhoods(0)[0])
	if len(peers) != 1 || peers[0].String() != "192.168.4.24" {
		t.Fatal(peers)
	}
	broad := lanInterface{local, netip.MustParsePrefix("192.0.0.0/8")}
	if peers := broad.peers(netip.MustParsePrefix("192.167.4.0/24")); len(peers) != 0 {
		t.Fatal("overbroad interface included public addresses", peers)
	}
}

func TestLANDiscoveryFixedDestination(t *testing.T) {
	ip := netip.MustParseAddr("192.168.7.34")
	client := newDiscoveryClient(ip)
	var requests []string
	client.Transport = discoveryRoundTrip(func(r *http.Request) (*http.Response, error) {
		requests = append(requests, r.URL.String())
		headers := http.Header{}
		headers.Set("Location", "http://example.invalid/another-service")
		return &http.Response{StatusCode: http.StatusMovedPermanently, Header: headers, Body: io.NopCloser(strings.NewReader(""))}, nil
	})
	_, ok := discoverDevice(t.Context(), client, ip)
	if ok || !slices.Equal(requests, []string{"http://192.168.7.34/api/v1/health"}) {
		t.Fatal(ok, requests)
	}
	if discoveryAddressKey("http://192.168.7.34:80/") != discoveryAddressKey("http://192.168.7.34") ||
		discoveryAddressKey("http://192.168.7.34:8080") == discoveryAddressKey("http://192.168.7.34") {
		t.Fatal("default port deduplication")
	}
}

func TestLANDiscoveryRequiresOwnerAndReportsRecentLegacySerial(t *testing.T) {
	s, h, c := newTestServer(t)
	path := h.URL + "/api/v1/devices/discover"
	status, _ := request(t, c, "GET", path, "", nil, nil)
	if status != 401 {
		t.Fatal(status)
	}
	signIn(t, c, h.URL)
	status, _ = request(t, c, "GET", path+"?network=10.0.0.0/8", "", nil, nil)
	if status != 400 {
		t.Fatal(status)
	}
	// Serve the completed scan to the handler without accessing the test host's LAN.
	done := make(chan struct{})
	close(done)
	s.discovery.current = &discoveryRun{done: done, finished: time.Now(), result: LANDiscovery{Partial: true}}
	status, _ = request(t, c, "GET", h.URL+"/API/usersetup?machine=ZY99", "", nil, nil)
	if status != 200 {
		t.Fatal(status)
	}
	status, body := request(t, c, "GET", path, "", nil, nil)
	var result LANDiscovery
	if status != 200 || json.Unmarshal(body, &result) != nil || !result.Partial || len(result.Candidates) != 1 {
		t.Fatal(status, string(body))
	}
	candidate := result.Candidates[0]
	if candidate.Source != "zymatic-sync" || candidate.Serial != "ZY99" || candidate.BaseURL != "" || len(s.fleet()) != 0 {
		t.Fatal(candidate)
	}
}

func TestLANDiscoveryStopsWhenDeviceIsUnreachable(t *testing.T) {
	calls := 0
	client := &http.Client{Transport: discoveryRoundTrip(func(*http.Request) (*http.Response, error) {
		calls++
		return nil, context.DeadlineExceeded
	})}
	_, ok := discoverDevice(t.Context(), client, netip.MustParseAddr("192.168.9.14"))
	if ok || calls != 1 {
		t.Fatal(ok, calls)
	}
}
