package server

import (
	"io"
	"io/fs"
	"mime"
	"net/http"
	"os"
	"path"
	"regexp"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/webui"
)

var fingerprintedAsset = regexp.MustCompile(`^assets/[^/]+-[A-Za-z0-9_-]{8}\.[A-Za-z0-9.]+$`)

func (s *Server) static(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/api" || strings.HasPrefix(r.URL.Path, "/api/") {
		s.fail(w, 404, "not_found", "API endpoint not found")
		return
	}
	name := strings.TrimSuffix(strings.TrimPrefix(r.URL.Path, "/"), "/")
	if name != "" && (!fs.ValidPath(name) || strings.ContainsAny(name, `\:`) || strings.ContainsFunc(name, func(char rune) bool { return char < 32 })) {
		http.NotFound(w, r)
		return
	}
	for _, part := range strings.Split(name, "/") {
		if strings.HasPrefix(part, ".") {
			http.NotFound(w, r)
			return
		}
	}
	var files fs.FS
	if s.cfg.WebDir == "" {
		var err error
		files, err = webui.Files()
		if err != nil {
			s.cfg.Logger.Error("Bundled website unavailable", "error", err)
			http.Error(w, "The bundled website is unavailable", http.StatusServiceUnavailable)
			return
		}
	} else {
		// The explicit development override remains confined to its selected
		// directory, including symlink resolution; it never exposes sibling data.
		root, err := os.OpenRoot(s.cfg.WebDir)
		if err != nil {
			http.Error(w, "The configured web directory is unavailable", http.StatusServiceUnavailable)
			return
		}
		defer root.Close()
		files = root.FS()
	}
	if name == "" {
		name = "index.html"
	}
	file, err := files.Open(name)
	if err == nil {
		info, statErr := file.Stat()
		if statErr != nil || !info.Mode().IsRegular() {
			file.Close()
			file = nil
		}
	}
	if file == nil {
		if name == "index.html" {
			http.Error(w, "The configured website has no index.html", http.StatusServiceUnavailable)
			return
		}
		// Failed asset requests must not return HTML with a misleading 200.
		// Extensionless application routes retain the SPA entry point.
		if path.Ext(name) != "" || name == "assets" || name == "vendor" || strings.HasPrefix(name, "assets/") || strings.HasPrefix(name, "vendor/") {
			http.NotFound(w, r)
			return
		}
		name = "index.html"
		file, err = files.Open(name)
		if err != nil {
			http.Error(w, "The configured website has no index.html", http.StatusServiceUnavailable)
			return
		}
	}
	defer file.Close()
	info, err := file.Stat()
	reader, seekable := file.(io.ReadSeeker)
	if err != nil || !info.Mode().IsRegular() || !seekable {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Cache-Control", "no-cache")
	if s.cfg.WebDir == "" && fingerprintedAsset.MatchString(name) {
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	}
	if tagged, ok := file.(interface{ ETag() string }); ok {
		w.Header().Set("ETag", tagged.ETag())
	}
	if contentType := webContentType(name); contentType != "" {
		w.Header().Set("Content-Type", contentType)
	}
	// Embedded assets deliberately have no artificial archive timestamp.
	// Content ETags validate updates; a fixed ZIP timestamp must not turn a new
	// index into a stale If-Modified-Since response after a binary upgrade.
	http.ServeContent(w, r, name, info.ModTime(), reader)
}

func webContentType(name string) string {
	// Serve the application consistently even on hosts with stale registry or
	// /etc/mime.types mappings. nosniff requires correct module/worker types.
	switch path.Ext(name) {
	case ".html":
		return "text/html; charset=utf-8"
	case ".js", ".mjs":
		return "text/javascript; charset=utf-8"
	case ".css":
		return "text/css; charset=utf-8"
	case ".wasm":
		return "application/wasm"
	case ".ttf":
		return "font/ttf"
	case ".pdf":
		return "application/pdf"
	default:
		return mime.TypeByExtension(path.Ext(name))
	}
}
