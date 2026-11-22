// Package stockfirmware retains CC3200 originals and verifies the public fallback for its documented device.
// The full flash capture is private to the local Hub. Only immutable, public OEM
// application and recovery blobs can be fetched from GitHub.
package stockfirmware

import (
	"context"
	"crypto/sha256"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

type image struct {
	name, sha256, blob string
	size               int64
}

var (
	fullBackup = image{name: "original-serial-flash.bin", size: 1 << 20, sha256: "837a728400da02b9da4a7d563e1b67fb24d6a5af03c054963c4b1de60c72343f"}
	oemSlots   = []image{
		{name: "mcuimg1.bin", size: 50504, sha256: "26829ef97df46e6d3667afe8352fa86492f3ddf667371f2d19eafa8468862c63", blob: "708a70b27c5a1171e7321360013e78071e1bfeb2"},
		{name: "mcuimg2.bin", size: 162792, sha256: "e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702", blob: "205f96477f9af5fd05e5506304d4e4b79e753c94"},
	}
	ErrIntegrity = errors.New("original firmware size or SHA-256 differs from the expected backup or OEM image")
)

func (i image) check(data []byte) error {
	if int64(len(data)) != i.size || fmt.Sprintf("%x", sha256.Sum256(data)) != i.sha256 {
		return fmt.Errorf("%s: %w", i.name, ErrIntegrity)
	}
	return nil
}

func (i image) read(path string) ([]byte, error) {
	b, err := privatefs.ReadFile(path, i.size)
	if err != nil {
		return nil, err
	}
	return b, i.check(b)
}

func privateDirectory(path string) error {
	if err := privatefs.EnsureDir(path); err != nil {
		return err
	}
	info, err := os.Lstat(path)
	if err != nil {
		return err
	}
	if !info.IsDir() || runtime.GOOS != "windows" && info.Mode().Perm()&0077 != 0 {
		return errors.New("the stock firmware cache requires a private directory owned by the Hub user")
	}
	return nil
}

func cacheDirectory(dataDir string) (string, error) {
	dir, err := filepath.Abs(filepath.Join(dataDir, "stock-firmware"))
	if err != nil {
		return "", err
	}
	return dir, privateDirectory(dir)
}

// publish installs a complete verified file. A private original is never
// replaced. Concurrent writers of the same reviewed bytes can share the result.
func (i image) publish(dir string, b []byte, replace bool) (string, error) {
	if err := i.check(b); err != nil {
		return "", err
	}
	path := filepath.Join(dir, i.name)
	f, err := os.CreateTemp(dir, ".incoming-")
	if err != nil {
		return "", err
	}
	defer os.Remove(f.Name())
	if _, err = f.Write(b); err == nil {
		err = f.Sync()
	}
	closeErr := f.Close()
	if err != nil || closeErr != nil {
		return "", errors.Join(err, closeErr)
	}
	if replace {
		err = os.Rename(f.Name(), path)
	} else {
		err = os.Link(f.Name(), path)
		if errors.Is(err, os.ErrExist) {
			err = nil // Verify the existing copy below.
		}
	}
	if err != nil {
		return "", err
	}
	// Persist the directory entry before a subsequent USB write can begin.
	if runtime.GOOS != "windows" {
		d, err := os.Open(dir)
		if err != nil {
			return "", err
		}
		if err := errors.Join(d.Sync(), d.Close()); err != nil {
			return "", err
		}
	}
	_, err = i.read(path)
	return path, err
}

func SaveBackup(dataDir, backupPath string) (string, error) {
	return saveBackup(dataDir, backupPath, fullBackup)
}

func saveBackup(dataDir, source string, expected image) (string, error) {
	f, err := privatefs.OpenSource(source)
	if err != nil {
		return "", err
	}
	defer f.Close()
	data, err := io.ReadAll(io.LimitReader(f, expected.size+1))
	if err != nil {
		return "", err
	}
	if err = expected.check(data); err != nil {
		return "", err
	}
	dir, err := cacheDirectory(dataDir)
	if err != nil {
		return "", err
	}
	path := filepath.Join(dir, expected.name)
	if _, err = expected.read(path); err == nil {
		return path, nil
	} else if !errors.Is(err, os.ErrNotExist) {
		return "", err
	}
	return expected.publish(dir, data, false)
}

func StoredBackup(dataDir string) (string, error) {
	dir, err := cacheDirectory(dataDir)
	if err != nil {
		return "", err
	}
	path := filepath.Join(dir, fullBackup.name)
	_, err = fullBackup.read(path)
	return path, err
}

type Source struct {
	Backup, OEMDirectory, Description string
}

// HasPublicSlots identifies the unit whose original application, recovery and
// allocation/boot layout were independently recorded with the GitHub blobs.
func HasPublicSlots(uid string) bool {
	return firmware.Digest([]byte(uid)) == "7290674610eca1cf26581779a71c7f41a9c2fc367eb7189d0e93918319964bc7"
}

func Resolve(ctx context.Context, dataDir string, g *firmware.GitHub) (Source, error) {
	return resolve(ctx, dataDir, g, fullBackup, oemSlots)
}

func resolve(ctx context.Context, dataDir string, g *firmware.GitHub, backup image, slots []image) (Source, error) {
	dir, err := cacheDirectory(dataDir)
	if err != nil {
		return Source{}, err
	}
	path := filepath.Join(dir, backup.name)
	_, err = backup.read(path)
	if err == nil {
		return Source{Backup: path, Description: "Verified original full-flash backup stored in this Hub"}, nil
	}
	note := ""
	if errors.Is(err, ErrIntegrity) {
		note = "The stored full-flash backup failed verification and was preserved. "
	} else if !errors.Is(err, os.ErrNotExist) {
		return Source{}, err
	}
	oemDir := filepath.Join(dir, "oem-slots")
	if err := privateDirectory(oemDir); err != nil {
		return Source{}, err
	}
	description := "Verified original application and recovery previously downloaded from GitHub"
	for _, slot := range slots {
		_, err := slot.read(filepath.Join(oemDir, slot.name))
		if err == nil {
			continue
		}
		corrupt := errors.Is(err, ErrIntegrity)
		if !errors.Is(err, os.ErrNotExist) && !corrupt {
			return Source{}, err
		}
		data, err := slot.download(ctx, g)
		if err != nil {
			return Source{}, fmt.Errorf("could not obtain the original %s from GitHub: %w", slot.name, err)
		}
		if _, err = slot.publish(oemDir, data, corrupt); err != nil {
			return Source{}, err
		}
		description = "Verified original application and recovery from the project's pinned GitHub blobs"
	}
	return Source{OEMDirectory: oemDir, Description: note + description}, nil
}

func (i image) download(ctx context.Context, g *firmware.GitHub) ([]byte, error) {
	if g == nil || g.HTTP == nil {
		return nil, errors.New("GitHub client unavailable")
	}
	endpoint := "https://api.github.com/repos/" + firmware.DefaultRepository + "/git/blobs/" + i.blob
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github.raw+json")
	resp, err := g.HTTP.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GitHub returned HTTP %d", resp.StatusCode)
	}
	data, err := io.ReadAll(io.LimitReader(resp.Body, i.size+1))
	if err != nil {
		return nil, err
	}
	return data, i.check(data)
}
