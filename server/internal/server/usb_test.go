package server

import (
	"net/http"
	"testing"
)

func TestUSBJobsRequireOwnerCSRFAndMachineIdentity(t *testing.T) {
	_, h, c := newTestServer(t)
	status, _ := request(t, c, "GET", h.URL+"/api/v1/usb/devices", "", nil, nil)
	if status != 401 {
		t.Fatal(status)
	}
	csrf := signIn(t, c, h.URL)
	input := map[string]string{"action": "restore", "model": "pico-s", "port": "serial-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "device_id": "unknown-machine"}
	status, _ = request(t, c, "POST", h.URL+"/api/v1/usb/jobs", "", input, nil)
	if status != 403 {
		t.Fatal(status)
	}
	status, _ = request(t, c, "POST", h.URL+"/api/v1/usb/jobs", csrf, input, nil)
	if status != 404 {
		t.Fatal(status)
	}
	delete(input, "device_id")
	input["data_dir"] = "/elsewhere"
	status, _ = request(t, c, "POST", h.URL+"/api/v1/usb/jobs", csrf, input, nil)
	if status != http.StatusBadRequest {
		t.Fatal(status)
	}
	status, _ = request(t, c, "POST", h.URL+"/api/v1/usb/jobs/no-such-job/confirm", csrf, map[string]string{"confirm": "expired"}, nil)
	if status != 409 {
		t.Fatal(status)
	}
}
