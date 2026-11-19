package privatefs

import (
	"errors"
	"os"
	"path/filepath"
	"testing"
)

func TestPrivateFileRoundTripAndExclusiveCreation(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private", "nested")
	if err := EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(dir, "master.key")
	f, err := OpenFile(path, os.O_RDWR|os.O_CREATE|os.O_EXCL)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.Write([]byte("synthetic key")); err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	if other, err := OpenFile(path, os.O_RDWR|os.O_CREATE|os.O_EXCL); !errors.Is(err, os.ErrExist) {
		if other != nil {
			other.Close()
		}
		t.Fatalf("exclusive creation: %v", err)
	}
	data, err := ReadFile(path, 32)
	if err != nil || string(data) != "synthetic key" {
		t.Fatalf("read %q: %v", data, err)
	}
	data, err = ReadFile(path, 2)
	if err != nil || string(data) != "syn" {
		t.Fatalf("bounded read %q: %v", data, err)
	}
	if f, err := OpenFile(dir, os.O_RDONLY); err == nil {
		f.Close()
		t.Fatal("accepted a directory as a private file")
	}
}
