package server

import (
	"context"
	"errors"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

type releaseNotice struct {
	DeviceID string `json:"device_id"`
	Name     string `json:"name"`
	Version  string `json:"version"`
}
type releaseView struct {
	Checked  *time.Time         `json:"checked,omitempty"`
	Releases []firmware.Release `json:"releases"`
	Notices  []releaseNotice    `json:"notices"`
}

func (s *Server) savedCatalog() (firmware.ReleaseCatalog, error) {
	var catalog firmware.ReleaseCatalog
	s.configMu.Lock()
	defer s.configMu.Unlock()
	cfg, err := s.readFirmwareSettings()
	if err != nil {
		return catalog, err
	}
	_, err = s.db.Get("config", "firmware-releases", &catalog)
	if errors.Is(err, store.ErrNotFound) || (err == nil && catalog.Settings != cfg) {
		return firmware.ReleaseCatalog{Settings: cfg}, nil
	}
	return catalog, err
}
func (s *Server) refreshCatalog(ctx context.Context) (firmware.ReleaseCatalog, error) {
	s.releaseMu.Lock()
	defer s.releaseMu.Unlock()
	catalog, err := s.savedCatalog()
	if err != nil {
		return catalog, err
	}
	next, err := s.firmwareGitHub.Releases(ctx, catalog.Settings)
	if err != nil {
		return catalog, err
	}
	s.configMu.Lock()
	defer s.configMu.Unlock()
	current, err := s.readFirmwareSettings()
	if err != nil {
		return catalog, err
	}
	if current != next.Settings {
		return catalog, sourceChanged()
	}
	_, err = s.db.Put("config", "firmware-releases", next, -1)
	return next, err
}
func (s *Server) fleetForReleases() []domain.DeviceView {
	s.mu.RLock()
	defer s.mu.RUnlock()
	var out []domain.DeviceView
	for _, d := range s.devices {
		d.mu.RLock()
		if d.view.Adapter == "pico-standalone" && !d.identityMismatch && d.view.ModelError == "" {
			out = append(out, d.view)
		}
		d.mu.RUnlock()
	}
	return out
}
func releaseMatches(r firmware.Release, v domain.DeviceView) bool {
	if r.Model == "pico-s-pro" {
		return v.Model == "pico-s" || v.Model == "pico-pro"
	}
	if r.Model != v.Model {
		return false
	}
	return r.Model != "pico-c" || v.Capabilities != nil && r.DisplayVariant == v.Capabilities.DisplayVariant
}

// Only compare numbered production releases for update notifications.
func newerRelease(a, b string) bool {
	left, right := strings.Split(a, "."), strings.Split(b, ".")
	if len(left) != 3 || len(right) != 3 {
		return false
	}
	cmp := 0
	for i := range left {
		x, e := strconv.ParseUint(left[i], 10, 32)
		y, f := strconv.ParseUint(right[i], 10, 32)
		if e != nil || f != nil {
			return false
		}
		if cmp == 0 {
			if x > y {
				cmp = 1
			} else if x < y {
				cmp = -1
			}
		}
	}
	return cmp > 0
}
func (s *Server) monitorReleases(ctx context.Context) {
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()
	var retryAfter time.Time
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-ticker.C:
			if now.Before(retryAfter) || len(s.fleetForReleases()) == 0 {
				continue
			}
			catalog, err := s.savedCatalog()
			if err == nil && now.Sub(catalog.Checked) >= 24*time.Hour {
				_, err = s.refreshCatalog(ctx)
			}
			if err != nil && ctx.Err() == nil {
				s.cfg.Logger.Warn("Firmware version check failed", "error", err)
				retryAfter = now.Add(time.Hour)
			}
		}
	}
}
func (s *Server) firmwareUpdates(w http.ResponseWriter, r *http.Request) {
	c, err := s.savedCatalog()
	if err == nil && r.Method == http.MethodPost && !s.cfg.Demo {
		c, err = s.refreshCatalog(r.Context())
	}
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	out := releaseView{Releases: []firmware.Release{}, Notices: []releaseNotice{}}
	if !c.Checked.IsZero() {
		out.Checked = &c.Checked
	}
	seen := map[string]bool{}
	for _, d := range s.fleetForReleases() {
		latest := ""
		for _, v := range c.Releases {
			if !releaseMatches(v, d) {
				continue
			}
			if !seen[v.ArchiveSHA256] {
				out.Releases = append(out.Releases, v)
				seen[v.ArchiveSHA256] = true
			}
			if v.Kind != "historical" && d.Capabilities != nil && newerRelease(v.FirmwareVersion, d.Capabilities.FirmwareVersion) && (latest == "" || newerRelease(v.FirmwareVersion, latest)) {
				latest = v.FirmwareVersion
			}
		}
		if latest != "" {
			out.Notices = append(out.Notices, releaseNotice{d.ID, d.Name, latest})
		}
	}
	s.json(w, 200, out)
}
func (s *Server) stageRelease(w http.ResponseWriter, r *http.Request) {
	if !s.beginFirmwareWork(w) {
		return
	}
	defer s.firmwareWorkMu.Unlock()
	c, err := s.savedCatalog()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	id := chi.URLParam(r, "packageID")
	var selected *firmware.Release
	for _, release := range c.Releases {
		if release.ArchiveSHA256 == id {
			selected = &release
			break
		}
	}
	if selected == nil {
		s.fail(w, 404, "unknown_release", "Refresh available firmware versions and select the release again")
		return
	}
	var saved firmware.Package
	s.firmwareMu.Lock()
	_, err = s.db.Get(firmwareRecordKind, id, &saved)
	if err == nil {
		_, err = s.readFirmwareArchive(id)
	}
	s.firmwareMu.Unlock()
	if err == nil {
		s.json(w, 200, saved)
		return
	}
	if !errors.Is(err, store.ErrNotFound) {
		s.firmwareError(w, err)
		return
	}
	data, err := s.firmwareGitHub.DownloadRelease(r.Context(), c, *selected)
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	s.configMu.Lock()
	defer s.configMu.Unlock()
	cfg, err := s.readFirmwareSettings()
	if err == nil && c.Settings != cfg {
		err = sourceChanged()
	}
	if err == nil {
		saved, _, err = s.saveFirmwarePackage(data, selected.Candidate, firmware.Provenance{Source: "library", Repository: c.Repository, Commit: c.Commit})
	}
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	s.json(w, 200, saved)
}
