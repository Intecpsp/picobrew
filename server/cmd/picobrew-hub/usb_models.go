package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
)

func isCC(model string) bool { return model == "pico-s" || model == "pico-pro" || model == "pico-c" }

var ccDeviceUID = regexp.MustCompile("^[a-f0-9]{32}$")

func checkUSBTarget(model, display, port, device, action string) error {
	if isCC(model) {
		if model == "pico-c" {
			if display != "ssd1325" && display != "ssd1309" {
				return errors.New("Pico C requires --display ssd1325 or ssd1309; the original application must match")
			}
		} else if display != "" {
			return errors.New("--display applies only to Pico C")
		}
		if port != "" || device != "" && !ccDeviceUID.MatchString(device) {
			return errors.New("CC3200 uses the local CH340 adapter; --device takes the brewer's 32-character lowercase UID")
		}
		return nil
	}
	if display != "" || device != "" {
		return errors.New("--display and --device are CC3200 options")
	}
	switch model {
	case "zymatic":
		if action != "restore" && port == "" {
			return errors.New("select the Zymatic built-in USB serial port with --port")
		}

		return nil
	case "z1", "z2", "z3", "z4":
		if action == "install" {
			return errors.New("standalone images have no verified model-specific mapping for Z stacks; the z-series profile independently qualifies a single module from its boot and flash contents")
		}
	case "z-series", "picostill", "picoferm":
	default:
		return errors.New("choose a supported physical model: pico-s, pico-pro, pico-c, z-series, z1–z4, picostill, picoferm or zymatic (process Z stacks one module at a time)")
	}
	if port == "" {
		return errors.New("supply the attached module's local serial device with --port")
	}
	return nil
}

func targetArgs(model, display string) []string {
	args := []string{"--stock-profile", "--model", model}
	if display != "" {
		args = append(args, "--display", display)
	}
	return args
}

func ccCache(checkout, model, display, device string, fresh bool) (usbBackupCache, error) {
	component, python, err := findUSBCheckout(checkout)
	if err != nil {
		return usbBackupCache{}, err
	}
	wantUID := ""
	if device != "" {
		wantUID = firmware.Digest([]byte(device))
	}
	return usbBackupCache{
		stored: func(dir string) (string, error) {
			if fresh && device == "" {
				return "", os.ErrNotExist
			}
			p, err := stockfirmware.StoredProfile(dir, model, display, wantUID)
			if errors.Is(err, os.ErrNotExist) && stockfirmware.HasPublicSlots(device) && model != "pico-c" {
				return stockfirmware.StoredBackup(dir)
			}
			return p, err
		},
		save: func(dir, path string) (string, error) {
			args := []string{filepath.Join(component, "tools", "pico_flash_install.py"), "inspect-stock", "--model", model, "--backup", path}
			if display != "" {
				args = append(args, "--display", display)
			}
			cmd := exec.Command(python, args...)
			cmd.Stderr = os.Stderr
			b, err := cmd.Output()
			if err != nil {
				return "", fmt.Errorf("stock inspection failed: %w", err)
			}
			var p stockfirmware.Profile
			if err := json.Unmarshal(b, &p); err != nil {
				return "", err
			}
			if p.Model != model || p.Display != display || wantUID != "" && p.UIDHash != wantUID {
				return "", errors.New("the stock capture does not match the selected brewer")
			}
			return stockfirmware.SaveProfile(dir, path, p)
		},
	}, nil
}

func runFamilyUSB(action, checkout, data, model, port, oemFile string, extra ...string) error {
	script := "firmware/common/tools/esp_usb.py"
	args := []string{action, "--model", model, "--port", port, "--data", data}
	if model == "zymatic" {
		script = "firmware/zymatic/modified/tools/usb_stock.py"
		args = []string{"--data", data}
		if port != "" {
			args = append(args, "--action", action, "--port", port)
		}
		if oemFile != "" {
			args = append(args, "--oem-file", oemFile)
		}
	}
	root, err := filepath.Abs(checkout)
	if err != nil {
		return err
	}
	for {
		if st, err := os.Stat(filepath.Join(root, script)); err == nil && st.Mode().IsRegular() {
			break
		}
		parent := filepath.Dir(root)
		if checkout != "" || parent == root {
			return errors.New("run from the full firmware checkout or select it with --checkout")
		}
		root = parent
	}
	venv := "bin/python"
	if runtime.GOOS == "windows" {
		venv = "Scripts/python.exe"
	}
	python := filepath.Join(root, ".venv", venv)
	if _, err := os.Stat(python); errors.Is(err, os.ErrNotExist) {
		python, err = exec.LookPath("python3")
		if err != nil {
			python, err = exec.LookPath("python")
		}
		if err != nil {
			return errors.New("install Python 3 and the USB requirements from the full checkout")
		}
	}
	args = append(args, extra...)
	cmd := exec.Command(python, append([]string{filepath.Join(root, script)}, args...)...)
	cmd.Dir = root
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s USB %s: %w", model, action, err)
	}
	return nil
}

func runUSBBackupCommand(args []string) error {
	return usbBackupCommand(args, os.Stderr)
}

func usbBackupCommand(args []string, out io.Writer) error {
	var opts usbInstallOptions
	fs := flag.NewFlagSet("usb-backup", flag.ContinueOnError)
	fs.SetOutput(out)
	fs.StringVar(&opts.model, "model", "", "Physical model")
	fs.StringVar(&opts.display, "display", "", "Pico C display controller")
	fs.StringVar(&opts.port, "port", "", "Local ESP service port or Zymatic USB serial port")
	fs.StringVar(&opts.checkout, "checkout", "", "Full firmware checkout")
	fs.StringVar(&opts.dataDir, "data", "data", "Hub data directory")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 0 || opts.dataDir == "" {
		return errors.New("specify a model and Hub data directory")
	}
	if err := checkUSBTarget(opts.model, opts.display, opts.port, "", "backup"); err != nil {
		return err
	}
	data, err := filepath.Abs(opts.dataDir)
	if err != nil {
		return err
	}
	if !isCC(opts.model) {
		return runFamilyUSB("backup", opts.checkout, data, opts.model, opts.port, "")
	}
	opts.dataDir, opts.startAt = data, "provision-key"
	_, tty, run, err := openUSBTools(opts.checkout)
	if err != nil {
		return err
	}
	defer tty.Close()
	cache, err := ccCache(opts.checkout, opts.model, opts.display, "", true)
	if err != nil {
		return err
	}
	source, err := stockBackupSource(opts, bufio.NewReader(tty), tty, run, cache)
	if err != nil {
		return err
	}
	saved, err := cache.save(data, source)
	if err == nil {
		fmt.Fprintln(tty, "Verified original firmware in Hub data:", saved)
	}
	return err
}
