package stockfirmware

import (
	"bytes"
	"context"
	_ "embed"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

//go:embed assets/pico-s-pro-stock-worker.bin
var restoreWorker []byte

const restoreWorkerSHA256 = "62f6d79ac39bd7b36e9e471e1ba9239390ecb2bdbe4cd0b7f105b8f55c364f2d"

type RestoreImages struct{ Worker, Application, Recovery []byte }

func WirelessProfile(model, uid string) bool {
	return (model == "pico-s" || model == "pico-pro") && HasPublicSlots(uid)
}

// Original slots for this profile are independently pinned. A private backup can
// have different unused flash bytes across captures. Accept only a matching UID
// and both complete verified TI file records on the reviewed block boundaries.
func slotsFromCapture(path, uid string, slots []image) (RestoreImages, error) {
	raw, err := privatefs.ReadFile(path, 1<<20)
	if err != nil {
		return RestoreImages{}, err
	}
	return captureSlots(raw, uid, slots)
}
func captureSlots(raw []byte, uid string, slots []image) (RestoreImages, error) {
	if len(raw) != 1<<20 {
		return RestoreImages{}, ErrIntegrity
	}
	record := func(block int) []byte {
		b := raw[block*4096:]
		length := int(binary.LittleEndian.Uint32(b[:4]) & 0xffffff)
		if !bytes.Equal(b[6:8], []byte("LS")) || length > len(b)-8 {
			return nil
		}
		return b[8 : 8+length]
	}
	gotUID := bytes.TrimSuffix(record(106), []byte{0})
	app, recovery := record(130), record(214)
	if string(gotUID) != uid || slots[0].check(recovery) != nil || slots[1].check(app) != nil {
		return RestoreImages{}, ErrIntegrity
	}
	return RestoreImages{Worker: restoreWorker, Application: app, Recovery: recovery}, nil
}

func WirelessImages(ctx context.Context, data, model, uid string, github *firmware.GitHub) (RestoreImages, error) {
	if !WirelessProfile(model, uid) {
		return RestoreImages{}, errors.New("This brewer has no verified wireless stock restoration profile")
	}
	if len(restoreWorker) != 31480 || firmware.Digest(restoreWorker) != restoreWorkerSHA256 {
		return RestoreImages{}, errors.New("The embedded stock restoration worker is unavailable")
	}
	for _, name := range []string{"pico-s", "pico-pro"} {
		images, found, err := savedWirelessImages(data, name, uid, oemSlots)
		if err != nil {
			return RestoreImages{}, err
		}
		if found {
			return images, nil
		}
	}
	source, err := Resolve(ctx, data, github)
	if err != nil {
		return RestoreImages{}, err
	}
	if source.Backup != "" {
		return slotsFromCapture(source.Backup, uid, oemSlots)
	}
	rec, err := oemSlots[0].read(filepath.Join(source.OEMDirectory, oemSlots[0].name))
	if err != nil {
		return RestoreImages{}, err
	}
	app, err := oemSlots[1].read(filepath.Join(source.OEMDirectory, oemSlots[1].name))
	if err != nil {
		return RestoreImages{}, fmt.Errorf("checking original application: %w", err)
	}
	return RestoreImages{Worker: restoreWorker, Application: app, Recovery: rec}, nil
}

func savedWirelessImages(data, model, uid string, slots []image) (RestoreImages, bool, error) {
	group, err := profileDirectory(data, model, "")
	if err != nil {
		return RestoreImages{}, false, err
	}
	dir := filepath.Join(group, firmware.Digest([]byte(uid)))
	_, err = os.Lstat(dir)
	if errors.Is(err, os.ErrNotExist) {
		return RestoreImages{}, false, nil
	}
	if err != nil {
		return RestoreImages{}, false, err
	}
	if err = privateDirectory(dir); err != nil {
		return RestoreImages{}, false, err
	}
	files, err := os.ReadDir(dir)
	if err != nil {
		return RestoreImages{}, false, err
	}
	// Each capture is independently admitted by its saved digest, UID, TI
	// records and original slot hashes, so equivalent captures are unambiguous.
	for _, file := range files {
		h, ok := strings.CutSuffix(file.Name(), ".bin")
		if !ok || !digest.MatchString(h) {
			continue
		}
		raw, err := (image{name: file.Name(), size: 1 << 20, sha256: h}).read(filepath.Join(dir, file.Name()))
		if errors.Is(err, ErrIntegrity) {
			continue
		}
		if err != nil {
			return RestoreImages{}, false, err
		}
		images, err := captureSlots(raw, uid, slots)
		if err == nil {
			return images, true, nil
		}
		if !errors.Is(err, ErrIntegrity) {
			return RestoreImages{}, false, err
		}
	}
	return RestoreImages{}, false, nil
}
