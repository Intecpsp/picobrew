package main

import (
	"bufio"
	"bytes"
	"errors"
	"flag"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"slices"
	"strings"
	"testing"
)

func TestUSBInstallRequiresExplicitModelAndInputs(t *testing.T) {
	root := t.TempDir()
	paths := []string{"--backup", filepath.Join(root, "original.bin"), "--key-file", filepath.Join(root, "private key")}
	for _, model := range []string{"pico-s", "pico-pro"} {
		options, err := parseUSBInstallOptions(append([]string{"--model", model}, paths...), io.Discard)
		if err != nil || options.model != model || options.startAt != "provision-key" {
			t.Fatalf("options for %s: %+v %v", model, options, err)
		}
	}
	for _, args := range [][]string{paths, append([]string{"--model", "pico-c"}, paths...), {"--model", "pico-s"},
		append([]string{"--start-at", "restore", "--model", "pico-s"}, paths...)} {
		if _, err := parseUSBInstallOptions(args, io.Discard); err == nil {
			t.Fatalf("accepted %v", args)
		}
	}
	if _, err := parseUSBInstallOptions([]string{"--help"}, io.Discard); !errors.Is(err, flag.ErrHelp) {
		t.Fatal(err)
	}
}

func TestUSBInstallFindsCheckoutFromServer(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("macOS virtual environment layout")
	}
	root, err := filepath.EvalSymlinks(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	component := filepath.Join(root, usbComponent)
	script := filepath.Join(component, "tools", "pico_flash_install.py")
	python := filepath.Join(component, ".venv", "bin", "python")
	for _, file := range []string{script, python} {
		if err := os.MkdirAll(filepath.Dir(file), 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(file, nil, 0700); err != nil {
			t.Fatal(err)
		}
	}
	server := filepath.Join(root, "server")
	if err := os.Mkdir(server, 0700); err != nil {
		t.Fatal(err)
	}
	t.Chdir(server)
	for _, path := range []string{"", root} {
		comp, interpreter, err := findUSBCheckout(path)
		if err != nil || interpreter != python || comp != component {
			t.Fatalf("checkout: %q %q %v", comp, interpreter, err)
		}
	}
}

func TestUSBInstallStageSequence(t *testing.T) {
	const authorizeAll = "provision-key\ninstall\ninstall-recovery\n"
	for _, tc := range []struct {
		name, startAt, input, failAt string
		existingKey, wantSuccess     bool
		want                         []string
	}{
		{"initial", "provision-key", authorizeAll, "", false, true, []string{"prepare-install", "create-key", "provision-key", "install", "install-recovery"}},
		{"existing key", "provision-key", authorizeAll, "", true, true, []string{"prepare-install", "provision-key", "install", "install-recovery"}},
		{"wrong backup", "provision-key", authorizeAll, "prepare-install", false, false, []string{"prepare-install"}},
		{"key creation failed", "provision-key", authorizeAll, "create-key", false, false, []string{"prepare-install", "create-key"}},
		{"unconfirmed application", "provision-key", "provision-key\nno\n", "", true, false, []string{"prepare-install", "provision-key"}},
		{"rejected by writer", "provision-key", authorizeAll, "install", true, false, []string{"prepare-install", "provision-key", "install"}},
		{"resume app", "install", "install\ninstall-recovery\n", "", true, true, []string{"prepare-install", "install", "install-recovery"}},
		{"resume recovery", "install-recovery", "install-recovery\n", "", true, true, []string{"prepare-install", "install-recovery"}},
		{"lost resume key", "install", "install\n", "", false, false, []string{"prepare-install"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			opts := usbInstallOptions{model: "pico-s", backup: filepath.Join(dir, "this brewer.bin"), keyFile: filepath.Join(dir, "device.key"), startAt: tc.startAt, dataDir: dir}
			cached := filepath.Join(dir, "stored-stock.bin")
			saved := false
			cache := usbBackupCache{
				stored: func(string) (string, error) { t.Fatal("should use the explicit backup"); return "", nil },
				save: func(dataDir, source string) (string, error) {
					if dataDir != dir || source != opts.backup {
						t.Fatalf("caching %s, %s", dataDir, source)
					}
					saved = true
					return cached, nil
				},
			}
			if tc.existingKey {
				if err := os.WriteFile(opts.keyFile, []byte("test value"), 0600); err != nil {
					t.Fatal(err)
				}
			}
			var stages []string
			seenReports := make(map[string]bool)
			run := func(usb bool, script string, args ...string) error {
				stage := args[0]
				if script == "provision_api_key.py" {
					if usb || !slices.Equal(args, []string{"--output", opts.keyFile}) {
						t.Fatalf("unexpected key creation: usb=%v, %v", usb, args)
					}
					stage = "create-key"
				} else if script != "pico_flash_install.py" {
					t.Fatalf("unexpected tool %s", script)
				} else if stage == "prepare-install" {
					if usb || !slices.Equal(args, []string{"prepare-install", "--model", opts.model, "--backup", opts.backup}) {
						t.Fatalf("unexpected preparation: usb=%v, %v", usb, args)
					}
				} else {
					if !saved || !usb || !slices.Contains(args, "--verify-backup-uid") {
						t.Fatalf("unguarded writer invocation: usb=%v, %v", usb, args)
					}
					want := map[string]string{"--backup": cached, "--key-file": opts.keyFile}
					if stage != "provision-key" {
						want["--model"] = opts.model
					}
					if stage == "install" {
						want["--firmware"] = filepath.Join(dir, "firmware", "standalone", opts.model, "pico-s-standalone.bin")
					}
					for flag, value := range want {
						i := slices.Index(args, flag)
						if i < 0 || i+1 == len(args) || args[i+1] != value {
							t.Fatalf("expected %s %s: %v", flag, value, args)
						}
					}
					i := slices.Index(args, "--output")
					if i < 0 || i+1 == len(args) {
						t.Fatal(args)
					}
					report := args[i+1]
					info, err := os.Stat(filepath.Dir(report))
					if err != nil || !info.IsDir() || (runtime.GOOS != "windows" && info.Mode().Perm() != 0700) || seenReports[report] ||
						filepath.Base(report) != stage+".json" || filepath.Dir(filepath.Dir(report)) != dir {
						t.Fatalf("invalid private report path %s", report)
					}
					seenReports[report] = true
				}
				stages = append(stages, stage)
				if tc.failAt == stage {
					return errors.New("simulated tool failure")
				}
				return nil
			}
			var output bytes.Buffer
			err := installOverUSB(opts, dir, strings.NewReader(tc.input), &output, run, cache)
			if (err == nil) != tc.wantSuccess || !reflect.DeepEqual(stages, tc.want) {
				t.Fatalf("stages=%v err=%v", stages, err)
			}
			if strings.Contains(output.String(), "Recovery verified.") != tc.wantSuccess || strings.Contains(output.String(), "test value") {
				t.Fatal(output.String())
			}
		})
	}
}

func TestUSBBackupUsesStoredOriginalOrCapturesBeforeInstall(t *testing.T) {
	for _, tc := range []struct {
		name, input, startAt            string
		lookupErr                       error
		captureFails, saveFails, wantOK bool
		commands                        string
	}{
		{"verified cached", "", "provision-key", nil, false, false, true, "prepare-install"},
		{"corrupt cached original", "backup\n", "provision-key", errors.New("hash mismatch"), false, false, false, ""},
		{"new capture", "backup\n", "provision-key", os.ErrNotExist, false, false, true, "capture,prepare-install"},
		{"capture stopped", "\n", "provision-key", os.ErrNotExist, false, false, false, ""},
		{"incomplete capture", "backup\n", "provision-key", os.ErrNotExist, true, false, false, "capture"},
		{"storage failure", "backup\n", "provision-key", os.ErrNotExist, false, true, false, "capture,prepare-install"},
		{"no stock at resume", "backup\n", "install", os.ErrNotExist, false, false, false, ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			opts := usbInstallOptions{model: "pico-pro", startAt: tc.startAt, dataDir: t.TempDir()}
			source := filepath.Join(opts.dataDir, "stored-stock.bin")
			var commands []string
			cache := usbBackupCache{
				stored: func(string) (string, error) {
					if tc.lookupErr != nil {
						return "", tc.lookupErr
					}
					return source, nil
				},
				save: func(dir, backup string) (string, error) {
					if backup != source || dir != opts.dataDir {
						t.Fatalf("unexpected save (%s, %s)", dir, backup)
					}
					if tc.saveFails {
						return "", errors.New("unwritable")
					}
					return filepath.Join(dir, "stored-stock.bin"), nil
				},
			}
			run := func(usb bool, script string, args ...string) error {
				switch script {
				case "pico_flash_backup_v2.py":
					if !usb || len(args) != 2 || args[0] != "--output" || filepath.Dir(filepath.Dir(args[1])) != opts.dataDir {
						t.Fatal(args)
					}
					commands = append(commands, "capture")
					source = args[1]
					if tc.captureFails {
						return errors.New("adapter unavailable")
					}
				case "pico_flash_install.py":
					if usb || !slices.Equal(args, []string{"prepare-install", "--model", opts.model, "--backup", source}) {
						t.Fatal(args)
					}
					commands = append(commands, args[0])
				default:
					t.Fatal(script)
				}
				return nil
			}
			path, err := prepareUSBBackup(opts, bufio.NewReader(strings.NewReader(tc.input)), io.Discard, run, cache)
			if (err == nil) != tc.wantOK || (path != "") != tc.wantOK || strings.Join(commands, ",") != tc.commands {
				t.Fatalf("path=%s, err=%v, commands=%v", path, err, commands)
			}
		})
	}
}
