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
	checkout, dataDir, backup string
}

func parseUSBRestoreOptions(args []string, out io.Writer) (usbRestoreOptions, error) {
	var opts usbRestoreOptions
	flags := flag.NewFlagSet("usb-restore", flag.ContinueOnError)
	flags.SetOutput(out)
	flags.Usage = func() {
		fmt.Fprintln(out, "Usage: picobrew-hub usb-restore [--data ./data] [--backup /path/to/original.bin]")
		fmt.Fprintln(out, "Restore the original application and recovery on the reviewed Pico S/Pro brewer over local USB.")
		flags.PrintDefaults()
	}
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
	dataDir, err := filepath.Abs(opts.dataDir)
	opts.dataDir = dataDir
	return opts, err
}

func runUSBRestoreCommand(args []string) error {
	opts, err := parseUSBRestoreOptions(args, os.Stderr)
	if err != nil {
		return err
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
	return restoreOverUSB(opts, tty, tty, run, stockfirmware.SaveBackup, resolve)
}

func restoreOverUSB(opts usbRestoreOptions, input io.Reader, out io.Writer, run usbToolRunner,
	save func(string, string) (string, error), resolve func(string) (stockfirmware.Source, error)) error {
	reader := bufio.NewReader(input)
	fmt.Fprintln(out, "Returning to the original PicoBrew firmware ends standalone operation, including its local website, REST API and Hub wireless updates. A future conversion back to standalone requires USB.")
	if !confirmUSBStage(reader, out, "use-stock") {
		return errors.New("stock restoration canceled")
	}
	if opts.backup != "" {
		if _, err := save(opts.dataDir, opts.backup); err != nil {
			return fmt.Errorf("could not import the specified original backup: %w", err)
		}
	}
	source, err := resolve(opts.dataDir)
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
	fmt.Fprintln(out, "Connect only the reviewed brewer's CH340 adapter. Confirm the brewer is idle and you want its original application and recovery written and read back.")
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
	args = append(args, "--output", filepath.Join(reports, "restore-stock.json"))
	if err := run(true, "pico_flash_install.py", args...); err != nil {
		return fmt.Errorf("stock restoration did not complete; inspect the report and journal before retrying: %w", err)
	}
	fmt.Fprintln(out, "Original application and recovery verified. Power off, disconnect USB, then boot normally and check the brewer's startup screen.")
	return nil
}
