//go:build !windows

package helper

import (
	"errors"
	"os"
	"syscall"
)

func checkDirectory(path string) error {
	info, err := os.Lstat(path)
	if err != nil {
		return err
	}
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !info.IsDir() || info.Mode().Perm()&0077 != 0 || !ok || int(stat.Uid) != os.Geteuid() {
		return errors.New("USB worker cache needs a private directory owned by the Hub account")
	}
	return nil
}
