package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func TestStagingRejectsMismatchedBytesAndKeepsVerifiedWorker(t *testing.T) {
	input, output := t.TempDir(), t.TempDir()
	host := runtime.GOOS + "-" + runtime.GOARCH
	name := "picobrew-usb-" + host
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	worker := filepath.Join(input, name)
	metadata := filepath.Join(input, host+".json")
	write := func(path string, data []byte) {
		t.Helper()
		if err := os.WriteFile(path, data, 0600); err != nil {
			t.Fatal(err)
		}
	}
	manifest := func(content []byte) {
		t.Helper()
		digest := sha256.Sum256(content)
		data, err := json.Marshal(Asset{Platform: host, Name: name, Bytes: int64(len(content)), SHA256: hex.EncodeToString(digest[:])})
		if err != nil {
			t.Fatal(err)
		}
		write(metadata, data)
	}
	original := []byte("first worker")
	write(worker, original)
	manifest(original)
	if err := stage(input, output); err != nil {
		t.Fatal(err)
	}
	manifest([]byte("next worker"))
	write(worker, []byte("bad! worker"))
	if err := stage(input, output); err == nil {
		t.Fatal("accepted worker with an incorrect release digest")
	}
	data, err := os.ReadFile(filepath.Join(output, "worker"))
	if err != nil || string(data) != string(original) {
		t.Fatalf("verified worker changed: %v", err)
	}

	// A fresh checkout needs its input once; subsequent builds use the
	// already verified payload even if the local build input is gone.
	manifest(original)
	if err := os.Remove(worker); err != nil {
		t.Fatal(err)
	}
	if err := stage(input, output); err != nil {
		t.Fatal(err)
	}
}
