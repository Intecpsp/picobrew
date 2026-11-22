package stockfirmware

import (
	"bytes"
	"context"
	"crypto/sha256"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

func fixture(name, body string) image {
	return image{name: name, sha256: fmt.Sprintf("%x", sha256.Sum256([]byte(body))), size: int64(len(body)), blob: name}
}

type transport func(*http.Request) (*http.Response, error)

func (fn transport) RoundTrip(r *http.Request) (*http.Response, error) { return fn(r) }

func TestPreferPrivateOriginalAndRecoverMissingPublicSlots(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private")
	if err := privatefs.EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	backup := fixture("backup.bin", "private")
	slots := []image{fixture("mcuimg1.bin", "one"), fixture("mcuimg2.bin", "two")}
	sourcePath := filepath.Join(dir, "capture")
	if err := os.WriteFile(sourcePath, []byte("private"), 0600); err != nil {
		t.Fatal(err)
	}
	saved, err := saveBackup(dir, sourcePath, backup)
	if err != nil {
		t.Fatal(err)
	}
	again, err := saveBackup(dir, sourcePath, backup)
	if again != saved || err != nil {
		t.Fatal(again, err)
	}
	var calls []string
	secondResponse := "bad"
	client := &firmware.GitHub{HTTP: &http.Client{Transport: transport(func(req *http.Request) (*http.Response, error) {
		if req.Method != "GET" || req.URL.Host != "api.github.com" || req.URL.RawQuery != "" || req.Body != nil ||
			req.Header.Get("Accept") != "application/vnd.github.raw+json" {
			t.Fatal("unexpected request for OEM data")
		}
		name := filepath.Base(req.URL.Path)
		if req.URL.Path != "/repos/"+firmware.DefaultRepository+"/git/blobs/"+name {
			t.Fatal("unexpected repository path", req.URL.Path)
		}
		data := map[string]string{slots[0].blob: "one", slots[1].blob: secondResponse}[name]
		calls = append(calls, name)
		return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(data))}, nil
	})}}
	resolveSource := func() (Source, error) {
		return resolve(context.Background(), dir, client, backup, slots)
	}
	got, err := resolveSource()
	if err != nil || got.Backup != saved || got.OEMDirectory != "" || len(calls) > 0 {
		t.Fatal("did not prefer the private backup", got, err, calls)
	}
	if err := os.Remove(saved); err != nil {
		t.Fatal(err)
	}
	if got, err = resolveSource(); !errors.Is(err, errIntegrity) || got != (Source{}) {
		t.Fatal("partial/corrupt download accepted", got, err)
	}
	secondResponse = "two"
	got, err = resolveSource()
	if err != nil || got.OEMDirectory == "" || got.Backup != "" || !slices.Equal(calls, []string{"mcuimg1.bin", "mcuimg2.bin", "mcuimg2.bin"}) {
		t.Fatal("failed to resume missing slots", got, err, calls)
	}
	if err := os.WriteFile(saved, []byte("damaged backup"), 0600); err != nil {
		t.Fatal(err)
	}
	got, err = resolveSource()
	if err != nil || got.Backup != "" || len(calls) != 3 || !strings.Contains(got.Description, "failed verification") {
		t.Fatal("cached public slots were not reused", got, err, calls)
	}
	if _, err := saveBackup(dir, sourcePath, backup); !errors.Is(err, errIntegrity) {
		t.Fatal("overwrote an existing damaged backup", err)
	}
	data, err := os.ReadFile(saved)
	if err != nil || !bytes.Equal(data, []byte("damaged backup")) {
		t.Fatal("did not preserve the owner's original file")
	}
	// Re-download only a corrupted public image, and still reject an oversized body.
	if err = os.WriteFile(filepath.Join(got.OEMDirectory, slots[1].name), []byte("bad"), 0600); err != nil {
		t.Fatal(err)
	}
	secondResponse = "two-too-long"
	if _, err = resolveSource(); !errors.Is(err, errIntegrity) {
		t.Fatal("accepted oversized public image", err)
	}
	secondResponse = "two"
	if _, err = resolveSource(); err != nil || len(calls) != 5 {
		t.Fatal("could not repair corrupted public slot", err, calls)
	}
}

func TestRejectUnqualifiedBackupBeforeCaching(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private")
	if err := privatefs.EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	source := filepath.Join(dir, "different.bin")
	if err := os.WriteFile(source, []byte("other brewer"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := SaveBackup(dir, source); !errors.Is(err, errIntegrity) {
		t.Fatal(err)
	}
	if _, err := StoredBackup(dir); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("unexpected saved original", err)
	}
}
