package helper

import (
	"context"
	"encoding/json"
	"os"
	"testing"
	"time"
)

func TestEmbeddedWorkerSelfTest(t *testing.T) {
	if !Available() {
		if os.Getenv("PICOBREW_REQUIRE_USB_WORKER") == "1" {
			t.Fatal("packaged Hub is missing its USB worker")
		}
		t.Skip("source-only build")
	}
	dir := t.TempDir()
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Second)
	defer cancel()
	cmd, err := Command(ctx, dir)
	if err != nil {
		t.Fatal(err)
	}
	cmd.Args = append(cmd.Args, "--self-test")
	data, err := cmd.Output()
	if err != nil {
		t.Fatalf("worker self-test: %v, %s", err, data)
	}
	var event struct{ Type string }
	if err = json.Unmarshal(data, &event); err != nil || event.Type != "complete" {
		t.Fatalf("invalid worker reply: %s", data)
	}
	if err = os.Chmod(cmd.Path, 0600); err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(cmd.Path, []byte("broken"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err = Command(ctx, dir); err == nil {
		t.Fatal("accepted corrupted executable")
	}
}
