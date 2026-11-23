package server

import (
	"errors"
	"net"
	"net/http"
	"strconv"
	"strings"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/usbservice"
)

func (s *Server) usbError(w http.ResponseWriter, err error) {
	var code int
	switch {
	case errors.Is(err, usbservice.ErrInput):
		code = 400
	case errors.Is(err, usbservice.ErrMissing):
		code = 404
	case errors.Is(err, usbservice.ErrPrompt), errors.Is(err, usbservice.ErrBusy):
		code = 409
	case errors.Is(err, usbservice.ErrUnavailable):
		code = 503
	default:
		s.internal(w, err)
		return
	}
	s.fail(w, code, "usb_service", err.Error())
}
func (s *Server) usbDevices(w http.ResponseWriter, r *http.Request) {
	if s.cfg.Demo {
		s.json(w, 200, usbservice.USBDevices{Devices: []usbservice.USBDevice{}, Message: "USB setup requires a physical Hub."})
		return
	}
	s.json(w, 200, s.usb.Devices(r.Context()))
}
func (s *Server) usbStart(w http.ResponseWriter, r *http.Request) {
	if s.cfg.Demo {
		s.fail(w, 409, "demo_device", "USB setup requires a physical Hub.")
		return
	}
	var in usbservice.USBInput
	if !s.decode(w, r, &in) {
		return
	}
	var uid string
	if in.DeviceID != "" {
		var d domain.Device
		if _, err := s.db.Get("device", in.DeviceID, &d); err != nil {
			if errors.Is(err, store.ErrNotFound) {
				s.fail(w, 404, "device_not_found", "Machine not found.")
				return
			}
			s.internal(w, err)
			return
		}
		var pin deviceIdentity
		if _, err := s.db.Get("device-identity", in.DeviceID, &pin); err != nil && !errors.Is(err, store.ErrNotFound) {
			s.internal(w, err)
			return
		}
		if pin.UID == "" || d.Adapter != "pico-standalone" || (in.Model != "" && d.Model != in.Model) {
			s.fail(w, 409, "device_identity", "USB service for a registered machine requires its verified model and device identity.")
			return
		}
		s.mu.RLock()
		rt := s.devices[in.DeviceID]
		s.mu.RUnlock()
		if rt != nil {
			rt.mu.RLock()
			inUse := rt.updatingFirmware || rt.view.Online && rt.view.Status != nil && rt.view.Status.Busy
			rt.mu.RUnlock()
			if inUse {
				s.fail(w, 409, "device_busy", "Finish the active machine operation before USB service.")
				return
			}
		}
		uid = pin.UID
		in.Model = d.Model
	}
	var host string
	if in.Action == "install" && in.Model == "zymatic" {
		origin := s.cfg.DeviceURL
		if origin == "" {
			origin = requestReceiverOrigin(r)
		}
		if target, err := parseReceiverURL(origin); err == nil && s.probeReceiver(r.Context(), target) {
			host = net.JoinHostPort(target.Host, strconv.Itoa(target.Port))
		}
	}
	j, err := s.usb.Start(in, uid, host)
	if err != nil {
		s.usbError(w, err)
		return
	}
	s.json(w, 202, j)
}
func (s *Server) usbJob(w http.ResponseWriter, r *http.Request) {
	j, err := s.usb.Get(chi.URLParam(r, "id"))
	if err != nil {
		s.usbError(w, err)
		return
	}
	s.json(w, 200, j)
}
func (s *Server) usbAnswer(w http.ResponseWriter, r *http.Request) {
	var in usbservice.USBAnswer
	if !s.decode(w, r, &in) {
		return
	}
	j, err := s.usb.Answer(chi.URLParam(r, "id"), in.Confirm, strings.HasSuffix(r.URL.Path, "/confirm"))
	if err != nil {
		s.usbError(w, err)
		return
	}
	s.json(w, 200, j)
}
