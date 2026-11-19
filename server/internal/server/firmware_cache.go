package server

import (
	"errors"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

var errFirmwareCacheLocked = errors.New("firmware cache is locked")
var firmwareStageName = regexp.MustCompile(`^\.incoming-[0-9a-f]{32}$`)

// lockFirmwareCache serializes the short filesystem/metadata publication
// window across processes. It never covers a network request and never waits
// for another process. Closing the handle also releases the OS lock on crash.
func (s *Server) lockFirmwareCache() (*os.File, error) {
	if err := s.ensureFirmwareCache(); err != nil {
		return nil, err
	}
	file, err := privatefs.OpenFile(filepath.Join(s.firmwareCacheDir(), ".cache.lock"), os.O_CREATE|os.O_RDWR)
	if err != nil {
		return nil, err
	}
	if err := tryLockFirmwareCache(file); err != nil {
		_ = file.Close()
		if errors.Is(err, errFirmwareCacheLocked) {
			return nil, &firmware.Error{Code: "firmware_busy", Message: "Another Hub is updating the firmware cache. Try again shortly."}
		}
		return nil, err
	}
	return file, nil
}

// recoverFirmwareCache runs only while holding both the cache mutex and the
// OS lock. Recognized files without committed metadata belong to interrupted
// publications; no other cooperating writer can have a live staging file.
// Unrecognized names and existing package records are never removed here.
func (s *Server) recoverFirmwareCache() error {
	packages, err := s.listFirmwarePackages()
	if err != nil {
		return err
	}
	saved := make(map[string]bool, len(packages))
	for _, p := range packages {
		saved[p.ID] = true
	}
	entries, err := os.ReadDir(s.firmwareCacheDir())
	if err != nil {
		return err
	}
	for _, entry := range entries {
		name := entry.Name()
		orphan := strings.HasSuffix(name, ".zip") && firmware.ValidID(strings.TrimSuffix(name, ".zip")) && !saved[strings.TrimSuffix(name, ".zip")]
		if !firmwareStageName.MatchString(name) && !orphan {
			continue
		}
		if entry.IsDir() {
			return cacheCorrupt()
		}
		if err := os.Remove(filepath.Join(s.firmwareCacheDir(), name)); err != nil && !errors.Is(err, os.ErrNotExist) {
			return err
		}
	}
	return nil
}
