package firmware

import (
	"context"
	"encoding/json"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware/firmwaretest"
	"net/http"
	"strings"
	"testing"
)

func TestCPackageRequiresModelControllerAndGeometry(t *testing.T) {
	for _, display := range []string{"ssd1325", "ssd1309"} {
		files := firmwaretest.CFiles(display)
		got, err := ValidateArchive(firmwaretest.Archive(files))
		if err != nil || got.Model != "pico-c" || got.ModelFamily != "pico-c" || got.DisplayVariant != display {
			t.Fatalf("C package: %+v %v", got, err)
		}
	}
	for _, test := range []struct {
		name string
		edit func(map[string]any)
	}{
		{"missing variant", func(r map[string]any) { delete(r, "display_variant") }},
		{"wrong variant", func(r map[string]any) { r["display_variant"] = "ssd1309" }},
		{"wrong family", func(r map[string]any) { r["model_family"] = "pico-s-pro" }},
		{"inferred dimensions", func(r map[string]any) { r["display_variant"] = "128x64" }},
		{"no explicit rotation", func(r map[string]any) { delete(r["display_geometry"].(map[string]any), "rotation") }},
		{"bad storage", func(r map[string]any) { r["display_geometry"].(map[string]any)["storage_bytes"] = 512 }},
		{"claims installer", func(r map[string]any) { r["firmware_update"] = true }},
	} {
		t.Run(test.name, func(t *testing.T) {
			files := firmwaretest.CFiles("ssd1325")
			var report map[string]any
			if err := json.Unmarshal(files["firmware-report.json"], &report); err != nil {
				t.Fatal(err)
			}
			test.edit(report)
			files["firmware-report.json"] = firmwaretest.JSON(report)
			if _, err := ValidateArchive(firmwaretest.Archive(files)); err == nil {
				t.Fatal("accepted inconsistent C package")
			}
		})
	}
}

func TestCGitHubCatalogAndImportKeepDisplayPathsDistinct(t *testing.T) {
	commit := strings.Repeat("a", 40)
	contents := map[string][]byte{}
	for _, display := range []string{"ssd1325", "ssd1309"} {
		for name, data := range firmwaretest.CFiles(display) {
			contents[rawPath("owner/repo", commit, "pico-c", name, display)] = data
		}
	}
	calls := 0
	client := &GitHub{HTTP: &http.Client{Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
		calls++
		if r.URL.Host == "api.github.com" {
			return githubResponse(200, []byte(`{"sha":"`+commit+`"}`)), nil
		}
		data, ok := contents[r.URL.String()]
		if !ok {
			return githubResponse(404, nil), nil
		}
		return githubResponse(200, data), nil
	})}}
	catalog, err := client.Catalog(context.Background(), Settings{Repository: "owner/repo", Ref: "main"})
	if err != nil || len(catalog.Packages) != 2 || catalog.Packages[0].DisplayVariant != "ssd1325" || catalog.Packages[1].DisplayVariant != "ssd1309" {
		t.Fatalf("C catalog: %+v %v", catalog, err)
	}
	for _, display := range []string{"ssd1325", "ssd1309"} {
		raw, got, err := client.Import(context.Background(), "owner/repo", commit, "pico-c", display)
		if err != nil || got.DisplayVariant != display {
			t.Fatalf("C import: %+v %v", got, err)
		}
		if checked, err := ValidateArchive(raw); err != nil || checked != got {
			t.Fatalf("C import did not validate: %+v %v", checked, err)
		}
	}
	before := calls
	for _, selection := range [][2]string{{"pico-c", ""}, {"pico-c", "ssd1306"}, {"pico-s", "ssd1309"}} {
		if _, _, err := client.Import(context.Background(), "owner/repo", commit, selection[0], selection[1]); err == nil {
			t.Fatal("accepted ambiguous controller selector")
		}
	}
	if calls != before {
		t.Fatal("invalid C selector reached the network")
	}
	contents[rawPath("owner/repo", commit, "pico-c", "firmware-report.json", "ssd1309")] = firmwaretest.CFiles("ssd1325")["firmware-report.json"]
	if _, _, err := client.Import(context.Background(), "owner/repo", commit, "pico-c", "ssd1309"); err == nil {
		t.Fatal("accepted a report from the other controller path")
	}
}
