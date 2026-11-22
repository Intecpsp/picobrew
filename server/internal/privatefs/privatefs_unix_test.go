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

func TestOpenSourcePreservesReadOnlyPermissions(t *testing.T) {
	path := filepath.Join(t.TempDir(), "original.bin")
	if err := os.WriteFile(path, []byte("read-only capture"), 0400); err != nil {
		t.Fatal(err)
	}
	f, err := OpenSource(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil || info.Mode().Perm() != 0400 {
		t.Fatal("external file permissions were changed", info, err)
	}
	link := path + ".link"
	if err := os.Symlink(path, link); err != nil {
		t.Fatal(err)
	}
	if other, err := OpenSource(link); err == nil {
		other.Close()
		t.Fatal("opened a symlink in place of the original")
	}
}
