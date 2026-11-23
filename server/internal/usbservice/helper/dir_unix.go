//go:build !windows

package helper

import (
	"errors"
	"os"
	"path/filepath"
	"syscall"
)

func checkDirectory(path string) error {
	cache, err := os.Lstat(path)
	if err != nil {
		return err
	}
	stat, ok := cache.Sys().(*syscall.Stat_t)
	if !cache.IsDir() || cache.Mode().Perm()&0077 != 0 || !ok || int(stat.Uid) != os.Geteuid() {
		return errors.New("USB worker cache needs a private directory owned by the Hub account")
	}

	// Resolve normal OS aliases such as /var on macOS, then ensure another
	// account cannot replace a verified executable by renaming an ancestor.
	current, err := filepath.EvalSymlinks(filepath.Dir(path))
	if err != nil {
		return err
	}
	for {
		info, err := os.Stat(current)
		if err != nil {
			return err
		}
		owner, ok := info.Sys().(*syscall.Stat_t)
		trusted := ok && (owner.Uid == 0 || int(owner.Uid) == os.Geteuid())
		// Sticky shared directories protect entries owned by the Hub account
		// or root. Every entry along this path is checked for that ownership.
		if !info.IsDir() || !trusted || (info.Mode().Perm()&0022 != 0 && info.Mode()&os.ModeSticky == 0) {
			return errors.New("the USB worker requires a Hub data path protected against writes by other accounts")
		}
		parent := filepath.Dir(current)
		if parent == current {
			return nil
		}
		current = parent
	}
}
