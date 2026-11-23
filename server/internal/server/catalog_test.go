package server

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"slices"
	"strings"
	"syscall"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/catalog"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestCatalogBrowseCopyAndOriginal(t *testing.T) {
	s, h, c := newTestServer(t)
	status, _ := request(t, c, "GET", h.URL+"/api/v1/catalog", "", nil, nil)
	if status != 401 {
		t.Fatalf("unauthenticated catalogue: %d", status)
	}
	csrf := signIn(t, c, h.URL)
	status, body := request(t, c, "GET", h.URL+"/api/v1/catalog?kind=archive&source=picopak-library&model=pico-pro&limit=2", "", nil, nil)
	var page catalog.Page
	if status != 200 || json.Unmarshal(body, &page) != nil || len(page.Items) != 2 || page.Total <= 2 {
		t.Fatalf("browse %d: %s", status, body)
	}
	status, body = request(t, c, "GET", h.URL+"/api/v1/catalog/picopak-64", "", nil, nil)
	var detail catalog.Detail
	if status != 200 || json.Unmarshal(body, &detail) != nil || detail.Recipe == nil {
		t.Fatalf("detail %d: %s", status, body)
	}
	status, _ = request(t, c, "POST", h.URL+"/api/v1/catalog/picopak-64/copy", "", map[string]any{}, nil)
	if status != 403 {
		t.Fatalf("copy without csrf: %d", status)
	}
	status, body = request(t, c, "POST", h.URL+"/api/v1/catalog/picopak-64/copy", csrf, map[string]any{}, nil)
	var copied domain.Recipe
	if status != 201 || json.Unmarshal(body, &copied) != nil {
		t.Fatalf("copy %d: %s", status, body)
	}
	if copied.ID == "" || copied.Revision != 1 || !reflect.DeepEqual(copied.NativeProgram, detail.NativeProgram) || copied.Provenance.CatalogID != detail.ID || copied.BatchLiters != 0 || len(copied.Ingredients) != 0 {
		t.Fatalf("copy changed original: %s", body)
	}
	var saved domain.Recipe
	if _, err := s.db.Get("recipe", copied.ID, &saved); err != nil || !reflect.DeepEqual(saved, copied) {
		t.Fatal("copy not saved", err)
	}
	var revision domain.Recipe
	if _, err := s.db.Get("recipe-revision", copied.ID+":1", &revision); err != nil {
		t.Fatal("revision not saved", err)
	}
	status, body = request(t, c, "POST", h.URL+"/api/v1/catalog/picopak-64/copy", csrf, map[string]any{}, nil)
	var second domain.Recipe
	if status != 201 || json.Unmarshal(body, &second) != nil || second.ID == copied.ID {
		t.Fatalf("repeat copy overwrote library %d: %s", status, body)
	}
	if _, err := s.db.Get("recipe", copied.ID, &saved); err != nil || !reflect.DeepEqual(saved, copied) {
		t.Fatal("source copy changed existing library", err)
	}
	rows, err := s.db.List("deployment")
	if err != nil || len(rows) != 0 {
		t.Fatal("catalogue copy deployed a program")
	}
	status, body = request(t, c, "GET", h.URL+"/api/v1/catalog/picopak-64/source", "", nil, nil)
	if status != 200 || catalog.Verify(body, detail.SourceRecords[0].Bytes, detail.SourceRecords[0].SHA256) != nil {
		t.Fatalf("source changed %d: %s", status, body)
	}
	status, _ = request(t, c, "GET", h.URL+"/api/v1/catalog/picopak-64/source?record=17", "", nil, nil)
	if status != 404 {
		t.Fatalf("unlisted original accepted %d", status)
	}
	for _, url := range []string{"/api/v1/catalog?limit=0", "/api/v1/catalog?limit=101", "/api/v1/catalog?offset=-1", "/api/v1/catalog?offset=bad"} {
		status, _ = request(t, c, "GET", h.URL+url, "", nil, nil)
		if status != 400 {
			t.Fatalf("bad filter %s: %d", url, status)
		}
	}
}

func TestCatalogReviewedFilter(t *testing.T) {
	_, h, c := newTestServer(t)
	signIn(t, c, h.URL)
	base := h.URL + "/api/v1/catalog"
	for _, tc := range []struct {
		search  string
		id      string
		blocked bool
	}{
		{"Mow+the+Damn+Lawn", "public-cb4320ef48424f71a65bb720b139c0c6", false},
		{"Chill+Cycle", "public-02a538894b06404da7793ff10fe99788", true},
	} {
		status, body := request(t, c, "GET", base+"?kind=reviewed&model=zymatic&source=public-recipes&q="+tc.search, "", nil, nil)
		var page catalog.Page
		if status != 200 || json.Unmarshal(body, &page) != nil || !slices.ContainsFunc(page.Items, func(item catalog.Summary) bool { return item.ID == tc.id }) {
			t.Fatalf("reviewed search missing %s: %s", tc.id, body)
		}
		status, body = request(t, c, "GET", base+"/"+tc.id, "", nil, nil)
		var detail catalog.Detail
		if status != 200 || json.Unmarshal(body, &detail) != nil || detail.SourceReview == nil || detail.SourceReview.Evidence == "" || (detail.SourceReview.Blocker != "") != tc.blocked {
			t.Fatalf("incorrect review for %s", tc.id)
		}
	}
	status, body := request(t, c, "GET", base+"?kind=reviewed&source=picopak-library", "", nil, nil)
	var page catalog.Page
	if status != 200 || json.Unmarshal(body, &page) != nil || page.Total != 0 {
		t.Fatal("unreviewed source returned in reviewed archive")
	}
}

func TestCatalogReferencesAndUnsupportedCopy(t *testing.T) {
	_, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	status, body := request(t, c, "GET", h.URL+"/api/v1/catalog?kind=archive&source=chiefwigms&model=pico-pro", "", nil, nil)
	var page catalog.Page
	if status != 200 || json.Unmarshal(body, &page) != nil || len(page.Items) == 0 {
		t.Fatalf("chief sources %d: %s", status, body)
	}
	for _, item := range page.Items {
		if item.CanCopy {
			t.Fatalf("unverified Pico named location mapping copied: %s", item.ID)
		}
		status, _ = request(t, c, "POST", h.URL+"/api/v1/catalog/"+item.ID+"/copy", csrf, map[string]any{}, nil)
		if status != 422 {
			t.Fatalf("reference copied %s: %d", item.ID, status)
		}
	}
	status, body = request(t, c, "GET", h.URL+"/api/v1/catalog/references?model=pico-pro", "", nil, nil)
	var refs catalog.References
	if status != 200 || json.Unmarshal(body, &refs) != nil || len(refs.Items) == 0 {
		t.Fatalf("references %d: %s", status, body)
	}
	foundBundled, foundOptional := false, false
	for _, ref := range refs.Items {
		status, data := request(t, c, "GET", h.URL+ref.URL, "", nil, nil)
		if ref.Bundled {
			foundBundled = true
			sum := sha256.Sum256(data)
			if status != 200 || !ref.Available || hex.EncodeToString(sum[:]) != ref.SHA256 {
				t.Fatalf("bundled reference %s: %d", ref.ID, status)
			}
		} else {
			foundOptional = true
			if status != 404 || ref.Available {
				t.Fatalf("absent PDF advertised %s: %d", ref.ID, status)
			}
		}
	}
	if !foundBundled || !foundOptional {
		t.Fatal("reference fixtures incomplete")
	}
	for _, asset := range refs.Assets {
		if !asset.Bundled || asset.ContentType != "image/svg+xml" {
			continue
		}
		resp, err := c.Get(h.URL + asset.URL)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		if resp.StatusCode != 200 || resp.Header.Get("Content-Type") != "image/svg+xml" || resp.Header.Get("X-Content-Type-Options") != "nosniff" {
			t.Fatal("asset headers", resp.StatusCode, resp.Header)
		}
		break
	}
	status, _ = request(t, c, http.MethodGet, h.URL+"/api/v1/catalog/assets/not-in-manifest", "", nil, nil)
	if status != 404 {
		t.Fatalf("unlisted asset: %d", status)
	}
}

func TestOptionalReferenceRequiresManifestBytesAndContainedPath(t *testing.T) {
	s, _, _ := newTestServer(t)
	root := filepath.Join(s.cfg.DataDir, "catalog-references")
	if err := os.MkdirAll(filepath.Join(root, "support"), 0700); err != nil {
		t.Fatal(err)
	}
	data := []byte("%PDF-verified-test-source")
	sum := sha256.Sum256(data)
	asset := catalog.Asset{Path: "support/manual.pdf", Bytes: int64(len(data)), SHA256: hex.EncodeToString(sum[:])}
	file := filepath.Join(root, asset.Path)
	if err := os.WriteFile(file, data, 0600); err != nil {
		t.Fatal(err)
	}
	if got, err := s.optionalCatalogAsset(asset); err != nil || string(got) != string(data) {
		t.Fatal("valid local reference rejected", err)
	}
	if err := os.WriteFile(file, []byte("%PDF-modified-test-source"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := s.optionalCatalogAsset(asset); err == nil {
		t.Fatal("substituted reference accepted")
	}
	if err := os.Remove(file); err != nil {
		t.Fatal(err)
	}
	out := filepath.Join(t.TempDir(), "outside.pdf")
	if err := os.WriteFile(out, data, 0600); err != nil {
		t.Fatal(err)
	}
	t.Run("symlink cannot escape the reference root", func(t *testing.T) {
		if err := os.Symlink(out, file); err != nil {
			// Windows allows this only with Developer Mode or symlink privilege.
			const privilegeNotHeld = syscall.Errno(1314) // ERROR_PRIVILEGE_NOT_HELD
			if runtime.GOOS == "windows" && (errors.Is(err, os.ErrPermission) || errors.Is(err, privilegeNotHeld)) {
				t.Skipf("Windows symlink privilege is unavailable: %v", err)
			}
			t.Fatal(err)
		}
		if _, err := s.optionalCatalogAsset(asset); err == nil {
			t.Fatal("symlink escaped reference root")
		}
	})
}

func TestCatalogStockProgramCopyPreviewsWithoutRoutingInput(t *testing.T) {
	_, h, c := newTestServer(t)
	token := signIn(t, c, h.URL)
	base := h.URL + "/api/v1"
	code, body := request(t, c, "GET", base+"/catalog?kind=brew-ready&limit=1", "", nil, nil)
	var page catalog.Page
	if code != 200 || json.Unmarshal(body, &page) != nil || len(page.Items) != 1 || !page.Items[0].BrewReady {
		t.Fatal(code, string(body))
	}
	code, body = request(t, c, "POST", base+"/catalog/"+page.Items[0].ID+"/copy", token, map[string]bool{"stock_program": true}, nil)
	var saved domain.Recipe
	if code != 201 || json.Unmarshal(body, &saved) != nil || saved.NativeProgram == nil || saved.SourceModel != "zymatic" {
		t.Fatal(code, string(body))
	}
	code, body = request(t, c, "POST", base+"/stock-zymatics/TEST123/recipes", token, stockSubmission{Recipes: []stockReference{{RecipeID: saved.ID, Revision: saved.Revision}}}, nil)
	if code != 200 {
		t.Fatal(code, string(body))
	}
	code, _ = request(t, c, "POST", base+"/catalog/picopak-64/copy", token, map[string]bool{"stock_program": true}, nil)
	if code != 422 {
		t.Fatal("unmatched stock program:", code)
	}
}

func TestUnresolvedCatalogCopyCannotPublishToStockMenu(t *testing.T) {
	_, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	base := h.URL + "/api/v1"
	code, body := request(t, c, "POST", base+"/catalog/public-02a538894b06404da7793ff10fe99788/copy", csrf, map[string]any{}, nil)
	var saved domain.Recipe
	if code != 201 || json.Unmarshal(body, &saved) != nil || saved.Provenance == nil {
		t.Fatalf("copy unresolved Chill Cycle for editing: %d %s", code, body)
	}
	ref := stockReference{RecipeID: saved.ID, Revision: saved.Revision}
	for _, manual := range []bool{false, true} {
		if manual {
			ref.Locations = make([]int, len(saved.Steps))
			for i := range ref.Locations {
				ref.Locations[i] = 1
			}
		}
		for _, publish := range []bool{false, true} {
			requested := stockSubmission{Recipes: []stockReference{ref}, Publish: publish}
			code, body := request(t, c, "POST", base+"/stock-zymatics/TEST/recipes", csrf, requested, nil)
			if code != 422 || !strings.Contains(string(body), "chiller utility with placeholder beer ingredients") {
				t.Fatalf("stock menu manual=%v publish=%v: %d %s", manual, publish, code, body)
			}
		}
	}
}
