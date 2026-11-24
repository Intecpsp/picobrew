package server

import (
	"context"
	"io"
	"net/http"
	"net/netip"
	"slices"
	"strings"
	"testing"
)

func TestRecognizeStockDevice(t *testing.T) {
	// Fragments from the stock Z Program Maker and PicoStill Wi-Fi page.
	const z = `<title>Z Program Maker</title><button onclick="pushToZ()">PUSH TO Z</button><input id="z-pak" type="checkbox">`
	const still = `<title>PicoStill</title><style>:root {--pb-green-normal: #93D402; --pb-green-dark: #425F00;}</style>`
	ip := netip.MustParseAddr("192.0.2.40")
	for _, tc := range []struct {
		name, html, next, reply, model, version string
	}{
		{"stock Z", z, "/ping", "PONG!", "z-series", ""},
		{"stock Still", still + strings.Repeat(" ", 16<<10), "/version", "#0.0.35#", "picostill", "0.0.35"},
		{"other Still revision reports its own version", still, "/version", "#0.0.31#", "picostill", "0.0.31"},
		{"Z page title alone", "<title>Z Program Maker</title>", "", "", "", ""},
		{"Still page title alone", "<title>PicoStill</title>", "", "", "", ""},
		{"mirrored Z page without stock service", z, "/ping", "pong", "", ""},
		{"invalid Still version", still, "/version", "0.0.35", "", ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var paths []string
			client := &http.Client{Transport: receiverTransport(func(req *http.Request) (*http.Response, error) {
				if req.Method != http.MethodGet || req.URL.Host != ip.String() || req.URL.RawQuery != "" {
					t.Fatalf("unexpected request: %s %s", req.Method, req.URL)
				}
				paths = append(paths, req.URL.Path)
				if req.URL.Path == "/api/v1/health" {
					return &http.Response{StatusCode: http.StatusNotFound, Body: http.NoBody}, nil
				}
				body := tc.html
				if req.URL.Path != "/" {
					if tc.next == "" || tc.next != req.URL.Path {
						t.Fatalf("unexpected route %q", req.URL.Path)
					}
					body = tc.reply
				}
				return &http.Response{StatusCode: http.StatusOK, Body: io.NopCloser(strings.NewReader(body))}, nil
			})}
			got, ok := discoverDevice(context.Background(), client, ip)
			wantPaths := []string{"/api/v1/health", "/"}
			if tc.next != "" {
				wantPaths = append(wantPaths, tc.next)
			}
			if !slices.Equal(paths, wantPaths) || ok != (tc.model != "") || got.Model != tc.model || got.FirmwareVersion != tc.version {
				t.Fatalf("paths %v, got %+v, match %v", paths, got, ok)
			}
			if ok && (got.Source != "stock" || got.BaseURL != "http://"+ip.String()) {
				t.Fatalf("unexpected source or website: %+v", got)
			}
		})
	}
}

func TestRecognizeStockPicoFermSetup(t *testing.T) {
	const header = "<h1>PicoFerm<sup>Beta</sup></h1>\n    <small>\n      Product ID: 001122aabbcc<br>Version: \r\n0.2.7\n    </small>\n\r\n<h3>Wi-Fi Setup</h3>"
	setupPage := "<html><head><title>PicoFerm</title>" + strings.Repeat(" ", 4110) + header
	for _, tc := range []struct {
		name, ip, page string
		match          bool
	}{
		{"original setup AP", "192.168.4.1", setupPage + strings.Repeat(" ", 4096), true},
		{"same website outside setup AP", "192.168.4.12", setupPage, false},
		{"setup AP with unrelated captive portal", "192.168.4.1", "<title>PicoFerm</title><h3>Wi-Fi Setup</h3>", false},
		{"missing OEM product ID", "192.168.4.1", strings.Replace(setupPage, "001122aabbcc", "", 1), false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var paths []string
			client := &http.Client{Transport: receiverTransport(func(req *http.Request) (*http.Response, error) {
				if req.Method != http.MethodGet || req.URL.Host != tc.ip {
					t.Fatalf("unexpected request: %s %s", req.Method, req.URL)
				}
				paths = append(paths, req.URL.RequestURI())
				switch req.URL.RequestURI() {
				case "/api/v1/health": // OEM 0.2.7 returns 404 for unrecognized paths.
					return &http.Response{StatusCode: http.StatusNotFound, Body: http.NoBody}, nil
				case "/":
					return &http.Response{StatusCode: http.StatusOK, Body: io.NopCloser(strings.NewReader(tc.page))}, nil
				default:
					t.Fatalf("unexpected route %q", req.URL.Path)
					return nil, nil
				}
			})}
			got, ok := discoverDevice(context.Background(), client, netip.MustParseAddr(tc.ip))
			if ok != tc.match || !slices.Equal(paths, []string{"/api/v1/health", "/"}) {
				t.Fatalf("paths %v, got %+v, match %v", paths, got, ok)
			}
			if ok && (got.Model != "picoferm" || got.Name != "PicoFerm Wi-Fi setup" || got.Source != "stock" || got.FirmwareVersion != "0.2.7" || got.BaseURL != "http://"+tc.ip) {
				t.Fatalf("unexpected setup portal: %+v", got)
			}
		})
	}
}
