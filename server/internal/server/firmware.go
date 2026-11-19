package server

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

const firmwareRecordKind = "firmware-package"

func (s *Server) firmwareError(w http.ResponseWriter, err error) {
	if errors.Is(err, context.Canceled) {
		s.fail(w, 499, "cancelled", "The firmware request was cancelled")
		return
	}
	if errors.Is(err, context.DeadlineExceeded) {
		s.fail(w, 504, "firmware_timeout", "The firmware request timed out. Try again.")
		return
	}
	var failure *firmware.Error
	if errors.As(err, &failure) {
		status := http.StatusBadRequest
		switch failure.Code {
		case "firmware_package_invalid":
			status = http.StatusUnprocessableEntity
		case "firmware_github_not_found":
			status = http.StatusNotFound
		case "firmware_github_rate_limit", "firmware_busy":
			status = http.StatusTooManyRequests
		case "firmware_github_unavailable", "firmware_github_size":
			status = http.StatusBadGateway
		case "firmware_source_changed", "firmware_cache_full", "firmware_cache_corrupt":
			status = http.StatusConflict
		}
		s.fail(w, status, failure.Code, failure.Message)
		return
	}
	s.storeError(w, err)
}

// A single acquisition at a time bounds memory and temporary storage. Other
// authenticated requests fail promptly instead of queuing unlimited uploads.
func (s *Server) beginFirmwareWork(w http.ResponseWriter) bool {
	if !s.firmwareWorkMu.TryLock() {
		w.Header().Set("Retry-After", "1")
		s.fail(w, 429, "firmware_busy", "Another firmware upload or GitHub request is in progress. Try again shortly.")
		return false
	}
	return true
}

// The caller holds configMu. Loading settings never contacts GitHub.
func (s *Server) readFirmwareSettings() (firmware.Settings, error) {
	cfg := firmware.Settings{Repository: firmware.DefaultRepository}
	if _, err := s.db.Get("config", "firmware", &cfg); err != nil && !errors.Is(err, store.ErrNotFound) {
		return cfg, err
	}
	return firmware.NormalizeSettings(cfg)
}

func (s *Server) firmwareSettings(w http.ResponseWriter, r *http.Request) {
	s.configMu.Lock()
	defer s.configMu.Unlock()
	if r.Method == http.MethodPut {
		var in firmware.Settings
		if !s.decode(w, r, &in) {
			return
		}
		cfg, err := firmware.NormalizeSettings(in)
		if err != nil {
			s.firmwareError(w, err)
			return
		}
		if _, err := s.db.Put("config", "firmware", cfg, -1); err != nil {
			s.internal(w, err)
			return
		}
		s.json(w, 200, cfg)
		return
	}
	cfg, err := s.readFirmwareSettings()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	s.json(w, 200, cfg)
}

func sourceChanged() error {
	return &firmware.Error{Code: "firmware_source_changed", Message: "The saved GitHub source changed. Check that source again before importing."}
}

func (s *Server) firmwareGitHubCatalog(w http.ResponseWriter, r *http.Request) {
	if !s.beginFirmwareWork(w) {
		return
	}
	defer s.firmwareWorkMu.Unlock()
	s.configMu.Lock()
	cfg, err := s.readFirmwareSettings()
	s.configMu.Unlock()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	catalog, err := s.firmwareGitHub.Catalog(r.Context(), cfg)
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	s.configMu.Lock()
	current, err := s.readFirmwareSettings()
	s.configMu.Unlock()
	if err == nil && current != cfg {
		err = sourceChanged()
	}
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	s.json(w, 200, catalog)
}

func (s *Server) firmwareGitHubImport(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Repository string `json:"repository"`
		Commit     string `json:"commit"`
		Model      string `json:"model"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	repository, err := firmware.NormalizeRepository(in.Repository)
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	if !firmware.ValidCommit(in.Commit) || !firmware.ValidModel(in.Model) {
		s.fail(w, 400, "firmware_github_selection", "Choose a supported model and a full 40-character GitHub commit")
		return
	}
	if !s.beginFirmwareWork(w) {
		return
	}
	defer s.firmwareWorkMu.Unlock()
	s.configMu.Lock()
	cfg, err := s.readFirmwareSettings()
	s.configMu.Unlock()
	if err == nil && cfg.Repository != repository {
		err = sourceChanged()
	}
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	archive, metadata, err := s.firmwareGitHub.Import(r.Context(), repository, in.Commit, in.Model)
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	// Recheck immediately before publishing, under the same lock as settings
	// writes. A source edit during a download cannot publish a stale selection.
	s.configMu.Lock()
	current, err := s.readFirmwareSettings()
	if err == nil && current != cfg {
		err = sourceChanged()
	}
	var saved firmware.Package
	var created bool
	if err == nil {
		if err = r.Context().Err(); err == nil {
			saved, created, err = s.saveFirmwarePackage(archive, metadata, firmware.Provenance{
				Source: "github", Repository: repository, Commit: in.Commit,
			})
		}
	}
	s.configMu.Unlock()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	code := http.StatusOK
	if created {
		code = http.StatusCreated
	}
	s.json(w, code, saved)
}

func (s *Server) firmwarePackages(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodPost {
		s.uploadFirmwarePackage(w, r)
		return
	}
	s.firmwareMu.Lock()
	packages, err := s.listFirmwarePackages()
	s.firmwareMu.Unlock()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	s.json(w, 200, packages)
}

func (s *Server) uploadFirmwarePackage(w http.ResponseWriter, r *http.Request) {
	if !s.beginFirmwareWork(w) {
		return
	}
	defer s.firmwareWorkMu.Unlock()
	contentType, _, err := mime.ParseMediaType(r.Header.Get("Content-Type"))
	if err != nil || contentType != "multipart/form-data" {
		s.fail(w, 415, "content_type", "Upload a firmware ZIP as multipart/form-data using the file field")
		return
	}
	// The archive is limited separately; this small allowance covers multipart
	// framing, not additional fields or additional files.
	r.Body = http.MaxBytesReader(w, r.Body, firmware.MaxArchiveBytes+(64<<10))
	reader, err := r.MultipartReader()
	if err != nil {
		s.fail(w, 400, "firmware_upload", "The firmware upload is not valid multipart data")
		return
	}
	part, err := reader.NextPart()
	if err != nil || part.FormName() != "file" || part.FileName() == "" {
		s.fail(w, 400, "firmware_upload", "Upload one ZIP file using the file field")
		return
	}
	archive, readErr := io.ReadAll(io.LimitReader(part, firmware.MaxArchiveBytes+1))
	closeErr := part.Close()
	var tooLarge *http.MaxBytesError
	if len(archive) > firmware.MaxArchiveBytes || errors.As(readErr, &tooLarge) || errors.As(closeErr, &tooLarge) {
		s.fail(w, 413, "firmware_upload_size", "The firmware ZIP must be no larger than 2 MiB")
		return
	}
	if readErr != nil || closeErr != nil {
		s.fail(w, 400, "firmware_upload", "The firmware upload was incomplete")
		return
	}
	if next, err := reader.NextPart(); err != io.EOF {
		if next != nil {
			_ = next.Close()
		}
		if errors.As(err, &tooLarge) {
			s.fail(w, 413, "firmware_upload_size", "The firmware upload exceeds its size limit")
		} else {
			s.fail(w, 400, "firmware_upload", "Upload exactly one file without additional fields")
		}
		return
	}
	metadata, err := firmware.ValidateArchive(archive)
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	if err := r.Context().Err(); err != nil {
		s.firmwareError(w, err)
		return
	}
	saved, created, err := s.saveFirmwarePackage(archive, metadata, firmware.Provenance{Source: "upload"})
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	code := http.StatusOK
	if created {
		code = http.StatusCreated
	}
	s.json(w, code, saved)
}

func cacheCorrupt() error {
	return &firmware.Error{Code: "firmware_cache_corrupt", Message: "The saved firmware file is missing or has changed. Remove this package and import it again."}
}

func cacheFull() error {
	return &firmware.Error{Code: "firmware_cache_full", Message: "The firmware cache holds at most 20 packages. Remove a saved package before adding another."}
}

func (s *Server) firmwareCacheDir() string {
	return filepath.Join(s.cfg.DataDir, "firmware")
}

func (s *Server) ensureFirmwareCache() error {
	dir := s.firmwareCacheDir()
	if err := privatefs.EnsureDir(dir); err != nil {
		return err
	}
	info, err := os.Lstat(dir)
	if err != nil {
		return err
	}
	if !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return cacheCorrupt()
	}
	return nil
}

// The cache lock covers metadata and bytes together within this Hub process.
func (s *Server) listFirmwarePackages() ([]firmware.Package, error) {
	rows, err := s.db.List(firmwareRecordKind)
	if err != nil {
		return nil, err
	}
	out := make([]firmware.Package, 0, len(rows))
	for _, row := range rows {
		var p firmware.Package
		if err := json.Unmarshal(row, &p); err != nil {
			return nil, err
		}
		if !firmware.ValidID(p.ID) || p.Validation != "passed" {
			return nil, cacheCorrupt()
		}
		out = append(out, p)
	}
	slices.SortFunc(out, func(a, b firmware.Package) int {
		if a.CreatedAt.After(b.CreatedAt) {
			return -1
		}
		if a.CreatedAt.Before(b.CreatedAt) {
			return 1
		}
		return strings.Compare(a.ID, b.ID)
	})
	return out, nil
}

func (s *Server) readFirmwareArchive(id string) ([]byte, error) {
	if !firmware.ValidID(id) {
		return nil, store.ErrNotFound
	}
	if err := s.ensureFirmwareCache(); err != nil {
		return nil, err
	}
	data, err := privatefs.ReadFile(filepath.Join(s.firmwareCacheDir(), id+".zip"), firmware.MaxArchiveBytes)
	if err != nil || len(data) > firmware.MaxArchiveBytes || firmware.Digest(data) != id {
		return nil, cacheCorrupt()
	}
	return data, nil
}

func (s *Server) saveFirmwarePackage(archive []byte, metadata firmware.Candidate, provenance firmware.Provenance) (firmware.Package, bool, error) {
	s.firmwareMu.Lock()
	defer s.firmwareMu.Unlock()
	var existing firmware.Package
	lock, err := s.lockFirmwareCache()
	if err != nil {
		return existing, false, err
	}
	defer lock.Close()
	if err := s.recoverFirmwareCache(); err != nil {
		return existing, false, err
	}
	id := firmware.Digest(archive)
	if _, err := s.db.Get(firmwareRecordKind, id, &existing); err == nil {
		if _, err := s.readFirmwareArchive(id); err != nil {
			return existing, false, err
		}
		// Duplicate acquisition never upgrades the original source's claims.
		return existing, false, nil
	} else if !errors.Is(err, store.ErrNotFound) {
		return existing, false, err
	}
	packages, err := s.listFirmwarePackages()
	if err != nil {
		return existing, false, err
	}
	if len(packages) >= firmware.MaxPackages {
		return existing, false, cacheFull()
	}
	if err := s.ensureFirmwareCache(); err != nil {
		return existing, false, err
	}
	dir := s.firmwareCacheDir()
	destination := filepath.Join(dir, id+".zip")
	files, err := os.ReadDir(dir)
	if err != nil {
		return existing, false, err
	}
	count := 0
	var total int64
	for _, entry := range files {
		info, err := entry.Info()
		if err != nil || !info.Mode().IsRegular() {
			return existing, false, cacheCorrupt()
		}
		// Include interrupted staged/orphan files in the disk bound.
		total += info.Size()
		if strings.HasSuffix(entry.Name(), ".zip") && entry.Name() != id+".zip" {
			count++
		}
	}
	if count >= firmware.MaxPackages || total+int64(len(archive)) > firmware.MaxPackages*firmware.MaxArchiveBytes {
		return existing, false, cacheFull()
	}
	createdFile := false
	if _, err := os.Lstat(destination); err == nil {
		if _, err := s.readFirmwareArchive(id); err != nil {
			return existing, false, err
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		return existing, false, err
	} else {
		staging := filepath.Join(dir, ".incoming-"+randomID())
		defer os.Remove(staging)
		file, err := privatefs.OpenFile(staging, os.O_WRONLY|os.O_CREATE|os.O_EXCL)
		if err != nil {
			return existing, false, err
		}
		_, err = file.Write(archive)
		if err == nil {
			err = file.Sync()
		}
		closeErr := file.Close()
		if err == nil {
			err = closeErr
		}
		if err == nil {
			err = os.Rename(staging, destination)
		}
		if err != nil {
			return existing, false, err
		}
		createdFile = true
	}
	// Read back the published bytes before announcing a validated package.
	if _, err := s.readFirmwareArchive(id); err != nil {
		return existing, false, err
	}
	p := firmware.Package{Candidate: metadata, ID: id, PackageBytes: len(archive),
		CreatedAt: time.Now().UTC(), Validation: "passed", Provenance: provenance}
	if _, err := s.db.Put(firmwareRecordKind, id, p, 0); err != nil {
		// Another Hub handle may have published the same content first.
		if errors.Is(err, store.ErrConflict) {
			if _, getErr := s.db.Get(firmwareRecordKind, id, &existing); getErr == nil {
				return existing, false, nil
			}
		}
		if createdFile {
			var check firmware.Package
			if _, getErr := s.db.Get(firmwareRecordKind, id, &check); errors.Is(getErr, store.ErrNotFound) {
				_ = os.Remove(destination)
			}
		}
		return existing, false, err
	}
	return p, true, nil
}

func (s *Server) firmwarePackage(w http.ResponseWriter, r *http.Request) {
	id := chi.URLParam(r, "packageID")
	if !firmware.ValidID(id) {
		s.storeError(w, store.ErrNotFound)
		return
	}
	s.firmwareMu.Lock()
	defer s.firmwareMu.Unlock()
	var p firmware.Package
	if _, err := s.db.Get(firmwareRecordKind, id, &p); err != nil {
		s.storeError(w, err)
		return
	}
	if r.Method == http.MethodDelete {
		lock, err := s.lockFirmwareCache()
		if err != nil {
			s.firmwareError(w, err)
			return
		}
		defer lock.Close()
		// Removing the cache entry is safe even when its bytes failed readback;
		// this is the recovery path for a missing or damaged download.
		err = os.Remove(filepath.Join(s.firmwareCacheDir(), id+".zip"))
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			s.internal(w, err)
			return
		}
		if err := s.db.Delete(firmwareRecordKind, id); err != nil {
			s.storeError(w, err)
			return
		}
		s.json(w, 200, map[string]bool{"deleted": true})
		return
	}
	s.json(w, 200, p)
}

func (s *Server) downloadFirmwarePackage(w http.ResponseWriter, r *http.Request) {
	id := chi.URLParam(r, "packageID")
	if !firmware.ValidID(id) {
		s.storeError(w, store.ErrNotFound)
		return
	}
	s.firmwareMu.Lock()
	var p firmware.Package
	_, err := s.db.Get(firmwareRecordKind, id, &p)
	var archive []byte
	if err == nil {
		archive, err = s.readFirmwareArchive(id)
	}
	s.firmwareMu.Unlock()
	if err != nil {
		s.firmwareError(w, err)
		return
	}
	w.Header().Set("Content-Type", "application/zip")
	w.Header().Set("Content-Disposition", fmt.Sprintf(`attachment; filename="picobrew-firmware-%s.zip"`, id[:12]))
	http.ServeContent(w, r, id+".zip", p.CreatedAt, bytes.NewReader(archive))
}

func (s *Server) machineFirmware(w http.ResponseWriter, r *http.Request) {
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	out := firmware.MachineFirmware{
		Packages: []firmware.Package{},
		Reason:   "Network installation is not available yet. It requires an initial USB recovery setup that is still being developed.",
	}
	d.mu.RLock()
	out.DeviceID = d.view.ID
	if d.view.Adapter == "simulator" {
		out.Reason = "Simulators do not receive firmware."
	} else if !d.identityMismatch && d.view.Capabilities != nil {
		out.CurrentVersion = d.view.Capabilities.FirmwareVersion
		if d.view.ModelSource == "firmware" && d.view.ModelError == "" &&
			firmware.ValidModel(d.view.Capabilities.Model) && d.view.Status != nil {
			out.Model = d.view.Capabilities.Model
		}
	}
	d.mu.RUnlock()
	if out.Model != "" {
		s.firmwareMu.Lock()
		packages, err := s.listFirmwarePackages()
		s.firmwareMu.Unlock()
		if err != nil {
			s.firmwareError(w, err)
			return
		}
		for _, p := range packages {
			if p.Model == out.Model {
				out.Packages = append(out.Packages, p)
			}
		}
	}
	s.json(w, 200, out)
}
