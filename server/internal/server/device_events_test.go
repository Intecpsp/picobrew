package server

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

type fixtureEventReader struct {
	*pairingAdapter
	reads int
}

func (a *fixtureEventReader) Events(context.Context) (domain.DeviceEvents, error) {
	a.reads++
	return domain.DeviceEvents{Events: []domain.DeviceEvent{}, Capacity: 8, Volatile: true, NextSequence: 1}, nil
}

func TestDeviceEventsRouteRequiresSession(t *testing.T) {
	_, h, c := newTestServer(t)
	status, _ := request(t, c, "GET", h.URL+"/api/v1/devices/registered/events", "", nil, nil)
	if status != http.StatusUnauthorized {
		t.Fatalf("unauthenticated GET status %d", status)
	}
	signIn(t, c, h.URL)
	status, _ = request(t, c, "GET", h.URL+"/api/v1/devices/registered/events", "", nil, nil)
	if status != http.StatusNotFound {
		t.Fatalf("unknown device status %d", status)
	}
}
func TestDeviceEventsChecksIdentityBeforeEveryRead(t *testing.T) {
	s, d, a := pairingFixture(t, "pico-s")
	pairedRefresh(t, s, d)
	read := func() *httptest.ResponseRecorder {
		w := httptest.NewRecorder()
		s.deviceEvents(w, identityRequest("GET", "/api/v1/devices/registered/events", ""))
		return w
	}
	if w := read(); w.Code != http.StatusNotFound || !strings.Contains(w.Body.String(), "unsupported_capability") {
		t.Fatal(w.Code, w.Body)
	}
	reader := &fixtureEventReader{pairingAdapter: a}
	d.adapter = reader
	if w := read(); w.Code != http.StatusOK || !strings.Contains(w.Body.String(), `"events":[]`) || reader.reads != 1 {
		t.Fatal(w.Code, w.Body, reader.reads)
	}
	a.status.DeviceUID = strings.Repeat("b", 32)
	if w := read(); w.Code == http.StatusOK || !strings.Contains(w.Body.String(), "identity") || reader.reads != 1 {
		t.Fatal(w.Code, w.Body, reader.reads)
	}
}
