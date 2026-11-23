package server

import (
	"net/http"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
)

func (s *Server) deviceEvents(w http.ResponseWriter, r *http.Request) {
	d, ok := s.runtime(w, r)
	if !ok {
		return
	}
	reader, ok := d.adapter.(device.EventReader)
	if !ok {
		s.fail(w, http.StatusNotFound, "device_unsupported_capability", "This machine does not expose a device event log")
		return
	}
	d.commandMu.Lock()
	defer d.commandMu.Unlock()
	if !s.currentDevice(d) {
		s.fail(w, http.StatusConflict, "connection_changed", "The machine connection changed. Reopen the event log.")
		return
	}
	if _, err := s.verifiedStatus(r.Context(), d); err != nil {
		s.deviceError(w, err)
		return
	}
	events, err := reader.Events(r.Context())
	if err != nil {
		s.deviceError(w, err)
		return
	}
	s.json(w, http.StatusOK, events)
}
