package stockfirmware

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

// Profile comes from the offline CC3200 filesystem and OEM image inspection.
type Profile struct {
	Model      string `json:"model"`
	Display    string `json:"display_variant"`
	UIDHash    string `json:"uid_sha256"`
	BackupHash string `json:"backup_sha256"`
}

var digest = regexp.MustCompile("^[0-9a-f]{64}$")

func profileDirectory(dataDir, model, display string) (string, error) {
	switch {
	case (model == "pico-s" || model == "pico-pro") && display == "":
	case model == "pico-c" && (display == "ssd1309" || display == "ssd1325"):
		model += "-" + display
	default:
		return "", errors.New("unknown CC3200 stock profile")
	}
	base, err := cacheDirectory(dataDir)
	if err != nil {
		return "", err
	}
	dir := filepath.Join(base, model)
	return dir, privateDirectory(dir)
}

func SaveProfile(dataDir, source string, p Profile) (string, error) {
	if !digest.MatchString(p.UIDHash) || !digest.MatchString(p.BackupHash) {
		return "", errors.New("invalid stock profile digest")
	}
	dir, err := profileDirectory(dataDir, p.Model, p.Display)
	if err != nil {
		return "", err
	}
	dir = filepath.Join(dir, p.UIDHash)
	if err := privateDirectory(dir); err != nil {
		return "", err
	}
	f, err := privatefs.OpenSource(source)
	if err != nil {
		return "", err
	}
	defer f.Close()
	im := image{name: p.BackupHash + ".bin", size: 1 << 20, sha256: p.BackupHash}
	b, err := io.ReadAll(io.LimitReader(f, im.size+1))
	if err != nil {
		return "", err
	}
	return im.publish(dir, b, false)
}

// StoredProfile selects an unambiguous original. Hash check again before use.
func StoredProfile(dataDir, model, display, uidHash string) (string, error) {
	if uidHash != "" && !digest.MatchString(uidHash) {
		return "", errors.New("invalid brewer identity")
	}
	base, err := profileDirectory(dataDir, model, display)
	if err != nil {
		return "", err
	}
	units, err := os.ReadDir(base)
	if err != nil {
		return "", err
	}
	var match string
	for _, unit := range units {
		if !digest.MatchString(unit.Name()) || (uidHash != "" && unit.Name() != uidHash) {
			continue
		}
		dir := filepath.Join(base, unit.Name())
		if !unit.IsDir() {
			return "", errors.New("invalid unit directory in stock firmware cache")
		}
		if err := privateDirectory(dir); err != nil {
			return "", err
		}
		files, err := os.ReadDir(dir)
		if err != nil {
			return "", err
		}
		for _, file := range files {
			h, ok := strings.CutSuffix(file.Name(), ".bin")
			if !ok || !digest.MatchString(h) {
				continue
			}
			if match != "" {
				return "", errors.New("multiple stock backups match; specify this brewer with --device or select its exact original with --backup")
			}
			match = filepath.Join(dir, file.Name())
			if _, err := (image{name: file.Name(), size: 1 << 20, sha256: h}).read(match); err != nil {
				return "", fmt.Errorf("checking saved original: %w", err)
			}
		}
	}
	if match == "" {
		return "", os.ErrNotExist
	}
	return match, nil
}
