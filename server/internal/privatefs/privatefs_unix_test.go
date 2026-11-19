//go:build !windows

package privatefs

import (
	"os"
	"path/filepath"
	"testing"
)

func TestUnixModesAndNoFollow(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private")
	if err := EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(dir)
	if err != nil || info.Mode().Perm() != 0700 {
		t.Fatalf("directory mode: %v %v", info, err)
	}
	key := filepath.Join(dir, "key")
	f, err := OpenFile(key, os.O_CREATE|os.O_EXCL|os.O_RDWR)
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
	info, err = os.Stat(key)
	if err != nil || info.Mode().Perm() != 0600 {
		t.Fatalf("file mode: %v %v", info, err)
	}
	link := filepath.Join(dir, "link")
	if err := os.Symlink(key, link); err != nil {
		t.Fatal(err)
	}
	if data, err := ReadFile(link, 32); err == nil {
		t.Fatalf("followed symlink: %q", data)
	}
}
