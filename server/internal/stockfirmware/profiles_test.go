package stockfirmware

import (
	"bytes"
	"crypto/sha256"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestProfileStockSelectionAndIntegrity(t *testing.T) {
	dir := t.TempDir()
	a := Profile{Model: "pico-c", Display: "ssd1309", UIDHash: strings.Repeat("1", 64)}
	other := a
	other.UIDHash = strings.Repeat("2", 64)
	var saved []string
	for i, p := range []*Profile{&a, &other} {
		b := bytes.Repeat([]byte{byte(i + 1)}, 1<<20)
		p.BackupHash = fmt.Sprintf("%x", sha256.Sum256(b))
		src := filepath.Join(dir, "external.bin")
		if err := os.WriteFile(src, b, 0600); err != nil {
			t.Fatal(err)
		}
		path, err := SaveProfile(dir, src, *p)
		if err != nil {
			t.Fatal(err)
		}
		if repeat, err := SaveProfile(dir, src, *p); err != nil || repeat != path {
			t.Fatalf("reimport %s, %v", repeat, err)
		}
		saved = append(saved, path)
	}
	if _, err := StoredProfile(dir, a.Model, a.Display, ""); err == nil {
		t.Fatal("ambiguous brewer selected")
	}
	if p, err := StoredProfile(dir, a.Model, a.Display, a.UIDHash); err != nil || p != saved[0] {
		t.Fatalf("wrong brewer %s, %v", p, err)
	}
	if _, err := StoredProfile(dir, a.Model, "ssd1325", a.UIDHash); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("other display", err)
	}
	if _, err := StoredProfile(dir, "pico-pro", "", a.UIDHash); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("other family", err)
	}
	if _, err := SaveProfile(dir, saved[0], other); !errors.Is(err, ErrIntegrity) {
		t.Fatal("source changed since inspection", err)
	}
	if err := os.WriteFile(saved[0], []byte{1}, 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := StoredProfile(dir, a.Model, a.Display, a.UIDHash); !errors.Is(err, ErrIntegrity) {
		t.Fatal("damaged backup accepted", err)
	}
	for _, invalid := range []Profile{{Model: "../path", UIDHash: a.UIDHash, BackupHash: a.BackupHash}, {Model: "pico-c", Display: "../../", UIDHash: a.UIDHash, BackupHash: a.BackupHash}, {Model: "pico-s", UIDHash: "../", BackupHash: a.BackupHash}} {
		if _, err := SaveProfile(dir, saved[0], invalid); err == nil {
			t.Fatal("invalid identifiers accepted")
		}
	}
}
