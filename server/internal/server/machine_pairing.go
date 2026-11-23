package server

import (
	"context"
	"encoding/hex"
	"errors"
	"net/http"
	"path/filepath"
	"strings"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/device"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

type machinePairing struct {
	base, nonce, claimedKey, savedID string
	expires                          time.Time
}
type registrationPairer interface {
	Request(context.Context, string) error
	Claim(context.Context, string) (string, error)
}

func (s *Server) startMachinePairing(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Base string `json:"base_url"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if s.cfg.Demo {
		s.fail(w, 403, "demo", "Simulation only connects to virtual machines")
		return
	}
	if len(in.Base) > 1024 {
		s.fail(w, 400, "connection", "Machine URL is too long")
		return
	}
	client, err := s.pairClient(in.Base)
	if err != nil {
		s.fail(w, 400, "connection", "Enter the machine’s HTTP or HTTPS address")
		return
	}
	s.deviceMu.Lock()
	defer s.deviceMu.Unlock()
	now := time.Now()
	if s.pairings == nil {
		s.pairings = make(map[string]*machinePairing)
	}
	for id, p := range s.pairings {
		if !now.Before(p.expires) {
			delete(s.pairings, id)
		}
	}
	if len(s.pairings) >= 32 {
		s.fail(w, 429, "pairing_limit", "Wait for a previous pairing request to expire")
		return
	}
	nonce := device.PairingNonce()
	if err := client.Request(r.Context(), nonce); err != nil {
		s.deviceError(w, err)
		return
	}
	id := randomID()
	s.pairings[id] = &machinePairing{base: in.Base, nonce: nonce, expires: now.Add(90 * time.Second)}
	time.AfterFunc(90*time.Second, func() {
		s.deviceMu.Lock()
		delete(s.pairings, id)
		s.deviceMu.Unlock()
	})
	s.json(w, 202, map[string]string{"pairing_id": id, "code": nonce[:8]})
}

func (s *Server) pairClient(base string) (registrationPairer, error) {
	if s.newPairingClient != nil {
		return s.newPairingClient(base)
	}
	return device.NewPairingClient(base)
}

// The USB installer publishes these two private files only after verified installation.
func (s *Server) installedCredential(id string) (key, uid string, err error) {
	raw, e := hex.DecodeString(id)
	if e != nil || len(raw) != 16 || id != strings.ToLower(id) {
		return "", "", errors.New("invalid USB job")
	}
	var job struct{ ID, Action, State string }
	if _, err := s.db.Get("usb-job", id, &job); err != nil || job.ID != id || job.Action != "install" || job.State != "complete" {
		return "", "", errors.New("USB installation is incomplete")
	}
	dir := filepath.Join(s.cfg.DataDir, "usb-jobs", id)
	k, e := privatefs.ReadFile(filepath.Join(dir, "device-key"), 66)
	if e != nil {
		return "", "", e
	}
	u, e := privatefs.ReadFile(filepath.Join(dir, "device-uid"), 34)
	if e != nil {
		return "", "", e
	}
	key = strings.TrimRight(string(k), "\r\n")
	uid = strings.TrimRight(string(u), "\r\n")
	kb, ke := hex.DecodeString(key)
	ub, ue := hex.DecodeString(uid)
	if ke != nil || ue != nil || len(kb) != 32 || len(ub) != 16 {
		return "", "", errors.New("incomplete USB result")
	}
	return key, uid, nil
}

func (s *Server) machineKey(w http.ResponseWriter, r *http.Request) {
	s.deviceMu.Lock()
	defer s.deviceMu.Unlock()
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	d.mu.RLock()
	dev := d.view.Device
	d.mu.RUnlock()
	if s.cfg.Demo || dev.Adapter != "pico-standalone" || !dev.KeyConfigured {
		s.fail(w, 409, "no_key", "This machine has no saved device API key")
		return
	}
	key, err := s.readSecret("device:" + dev.ID)
	if err != nil || key == "" {
		s.fail(w, 503, "key_unavailable", "The Hub could not read this machine’s saved key")
		return
	}
	s.json(w, 200, map[string]string{"api_key": key})
}

func (s *Server) machineWebsite(w http.ResponseWriter, r *http.Request) {
	d, ok := s.runtime(w, r)
	if !ok {
		return
	}
	d.commandMu.Lock()
	defer d.commandMu.Unlock()
	if !s.registeredDevice(d) {
		s.fail(w, 409, "device_changed", "Reopen this machine from the brewery")
		return
	}
	grant, ok := d.adapter.(interface {
		WebsiteGrant(context.Context) (string, error)
	})
	if !ok || s.cfg.Demo {
		s.fail(w, 409, "site_unavailable", "This machine has no local website sign-in")
		return
	}
	if _, err := s.verifiedStatus(r.Context(), d); err != nil {
		s.deviceError(w, err)
		return
	}
	d.mu.RLock()
	supported := d.view.Capabilities != nil && d.view.Capabilities.Supports("pairing_v1")
	d.mu.RUnlock()
	if !supported {
		s.fail(w, 409, "site_unavailable", "This firmware does not support opening its website already connected")
		return
	}
	target, err := grant.WebsiteGrant(r.Context())
	if err != nil {
		s.deviceError(w, err)
		return
	}
	s.json(w, 200, map[string]string{"url": target})
}
