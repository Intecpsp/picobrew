package server

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"syscall"
	"testing"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/webui"
)

func getWebsite(t *testing.T, client *http.Client, method, url string, headers map[string]string) (*http.Response, []byte) {
	t.Helper()
	req, err := http.NewRequest(method, url, nil)
	if err != nil {
		t.Fatal(err)
	}
	for key, value := range headers {
		req.Header.Set(key, value)
	}
	response, err := client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	data, err := io.ReadAll(response.Body)
	if err != nil {
		t.Fatal(err)
	}
	return response, data
}

func TestBundledWebsiteWorksOutsideCheckout(t *testing.T) {
	// Neither a source checkout nor web/dist is present in this process cwd.
	t.Chdir(t.TempDir())
	_, h, client := newTestServer(t)
	response, index := getWebsite(t, client, "GET", h.URL+"/", nil)
	if response.StatusCode != 200 || !bytes.Contains(index, []byte("<title>PicoBrew Hub</title>")) {
		t.Fatalf("bundled index: %d %s", response.StatusCode, index)
	}
	if response.Header.Get("Content-Type") != "text/html; charset=utf-8" || response.Header.Get("Cache-Control") != "no-cache" || response.Header.Get("ETag") == "" {
		t.Fatalf("index headers: %v", response.Header)
	}
	if response.Header.Get("X-Content-Type-Options") != "nosniff" || response.Header.Get("Content-Security-Policy") == "" {
		t.Fatal("bundled website lost the application security headers")
	}
	if response.Header.Get("Last-Modified") != "" {
		t.Fatal("deterministic ZIP timestamp must not become website freshness")
	}
	entryTags := regexp.MustCompile(`(?:src|href)="(/assets/[^"]+)"`).FindAllSubmatch(index, -1)
	if len(entryTags) < 2 {
		t.Fatal("production HTML does not reference compiled script and stylesheet assets")
	}
	files, err := webui.Files()
	if err != nil {
		t.Fatal(err)
	}
	for _, match := range entryTags {
		name := string(match[1])
		response, data := getWebsite(t, client, "GET", h.URL+name, nil)
		want, err := fs.ReadFile(files, strings.TrimPrefix(name, "/"))
		if err != nil || response.StatusCode != 200 || !bytes.Equal(data, want) {
			t.Fatalf("entry asset %s: %d, %v", name, response.StatusCode, err)
		}
		if response.Header.Get("Content-Type") != webContentType(name) || !strings.Contains(response.Header.Get("Cache-Control"), "immutable") {
			t.Fatalf("compiled asset headers: %s %v", name, response.Header)
		}
	}
	for _, route := range []string{"/recipes/recipe-id", "/devices/test", "/catalog"} {
		response, data := getWebsite(t, client, "GET", h.URL+route, nil)
		if response.StatusCode != 200 || !bytes.Equal(data, index) || response.Header.Get("Cache-Control") != "no-cache" {
			t.Fatalf("SPA fallback %s: %d", route, response.StatusCode)
		}
	}
	response, body := getWebsite(t, client, "HEAD", h.URL+"/", nil)
	if response.StatusCode != 200 || len(body) != 0 || response.ContentLength != int64(len(index)) {
		t.Fatalf("HEAD index: %d, %d body bytes, length %d", response.StatusCode, len(body), response.ContentLength)
	}
	tag := response.Header.Get("ETag")
	response, body = getWebsite(t, client, "GET", h.URL+"/", map[string]string{"If-None-Match": tag})
	if response.StatusCode != http.StatusNotModified || len(body) != 0 {
		t.Fatalf("ETag validation: %d %s", response.StatusCode, body)
	}
	response, _ = getWebsite(t, client, "GET", h.URL+"/", map[string]string{"If-Modified-Since": time.Now().Add(time.Hour).UTC().Format(http.TimeFormat)})
	if response.StatusCode != 200 {
		t.Fatal("an artificial bundle timestamp caused a stale conditional response")
	}
	// A leftover host build must not silently override the bundled version.
	if err := os.MkdirAll("web/dist", 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile("web/dist/index.html", []byte("STALE HOST BUILD"), 0600); err != nil {
		t.Fatal(err)
	}
	_, body = getWebsite(t, client, "GET", h.URL+"/", nil)
	if !bytes.Equal(body, index) {
		t.Fatal("default UI depended on a host web directory")
	}
}

func TestBundledWebsiteAssetsSupportRangesAndMissingPathsStay404(t *testing.T) {
	_, h, client := newTestServer(t)
	files, err := webui.Files()
	if err != nil {
		t.Fatal(err)
	}
	// Both the PDF worker and the PDF font data come from the same bundle
	// filesystem. Per-request seekable readers must support real HTTP ranges.
	workers, err := fs.Glob(files, "assets/pdf.worker.min-*.mjs")
	if err != nil || len(workers) != 1 {
		t.Fatalf("PDF worker missing: %v %v", workers, err)
	}
	fonts, err := fs.Glob(files, "vendor/pdfjs-*/standard_fonts/LiberationSans-Regular.ttf")
	if err != nil || len(fonts) != 1 {
		t.Fatalf("PDF font missing: %v %v", fonts, err)
	}
	for _, item := range []struct{ name, mime string }{
		{"/" + workers[0], "text/javascript; charset=utf-8"},
		{"/" + fonts[0], "font/ttf"},
	} {
		want, err := fs.ReadFile(files, strings.TrimPrefix(item.name, "/"))
		if err != nil || len(want) < 32 {
			t.Fatalf("bundled test asset %s: %v", item.name, err)
		}
		response, data := getWebsite(t, client, "GET", h.URL+item.name, map[string]string{"Range": "bytes=7-23"})
		if response.StatusCode != http.StatusPartialContent || !bytes.Equal(data, want[7:24]) || response.Header.Get("Content-Range") != fmt.Sprintf("bytes 7-23/%d", len(want)) {
			t.Fatalf("range %s: %d %v", item.name, response.StatusCode, response.Header)
		}
		if response.Header.Get("Content-Type") != item.mime {
			t.Fatalf("asset MIME %s: %s", item.name, response.Header.Get("Content-Type"))
		}
	}
	for _, route := range []string{"/assets/missing.js", "/assets/no-suffix", "/vendor/missing.bin", "/missing.pdf", "/api", "/api/unknown", "/api/v1/no-such-endpoint", "/%2e%2e/secret", "/..%5csecret", "/.env"} {
		response, data := getWebsite(t, client, "GET", h.URL+route, nil)
		if response.StatusCode != http.StatusNotFound || bytes.Contains(data, []byte("<html")) {
			t.Fatalf("missing/invalid resource %s returned %d %s", route, response.StatusCode, data)
		}
	}
}

func TestExplicitWebOverrideStaysConfinedAndDoesNotFallBackToBundle(t *testing.T) {
	s, h, client := newTestServer(t)
	root := t.TempDir()
	s.cfg.WebDir = filepath.Join(root, "web")
	if err := os.Mkdir(s.cfg.WebDir, 0700); err != nil {
		t.Fatal(err)
	}
	response, _ := getWebsite(t, client, "GET", h.URL+"/", nil)
	if response.StatusCode != http.StatusServiceUnavailable {
		t.Fatal("an explicit missing index silently used bundled assets")
	}
	if err := os.WriteFile(filepath.Join(s.cfg.WebDir, "index.html"), []byte("EXPLICIT DEVELOPMENT UI"), 0600); err != nil {
		t.Fatal(err)
	}
	response, data := getWebsite(t, client, "GET", h.URL+"/recipes", nil)
	if response.StatusCode != 200 || string(data) != "EXPLICIT DEVELOPMENT UI" {
		t.Fatalf("explicit override: %d %s", response.StatusCode, data)
	}
	secret := filepath.Join(root, "outside.txt")
	if err := os.WriteFile(secret, []byte("OUTSIDE PRIVATE FILE"), 0600); err != nil {
		t.Fatal(err)
	}
	t.Run("symlink cannot escape web root", func(t *testing.T) {
		if err := os.Symlink(secret, filepath.Join(s.cfg.WebDir, "outside.txt")); err != nil {
			if runtime.GOOS == "windows" && (errors.Is(err, os.ErrPermission) || errors.Is(err, syscall.Errno(1314))) {
				t.Skip("symlink creation is unavailable to this account")
			}
			t.Fatal(err)
		}
		response, data := getWebsite(t, client, "GET", h.URL+"/outside.txt", nil)
		if response.StatusCode != 404 || bytes.Contains(data, []byte("OUTSIDE PRIVATE FILE")) {
			t.Fatalf("symlink escape: %d %s", response.StatusCode, data)
		}
	})
}
