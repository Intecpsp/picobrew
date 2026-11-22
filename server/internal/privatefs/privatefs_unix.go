//go:build !windows

package privatefs

import (
	"fmt"
	"os"

	"golang.org/x/sys/unix"
)

// EnsureDir preserves existing Unix directory modes, as before. Newly created
// directories are private. Normal OS aliases such as macOS /var remain usable.
func EnsureDir(path string) error { return os.MkdirAll(path, 0700) }

// OpenFile rejects a final symlink or nonregular file. Nonblocking open avoids
// waiting forever on a FIFO supplied in place of a key/database.
func OpenFile(path string, flags int) (*os.File, error) {
	f, err := openRegular(path, flags)
	if err != nil {
		return nil, err
	}
	if err = f.Chmod(0600); err != nil {
		f.Close()
		return nil, err
	}
	return f, nil
}

// OpenSource opens a user-supplied file without changing its permissions.
func OpenSource(path string) (*os.File, error) {
	return openRegular(path, os.O_RDONLY)
}

func openRegular(path string, flags int) (*os.File, error) {
	fd, err := unix.Open(path, flags|unix.O_NOFOLLOW|unix.O_NONBLOCK|unix.O_CLOEXEC, 0600)
	if err != nil {
		return nil, &os.PathError{Op: "open private file", Path: path, Err: err}
	}
	f := os.NewFile(uintptr(fd), path)
	info, err := f.Stat()
	if err == nil && !info.Mode().IsRegular() {
		err = fmt.Errorf("private file must be a regular file")
	}
	if err != nil {
		f.Close()
		return nil, err
	}
	return f, nil
}
