package main

import (
	"bufio"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
)

type usbInstallOptions struct {
	checkout, model, backup, keyFile, startAt, dataDir string
}

const usbComponent = "firmware/pico-s-pro/modified"

func parseUSBInstallOptions(args []string, out io.Writer) (usbInstallOptions, error) {
	var opts usbInstallOptions
	flags := flag.NewFlagSet("usb-install", flag.ContinueOnError)
	flags.SetOutput(out)
	flags.Usage = func() {
		fmt.Fprintln(out, "Usage: picobrew-hub usb-install --model pico-s|pico-pro --key-file /path/to/api.key [--backup /path/to/original.bin] [--data ./data]")
		fmt.Fprintln(out, "Guided macOS USB installation for the reviewed brewer and its exact original backup. Run from the firmware checkout as your usual user.")
		flags.PrintDefaults()
	}
	flags.StringVar(&opts.checkout, "checkout", "", "Full firmware checkout (defaults to the checkout containing the current directory)")
	flags.StringVar(&opts.model, "model", "", "Physical brewer model: pico-s or pico-pro")
	flags.StringVar(&opts.backup, "backup", "", "Existing original full flash backup (absolute path); otherwise use the Hub copy or capture stock flash over USB")
	flags.StringVar(&opts.dataDir, "data", "data", "Persistent data directory used by your Hub")
	flags.StringVar(&opts.keyFile, "key-file", "", "Absolute path to the private device key (created on initial setup if missing)")
	flags.StringVar(&opts.startAt, "start-at", "provision-key", "First USB stage: provision-key, install, or install-recovery (resume with the same key after a verified prior stage)")
	if err := flags.Parse(args); err != nil {
		return opts, err
	}
	if flags.NArg() != 0 || (opts.model != "pico-s" && opts.model != "pico-pro") ||
		(opts.backup != "" && !filepath.IsAbs(opts.backup)) || !filepath.IsAbs(opts.keyFile) || opts.dataDir == "" {
		flags.Usage()
		return opts, errors.New("select a supported model, a Hub data directory and an absolute key file path; an explicit backup path must also be absolute")
	}
	switch opts.startAt {
	case "provision-key", "install", "install-recovery":
	default:
		return opts, errors.New("unknown --start-at stage")
	}
	dataDir, err := filepath.Abs(opts.dataDir)
	opts.dataDir = dataDir
	return opts, err
}

func findUSBCheckout(checkout string) (component, python string, err error) {
	root, err := filepath.Abs(checkout)
	if err != nil {
		return "", "", err
	}
	for {
		component = filepath.Join(root, usbComponent)
		if info, err := os.Stat(filepath.Join(component, "tools", "pico_flash_install.py")); err == nil && info.Mode().IsRegular() {
			break
		}
		parent := filepath.Dir(root)
		if checkout != "" || parent == root {
			return "", "", errors.New("could not find the Pico USB tools; run from a full firmware checkout or supply --checkout")
		}
		root = parent
	}
	for _, base := range []string{component, root} {
		python = filepath.Join(base, ".venv", "bin", "python")
		if info, err := os.Stat(python); err == nil && info.Mode().IsRegular() && info.Mode()&0111 != 0 {
			return component, python, nil
		}
	}
	return "", "", fmt.Errorf("set up the Python environment as described in %s", filepath.Join(component, "docs", "macos-usb-installation.md"))
}

func runUSBInstallCommand(args []string) error {
	opts, err := parseUSBInstallOptions(args, os.Stderr)
	if err != nil {
		return err
	}
	component, tty, run, err := openUSBTools(opts.checkout)
	if err != nil {
		return err
	}
	defer tty.Close()
	cache := usbBackupCache{stored: stockfirmware.StoredBackup, save: stockfirmware.SaveBackup}
	return installOverUSB(opts, component, tty, tty, run, cache)
}

func openUSBTools(checkout string) (string, *os.File, usbToolRunner, error) {
	if runtime.GOOS != "darwin" {
		return "", nil, nil, errors.New("the reviewed USB procedure requires an attached Mac with the CH340 adapter")
	}
	if os.Geteuid() == 0 {
		return "", nil, nil, errors.New("run this command as your usual user; it requests sudo for each USB session")
	}
	component, python, err := findUSBCheckout(checkout)
	if err != nil {
		return "", nil, nil, err
	}
	tty, err := os.OpenFile("/dev/tty", os.O_RDWR, 0)
	if err != nil {
		return "", nil, nil, errors.New("open a local interactive terminal to confirm each USB session")
	}
	run := func(usb bool, script string, args ...string) error {
		program := python
		argv := append([]string{filepath.Join(component, "tools", script)}, args...)
		if usb {
			program = "/usr/bin/sudo"
			argv = append([]string{"--", python}, argv...)
		}
		cmd := exec.Command(program, argv...)
		cmd.Dir = component
		cmd.Stdin, cmd.Stdout, cmd.Stderr = tty, tty, tty
		return cmd.Run()
	}
	return component, tty, run, nil
}

type usbToolRunner func(usb bool, script string, args ...string) error

type usbBackupCache struct {
	stored func(dataDir string) (string, error)
	save   func(dataDir, backup string) (string, error)
}

func confirmUSBStage(input *bufio.Reader, out io.Writer, action string) bool {
	fmt.Fprintf(out, "Type %q and press Return to continue: ", action)
	line, err := input.ReadString('\n')
	return err == nil && strings.TrimSpace(line) == action
}

const usbResetPrompt = "Wait for BREAK asserted, then power on or press and release RESET once. Keep power and USB connected until the tool exits."

func prepareUSBBackup(opts usbInstallOptions, input *bufio.Reader, out io.Writer, run usbToolRunner, cache usbBackupCache) (string, error) {
	source := opts.backup
	if source == "" {
		var err error
		source, err = cache.stored(opts.dataDir)
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			return "", fmt.Errorf("could not verify the original backup stored in the Hub: %w", err)
		}
		if errors.Is(err, os.ErrNotExist) {
			if opts.startAt != "provision-key" {
				return "", errors.New("resuming installation requires the brewer's original stock backup; supply --backup or select the original Hub --data directory")
			}
			fmt.Fprintln(out, "The Hub needs this brewer's original firmware. With only its CH340 adapter connected, start a read-only USB session to capture and compare two full flash reads.")
			if !confirmUSBStage(input, out, "backup") {
				return "", errors.New("stopped before USB backup")
			}
			if err := privatefs.EnsureDir(opts.dataDir); err != nil {
				return "", err
			}
			capture, err := os.MkdirTemp(opts.dataDir, "usb-capture-")
			if err != nil {
				return "", err
			}
			source = filepath.Join(capture, "original.bin")
			fmt.Fprintln(out, "Private backup and readback reports:", capture)
			fmt.Fprintln(out, usbResetPrompt)
			if err := run(true, "pico_flash_backup_v2.py", "--output", source); err != nil {
				return "", fmt.Errorf("stock capture did not complete; inspect %s: %w", capture, err)
			}
		}
	}
	fmt.Fprintf(out, "Preparing %s and verifying the original backup, application, website, recovery image and USB tools locally.\n", opts.model)
	if err := run(false, "pico_flash_install.py", "prepare-install", "--model", opts.model, "--backup", source); err != nil {
		return "", fmt.Errorf("USB preparation failed before installation: %w", err)
	}
	path, err := cache.save(opts.dataDir, source)
	if err != nil {
		return "", fmt.Errorf("saving verified original firmware in Hub data: %w", err)
	}
	fmt.Fprintln(out, "Verified original firmware in Hub data:", path)
	return path, nil
}

func installOverUSB(opts usbInstallOptions, component string, input io.Reader, out io.Writer, run usbToolRunner, cache usbBackupCache) error {
	reader := bufio.NewReader(input)
	backup, err := prepareUSBBackup(opts, reader, out, run, cache)
	if err != nil {
		return err
	}
	if _, err := os.Lstat(opts.keyFile); errors.Is(err, os.ErrNotExist) {
		if opts.startAt != "provision-key" {
			return errors.New("resuming installation requires the original private key file")
		}
		if err := run(false, "provision_api_key.py", "--output", opts.keyFile); err != nil {
			return fmt.Errorf("could not create the private host key: %w", err)
		}
	} else if err != nil {
		return fmt.Errorf("checking the host key file: %w", err)
	}
	reports, err := os.MkdirTemp(filepath.Dir(backup), "pico-usb-install-")
	if err != nil {
		return fmt.Errorf("creating a private reports directory beside the backup: %w", err)
	}
	fmt.Fprintf(out, "Installation reports: %s\nConnect only the reviewed brewer's CH340 adapter. Keep the idle brewer powered off, or ready for a manual reset.\n", reports)
	stages := []struct {
		command, purpose string
	}{
		{"provision-key", "Write and verify the device key on the stock brewer"},
		{"install", "Install and verify the standalone application and its website"},
		{"install-recovery", "Install and verify the separate OTA recovery image"},
	}
	started := false
	for _, stage := range stages {
		if stage.command == opts.startAt {
			started = true
		}
		if !started {
			continue
		}
		fmt.Fprintf(out, "\n%s (%s). ", stage.purpose, opts.model)
		if !confirmUSBStage(reader, out, stage.command) {
			return fmt.Errorf("stopped before %s; reports are in %s", stage.command, reports)
		}
		fmt.Fprintln(out, usbResetPrompt)
		args := []string{stage.command, "--verify-backup-uid", "--backup", backup, "--key-file", opts.keyFile,
			"--output", filepath.Join(reports, stage.command+".json")}
		if stage.command != "provision-key" {
			args = append(args, "--model", opts.model)
		}
		if stage.command == "install" {
			args = append(args, "--firmware", filepath.Join(component, "firmware", "standalone", opts.model, opts.model+"-standalone.bin"))
		}
		if err := run(true, "pico_flash_install.py", args...); err != nil {
			return fmt.Errorf("%s did not complete; inspect its report and journal before starting another session: %w", stage.command, err)
		}
	}
	fmt.Fprintln(out, "Recovery verified. Power off, disconnect USB, then boot normally. Read the brewer's IP address under Network → Status. Add it in the Hub with the same private key, and check its reported model, firmware version and OTA capability.")
	return nil
}
