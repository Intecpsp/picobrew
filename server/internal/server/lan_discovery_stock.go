package server

import (
	"context"
	"net/http"
	"net/netip"
	"regexp"
	"strings"
)

var stockStillVersion = regexp.MustCompile(`^#([0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3})#$`)
var stockFermSetupIP = netip.AddrFrom4([4]byte{192, 168, 4, 1})
var stockFermSetupHeader = regexp.MustCompile(`<h1>PicoFerm<sup>Beta</sup></h1>\s*<small>\s*Product ID:\s*[0-9a-fA-F]{12}<br>Version:\s*([0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3})\s*</small>\s*<h3>Wi-Fi Setup</h3>`)

func recognizeStockDevice(ctx context.Context, client *http.Client, ip netip.Addr) (DiscoveryCandidate, bool) {
	address := "http://" + ip.String()
	// The stock PicoFerm setup AP places the product ID and version after
	// 4,272 bytes of static HTML. Read just through the setup heading.
	limit := 2048
	if ip == stockFermSetupIP {
		limit = 4400
	}
	data, _ := discoveryResponse(ctx, client, address, "/", limit, true)
	page := string(data)
	switch {
	case ip == stockFermSetupIP && strings.Contains(page, "<title>PicoFerm</title>"):
		if match := stockFermSetupHeader.FindStringSubmatch(page); match != nil {
			return DiscoveryCandidate{Name: "PicoFerm Wi-Fi setup", Model: "picoferm", Source: "stock",
				BaseURL: address, FirmwareVersion: match[1]}, true
		}
	case strings.Contains(page, "<title>Z Program Maker</title>") &&
		strings.Contains(page, `onclick="pushToZ()">PUSH TO Z`) && strings.Contains(page, `id="z-pak"`):
		// The 0.0.119 Program Maker has a read-only ping endpoint. Its
		// response identifies the service but carries no firmware version.
		if string(readDiscoveryPage(ctx, client, address, "/ping")) == "PONG!" {
			return DiscoveryCandidate{Name: discoveryLabel("z-series"), Model: "z-series", Source: "stock", BaseURL: address}, true
		}
	case strings.Contains(page, "<title>PicoStill</title>") &&
		strings.Contains(page, "--pb-green-normal: #93D402;") && strings.Contains(page, "--pb-green-dark: #425F00;"):
		// The OEM PicoStill frames the device's reported version with '#'.
		match := stockStillVersion.FindSubmatch(readDiscoveryPage(ctx, client, address, "/version"))
		if match != nil {
			return DiscoveryCandidate{Name: discoveryLabel("picostill"), Model: "picostill", Source: "stock",
				BaseURL: address, FirmwareVersion: string(match[1])}, true
		}
	}
	return DiscoveryCandidate{}, false
}
