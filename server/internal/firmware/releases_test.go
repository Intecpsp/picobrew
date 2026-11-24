package firmware

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
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
	listed := map[string]bool{}
	for _, r := range catalog.Releases {
		path := filepath.Join(root, r.ModelFamily, "modified", "releases", r.Model, r.DisplayVariant, r.FirmwareVersion+".zip")
		if listed[path] {
			t.Fatalf("duplicate package path %s", path)
		}
		listed[path] = true
		data, err = os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		if err = ValidateRelease(data, r); err != nil {
			t.Fatalf("%s: %s", path, err)
		}
		if r.SupportsFermOTA() || r.SupportsFactoryOTA() {
			current[r.Model] = true
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
	for _, v := range []string{"pico-s", "pico-pro", "pico-cssd1309", "pico-cssd1325", "z-series", "picoferm", "picostill"} {
		if !current[v] {
			t.Fatalf("catalog has no current %s package", v)
		}
	}
	releaseDirs, err := filepath.Glob(filepath.Join(root, "*", "modified", "releases"))
	if err != nil {
		t.Fatal(err)
	}
	for _, dir := range releaseDirs {
		err := filepath.WalkDir(dir, func(path string, entry fs.DirEntry, err error) error {
			if err == nil && !entry.IsDir() && strings.HasSuffix(path, ".zip") && !listed[path] {
				t.Errorf("unlisted firmware archive %s", path)
			}
			return err
		})
		if err != nil {
			t.Fatal(err)
		}
	}

}

func TestWithdrawnLibraryPackage(t *testing.T) {
	source := Settings{Repository: "owner/repo"}
	c := ReleaseCatalog{Settings: source, ReleaseChannel: "stable", Releases: []Release{{ArchiveSHA256: "current"}}}
	p := Package{ID: "old", Provenance: Provenance{Source: "library", Repository: source.Repository}}
	if !c.Withdrawn(p) {
		t.Fatal("old library release is still offered")
	}
	for _, provenance := range []Provenance{{Source: "upload"}, {Source: "library", Repository: "another/repo"}} {
		other := p
		other.Provenance = provenance
		if c.Withdrawn(other) {
			t.Fatal("suppressed package from a different source", provenance)
		}
	}
	p.ID = "current"
	if c.Withdrawn(p) {
		t.Fatal("suppressed current release")
	}
}
