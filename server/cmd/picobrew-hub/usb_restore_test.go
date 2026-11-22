package main

import (
	"bytes"
	"errors"
	"flag"
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
)

func TestUSBRestoreCommandOptions(t *testing.T) {
	dir := t.TempDir()
	explicit := filepath.Join(dir, "original.bin")
	opts, err := parseUSBRestoreOptions([]string{"--data", dir, "--checkout", "..", "--backup", explicit}, io.Discard)
	if err != nil || opts.dataDir != dir || opts.checkout != ".." || opts.backup != explicit {
		t.Fatalf("%+v, %v", opts, err)
	}
	for _, args := range [][]string{{"pico-pro"}, {"--backup", "relative.bin"}, {"--data", ""}, {"--force"}} {
		if _, err := parseUSBRestoreOptions(args, io.Discard); err == nil {
			t.Fatalf("accepted %v", args)
		}
	}
	if _, err := parseUSBRestoreOptions([]string{"--help"}, io.Discard); !errors.Is(err, flag.ErrHelp) {
		t.Fatal(err)
	}
}

func TestUSBRestoreRequiresTwoConfirmationsAndOneVerifiedSource(t *testing.T) {
	const both = "use-stock\nrestore-stock\n"
	for _, tc := range []struct {
		name, input, source, fail string
		userBackup, success       bool
		calls                     string
	}{
		{"before inspection", "\n", "local", "", false, false, ""},
		{"before writing", "use-stock\nno\n", "github", "", false, false, "resolve"},
		{"import failed", both, "local", "save", true, false, "save"},
		{"source failed", both, "github", "resolve", false, false, "resolve"},
		{"no valid source", both, "empty", "", false, false, "resolve"},
		{"ambiguous source", both, "both", "", false, false, "resolve"},
		{"writer stopped", both, "local", "write", false, false, "resolve,write"},
		{"existing backup", both, "local", "", false, true, "resolve,write"},
		{"import original", both, "local", "", true, true, "save,resolve,write"},
		{"verified OEM pair", both, "github", "", false, true, "resolve,write"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			opts := usbRestoreOptions{dataDir: dir}
			if tc.userBackup {
				opts.backup = filepath.Join(dir, "external original.bin")
			}
			var calls []string
			save := func(data, input string) (string, error) {
				calls = append(calls, "save")
				if !tc.userBackup || data != dir || input != opts.backup {
					t.Fatalf("unexpected import %q, %q", data, input)
				}
				if tc.fail == "save" {
					return "", errors.New("wrong original backup")
				}
				return filepath.Join(dir, "cache", "stock.bin"), nil
			}
			from := stockfirmware.Source{Description: "verified test source"}
			if tc.source == "local" || tc.source == "both" {
				from.Backup = filepath.Join(dir, "cache", "stock.bin")
			}
			if tc.source == "github" || tc.source == "both" {
				from.OEMDirectory = filepath.Join(dir, "OEM source")
			}
			resolve := func(data string) (stockfirmware.Source, error) {
				calls = append(calls, "resolve")
				if data != dir {
					t.Fatal(data)
				}
				if tc.fail == "resolve" {
					return stockfirmware.Source{}, errors.New("unavailable")
				}
				return from, nil
			}
			var out bytes.Buffer
			writer := func(usb bool, script string, args ...string) error {
				calls = append(calls, "write")
				sourceArg := []string{"--backup", from.Backup}
				if tc.source == "github" {
					sourceArg = []string{"--oem-directory", from.OEMDirectory}
				}
				if !usb || script != "pico_flash_install.py" || len(args) != 5 || args[0] != "restore-stock" ||
					!slices.Equal(args[1:3], sourceArg) || args[3] != "--output" || filepath.Dir(filepath.Dir(args[4])) != dir {
					t.Fatalf("unexpected restore %v %s %v", usb, script, args)
				}
				if _, err := os.Stat(filepath.Dir(args[4])); err != nil {
					t.Fatal(err)
				}
				if !strings.Contains(out.String(), from.Description) || strings.Count(out.String(), "press Return") != 2 {
					t.Fatal(out.String())
				}
				if tc.fail == "write" {
					return errors.New("live UID mismatch")
				}
				return nil
			}
			err := restoreOverUSB(opts, strings.NewReader(tc.input), &out, writer, save, resolve)
			if strings.Join(calls, ",") != tc.calls || (err == nil) != tc.success ||
				strings.Contains(out.String(), "Original application and recovery verified.") != tc.success {
				t.Fatalf("calls=%v, err=%v, output=%s", calls, err, out.String())
			}
		})
	}
}
