//go:build !windows

package server

import (
	"errors"
	"os"

	"golang.org/x/sys/unix"
)

func tryLockFirmwareCache(file *os.File) error {
	err := unix.Flock(int(file.Fd()), unix.LOCK_EX|unix.LOCK_NB)
	if errors.Is(err, unix.EWOULDBLOCK) || errors.Is(err, unix.EAGAIN) {
		return errFirmwareCacheLocked
	}
	return err
}
