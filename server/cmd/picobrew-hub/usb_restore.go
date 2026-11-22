package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
)

type usbRestoreOptions struct {
	checkout, dataDir, backup, model, display, port, device, oemFile string
}

func parseUSBRestoreOptions(args []string, out io.Writer) (usbRestoreOptions, error) {
	var opts usbRestoreOptions
	flags := flag.NewFlagSet("usb-restore", flag.ContinueOnError)
	flags.SetOutput(out)
	flags.Usage = func() {
		fmt.Fprintln(out, "Usage: picobrew-hub usb-restore --model MODEL [model options] --data ./data")
		fmt.Fprintln(out, "Without --model, the legacy restoration command selects the originally reviewed S/Pro brewer.")
		flags.PrintDefaults()
	}
	flags.StringVar(&opts.model, "model", "", "Physical brewer or module model")
	flags.StringVar(&opts.port, "port", "", "ESP local serial port")
	flags.StringVar(&opts.display, "display", "", "Pico C display controller")
	flags.StringVar(&opts.device, "device", "", "CC3200 brewer UID for identifying its saved original")
	flags.StringVar(&opts.oemFile, "oem-file", "", "Original vendor Zymatic 1.1.14 HEX or ZIP (absolute path)")
	flags.StringVar(&opts.checkout, "checkout", "", "Full firmware checkout (defaults to the checkout containing the current directory)")
	flags.StringVar(&opts.dataDir, "data", "data", "Persistent data directory used by your Hub")
	flags.StringVar(&opts.backup, "backup", "", "Import this brewer's original full flash backup into the Hub first (absolute path)")
	if err := flags.Parse(args); err != nil {
		return opts, err
	}
	if flags.NArg() != 0 || opts.dataDir == "" || (opts.backup != "" && !filepath.IsAbs(opts.backup)) {
		flags.Usage()
		return opts, errors.New("supply a Hub data directory and an absolute path when importing a backup")
	}
	if opts.model == "" {
		if opts.display != "" || opts.port != "" || opts.device != "" || opts.oemFile != "" {
			return opts, errors.New("supply --model with model-specific options")
		}
	} else if err := checkUSBTarget(opts.model, opts.display, opts.port, opts.device, "restore"); err != nil {
		return opts, err
	}
	if opts.oemFile != "" && (opts.model != "zymatic" || !filepath.IsAbs(opts.oemFile)) {
		return opts, errors.New("--oem-file requires Zymatic and an absolute vendor HEX/ZIP path")
	}
	if opts.model != "" && !isCC(opts.model) && opts.backup != "" {
		return opts, errors.New("--backup selects a CC3200 capture; ESP restoration uses the connected device's saved original")
	}
	dataDir, err := filepath.Abs(opts.dataDir)
	opts.dataDir = dataDir
	return opts, err
}

func runUSBRestoreCommand(args []string) error {
	opts, err := parseUSBRestoreOptions(args, os.Stderr)
	if err != nil {
		return err
	}
	if opts.model != "" && !isCC(opts.model) {
		return runFamilyUSB("restore", opts.checkout, opts.dataDir, opts.model, opts.port, opts.oemFile)
	}
	_, tty, run, err := openUSBTools(opts.checkout)
	if err != nil {
		return err
	}
	defer tty.Close()
	github := firmware.NewGitHub()
	defer github.Close()
	resolve := func(dir string) (stockfirmware.Source, error) {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
		defer cancel()
		return stockfirmware.Resolve(ctx, dir, github)
	}
	if opts.model != "" {
		cache, err := ccCache(opts.checkout, opts.model, opts.display, opts.device, false)
		if err != nil {
			return err
		}
		resolveOriginal := func(dir string) (stockfirmware.Source, error) {
			path, err := cache.stored(dir)
			if (errors.Is(err, os.ErrNotExist) || errors.Is(err, stockfirmware.ErrIntegrity)) && opts.model != "pico-c" && stockfirmware.HasPublicSlots(opts.device) {
				return resolve(dir)
			}
			if errors.Is(err, os.ErrNotExist) {
				return stockfirmware.Source{}, errors.New("no original for this brewer in Hub data; supply its complete stock capture with --backup")
			}
			if err != nil {
				return stockfirmware.Source{}, err
			}
			saved, err := cache.save(dir, path)
			return stockfirmware.Source{Backup: saved, Description: "This brewer's verified original full-flash backup in Hub data"}, err
		}
		return restoreOverUSB(opts, tty, tty, run, cache.save, resolveOriginal)
	}
	return restoreOverUSB(opts, tty, tty, run, stockfirmware.SaveBackup, resolve)
}

func restoreOverUSB(opts usbRestoreOptions, input io.Reader, out io.Writer, run usbToolRunner,
	save func(string, string) (string, error), resolve func(string) (stockfirmware.Source, error)) error {
	reader := bufio.NewReader(input)
	fmt.Fprintln(out, "Returning to the original PicoBrew firmware ends standalone operation, including its local website, REST API and Hub wireless updates. A future conversion back to standalone requires USB.")
	if !confirmUSBStage(reader, out, "use-stock") {
		return errors.New("stock restoration canceled")
	}
	var source stockfirmware.Source
	var err error
	if opts.backup != "" {
		source.Backup, err = save(opts.dataDir, opts.backup)
		if err != nil {
			return fmt.Errorf("could not import the specified original backup: %w", err)
		}
		source.Description = "This brewer's verified original full-flash backup in Hub data"
	} else {
		source, err = resolve(opts.dataDir)
	}
	if err != nil {
		return fmt.Errorf("could not obtain verified original firmware: %w", err)
	}
	var sourceArgs []string
	switch {
	case filepath.IsAbs(source.Backup) && source.OEMDirectory == "":
		sourceArgs = []string{"--backup", source.Backup}
	case filepath.IsAbs(source.OEMDirectory) && source.Backup == "":
		sourceArgs = []string{"--oem-directory", source.OEMDirectory}
	default:
		return errors.New("expected one verified original firmware source")
	}
	fmt.Fprintln(out, "Original firmware source:", source.Description)
	if opts.model == "pico-c" {
		fmt.Fprintln(out, "Connect only this idle brewer’s CH340 adapter. Confirm restoration of its original application and verification of its retained recovery.")
	} else {
		fmt.Fprintln(out, "Connect only this idle brewer’s CH340 adapter. Confirm restoration and readback of its original application and recovery.")
	}
	if !confirmUSBStage(reader, out, "restore-stock") {
		return errors.New("stopped before USB restoration")
	}
	if err := privatefs.EnsureDir(opts.dataDir); err != nil {
		return err
	}
	reports, err := os.MkdirTemp(opts.dataDir, "usb-restore-")
	if err != nil {
		return fmt.Errorf("creating the private restore reports directory: %w", err)
	}
	fmt.Fprintln(out, "Private restore report and journal:", reports)
	fmt.Fprintln(out, usbResetPrompt)
	args := append([]string{"restore-stock"}, sourceArgs...)
	if opts.model != "" && source.Backup != "" {
		args = append(args, targetArgs(opts.model, opts.display)...)
	}
	args = append(args, "--output", filepath.Join(reports, "restore-stock.json"))
	if err := run(true, "pico_flash_install.py", args...); err != nil {
		return fmt.Errorf("stock restoration did not complete; inspect the report and journal before retrying: %w", err)
	}
	fmt.Fprintln(out, "Original application and recovery verified. Power off, disconnect USB, then boot normally and check the brewer's startup screen.")
	return nil
}
