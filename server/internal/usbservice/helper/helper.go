// Package helper prepares the USB worker embedded for this Hub's platform.
package helper

import (
	"bytes"
	"context"
	"crypto/sha256"
	"embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sync"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

//go:embed payload/*
var files embed.FS

type manifest struct {
	Platform string `json:"platform"`
	SHA256   string `json:"sha256"`
}

// A plain go build can also run the Hub with only the web assets. USB-enabled
// builds stage one verified native worker before compiling.
var embedded = sync.OnceValues(func() ([]byte, string) {
	var meta manifest
	metadata, err := files.ReadFile("payload/manifest.json")
	if err != nil || json.Unmarshal(metadata, &meta) != nil || meta.Platform != runtime.GOOS+"-"+runtime.GOARCH {
		return nil, ""
	}
	binary, err := files.ReadFile("payload/worker")
	sum := sha256.Sum256(binary)
	if err != nil || len(binary) == 0 || meta.SHA256 != hex.EncodeToString(sum[:]) {
		return nil, ""
	}
	return binary, meta.SHA256
})

func Available() bool {
	_, digest := embedded()
	return digest != ""
}

var prepareMu sync.Mutex

// Command returns a verified worker with stdin and stdout available for the
// job protocol. The caller owns the pipes and process lifetime.
func Command(ctx context.Context, dataDir string) (*exec.Cmd, error) {
	binary, digest := embedded()
	if digest == "" {
		return nil, errors.New("this Hub build does not include a USB worker for this platform")
	}
	prepareMu.Lock()
	defer prepareMu.Unlock()
	directory, err := filepath.Abs(filepath.Join(dataDir, "usb-worker"))
	if err != nil {
		return nil, err
	}
	if err = privatefs.EnsureDir(directory); err != nil {
		return nil, err
	}
	if err = checkDirectory(directory); err != nil {
		return nil, err
	}
	name := "worker-" + digest
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	path := filepath.Join(directory, name)
	file, err := privatefs.OpenSource(path)
	if errors.Is(err, os.ErrNotExist) {
		var tmp *os.File
		tmp, err = os.CreateTemp(directory, ".worker-")
		if err != nil {
			return nil, err
		}
		defer os.Remove(tmp.Name())
		if _, err = io.Copy(tmp, bytes.NewReader(binary)); err == nil {
			err = tmp.Chmod(0700)
		}
		if closeErr := tmp.Close(); err == nil {
			err = closeErr
		}
		if err != nil {
			return nil, err
		}
		if err = os.Rename(tmp.Name(), path); err != nil {
			// Another Hub process can finish staging the same release first.
			if _, statErr := os.Lstat(path); statErr != nil {
				return nil, err
			}
		}
		file, err = privatefs.OpenSource(path)
	}
	if err != nil {
		return nil, err
	}
	defer file.Close()
	hash := sha256.New()
	size, err := io.Copy(hash, io.LimitReader(file, int64(len(binary))+1))
	if err != nil || size != int64(len(binary)) || hex.EncodeToString(hash.Sum(nil)) != digest {
		return nil, errors.New("the cached USB worker failed integrity verification")
	}
	return exec.CommandContext(ctx, path), nil
}
