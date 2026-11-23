package stockfirmware

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"os"
	"path/filepath"
	"testing"
)

func TestWirelessPrivateCaptureChecksUIDHeadersAndBothSlotHashes(t *testing.T) {
	const uid = "synthetic-device"
	slots := []image{fixture("mcuimg1.bin", "original recovery"), fixture("mcuimg2.bin", "original application")}
	raw := make([]byte, 1<<20)
	record := func(block int, value string) {
		b := raw[block*4096:]
		binary.LittleEndian.PutUint32(b, uint32(len(value)))
		copy(b[6:8], "LS")
		copy(b[8:], value)
	}
	record(106, uid+"\x00")
	record(130, "original application")
	record(214, "original recovery")
	capture := filepath.Join(t.TempDir(), "capture.bin")
	check := func(wantOK bool) {
		t.Helper()
		if err := os.WriteFile(capture, raw, 0600); err != nil {
			t.Fatal(err)
		}
		got, err := slotsFromCapture(capture, uid, slots)
		if wantOK {
			if err != nil || !bytes.Equal(got.Application, []byte("original application")) || !bytes.Equal(got.Recovery, []byte("original recovery")) {
				t.Fatal("originals not found", err)
			}
		} else if !errors.Is(err, ErrIntegrity) {
			t.Fatal("bad capture accepted", err)
		}
	}
	check(true)
	for _, offset := range []int{106*4096 + 8, 130 * 4096, 214*4096 + 6, 130*4096 + 8, 214*4096 + 8} {
		raw[offset] ^= 1
		check(false)
		raw[offset] ^= 1
	}
	raw = raw[:1048575]
	check(false)
}

func TestUnqualifiedWirelessRestoreDoesNotFetchOrCreateFiles(t *testing.T) {
	root := t.TempDir()
	for _, model := range []string{"pico-s", "pico-c", "z-series", "zymatic"} {
		if _, err := WirelessImages(context.Background(), root, model, "unknown-device", nil); err == nil {
			t.Fatal("unverified model/identity accepted")
		}
	}
	entries, err := os.ReadDir(root)
	if err != nil || len(entries) != 0 {
		t.Fatal("unqualified restoration touched storage", err)
	}
}

func TestEmbeddedStockWorkerMatchesFirmware(t *testing.T) {
	if firmware.Digest(restoreWorker) != restoreWorkerSHA256 || len(restoreWorker) != 31480 {
		t.Fatal("embedded restoration worker differs from its reviewed build")
	}
	root := filepath.Join("..", "..", "..", "..", "firmware")
	if _, err := os.Stat(root); errors.Is(err, os.ErrNotExist) {
		return
	}
	b, err := os.ReadFile(filepath.Join(root, "pico-s-pro", "modified", "firmware", "standalone", "pico-stock-restore.bin"))
	if err != nil || !bytes.Equal(b, restoreWorker) {
		t.Fatal("Hub worker differs from the firmware component", err)
	}
}
