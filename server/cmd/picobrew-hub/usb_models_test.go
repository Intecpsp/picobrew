package main

import (
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

func TestUSBModelOperationEligibility(t *testing.T) {
	dir := t.TempDir()
	key := filepath.Join(dir, "key")
	for _, model := range []string{"pico-s", "pico-pro", "pico-c", "picoferm", "picostill", "z-series", "z1", "z2", "z3", "z4", "zymatic"} {
		args := []string{"--model", model, "--data", dir}
		switch {
		case model == "pico-c":
			args = append(args, "--display", "ssd1309")
		case model != "zymatic" && !isCC(model):
			args = append(args, "--port", "COM3")
		}
		if _, err := parseUSBRestoreOptions(args, io.Discard); err != nil {
			t.Errorf("%s restore: %v", model, err)
		}
		if isCC(model) {
			args = append(args, "--key-file", key)
		}
		_, err := parseUSBInstallOptions(args, io.Discard)
		want := isCC(model) || slices.Contains([]string{"picoferm", "picostill", "z-series"}, model)
		if (err == nil) != want {
			t.Errorf("%s install: %v", model, err)
		}
	}
	for _, args := range [][]string{{"--model", "pico-c"}, {"--model", "pico-s", "--display", "ssd1325"}, {"--model", "picoferm"}, {"--model", "picostill", "--port", "COM2", "--oem-file", key}, {"--model", "pico-c", "--display", "ssd1325", "--device", "../../stock"}, {"--model", "z-series", "--port", "COM2", "--backup", key}} {
		if _, err := parseUSBRestoreOptions(args, io.Discard); err == nil {
			t.Errorf("accepted %q", args)
		}
	}
}

func TestPicoCUSBStages(t *testing.T) {
	dir := t.TempDir()
	key := filepath.Join(dir, "key")
	if err := os.WriteFile(key, []byte("test"), 0600); err != nil {
		t.Fatal(err)
	}
	for _, display := range []string{"ssd1325", "ssd1309"} {
		c := filepath.Join(dir, "firmware", "pico-s-pro", "modified")
		opts := usbInstallOptions{model: "pico-c", display: display, backup: filepath.Join(dir, "original"), dataDir: dir, keyFile: key, startAt: "provision-key"}
		cache := usbBackupCache{save: func(_, path string) (string, error) { return path, nil }}
		var stages []string
		run := func(usb bool, script string, args ...string) error {
			stages = append(stages, args[0])
			if script != "pico_flash_install.py" || usb != (args[0] != "prepare-install") || !slices.Contains(args, "--stock-profile") || !slices.Contains(args, display) {
				t.Fatal(script, args)
			}
			if args[0] == "install" && !slices.Contains(args, filepath.Join(dir, "firmware", "pico-c", "modified", "firmware", "standalone", "pico-c", display, "pico-c-standalone.bin")) {
				t.Fatal("wrong image", args)
			}
			return nil
		}
		if err := installOverUSB(opts, c, strings.NewReader("provision-key\ninstall\n"), io.Discard, run, cache); err != nil {
			t.Fatal(err)
		}
		if !slices.Equal(stages, []string{"prepare-install", "provision-key", "install"}) {
			t.Fatal(stages)
		}
	}
}
