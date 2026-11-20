package firmware

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestPublishedVersionArchives(t *testing.T) {
	root := os.Getenv("PICO_FIRMWARE_LIBRARY")
	if root == "" {
		t.Skip("repository version library is outside the standalone Hub")
	}
	data, err := os.ReadFile(filepath.Join(root, "releases.json"))
	if err != nil {
		t.Fatal(err)
	}
	var catalog struct {
		Releases []Release `json:"releases"`
	}
	if err = json.Unmarshal(data, &catalog); err != nil {
		t.Fatal(err)
	}
	current := map[string]bool{}
	for _, r := range catalog.Releases {
		path := filepath.Join(root, r.ModelFamily, "modified", "releases", r.Model, r.DisplayVariant, r.FirmwareVersion+".zip")
		data, err = os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		if err = ValidateRelease(data, r); err != nil {
			t.Fatalf("%s: %s", path, err)
		}
		if ValidModel(r.Model) {
			report, err := os.ReadFile(filepath.Join(root, r.ModelFamily, "modified", "firmware", "standalone", r.Model, r.DisplayVariant, "firmware-report.json"))
			if err != nil {
				t.Fatal(err)
			}
			parsed, err := parseReport(report)
			if err != nil {
				t.Fatal(err)
			}
			if candidate(parsed) == r.Candidate {
				current[r.Model+r.DisplayVariant] = true
			}
		}
	}
	for _, v := range []string{"pico-s", "pico-pro", "pico-cssd1309", "pico-cssd1325"} {
		if !current[v] {
			t.Fatalf("catalog has no current %s package", v)
		}
	}
}
