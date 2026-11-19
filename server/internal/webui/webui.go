// Package webui supplies the production website bundled into the Hub binary.
// Frontend development builds the archive; running the Hub needs no Node.js,
// host web directory, or extraction into the writable data directory.
package webui

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	_ "embed"
	"fmt"
	"io"
	"io/fs"
	"path"
	"sync"
	"time"
)

//go:embed bundle.zip
var bundle []byte

var (
	once      sync.Once
	website   fs.FS
	bundleErr error
)

// Files returns an immutable filesystem whose files support io.ReadSeeker.
// Decompress once; each request receives its own cursor for HTTP byte ranges.
func Files() (fs.FS, error) {
	once.Do(func() { website, bundleErr = load(bundle) })
	return website, bundleErr
}

type assets struct {
	files   map[string]*asset
	archive *zip.Reader
}

type asset struct {
	name string
	data []byte
	etag string
}

func load(data []byte) (fs.FS, error) {
	archive, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		return nil, fmt.Errorf("web bundle: %w", err)
	}
	files := make(map[string]*asset, len(archive.File))
	const maxBytes = 64 << 20
	var total uint64
	for _, entry := range archive.File {
		if !fs.ValidPath(entry.Name) || entry.Name == "." || !entry.Mode().IsRegular() || files[entry.Name] != nil {
			return nil, fmt.Errorf("web bundle: invalid or duplicate asset %q", entry.Name)
		}
		if entry.UncompressedSize64 > maxBytes-total {
			return nil, fmt.Errorf("web bundle: uncompressed assets exceed %d bytes", maxBytes)
		}
		total += entry.UncompressedSize64
		reader, err := entry.Open()
		if err != nil {
			return nil, fmt.Errorf("web bundle: open %s: %w", entry.Name, err)
		}
		contents, err := io.ReadAll(io.LimitReader(reader, int64(entry.UncompressedSize64)+1))
		closeErr := reader.Close()
		if err == nil {
			err = closeErr
		}
		if err != nil || uint64(len(contents)) != entry.UncompressedSize64 {
			return nil, fmt.Errorf("web bundle: invalid content for %s", entry.Name)
		}
		files[entry.Name] = &asset{name: path.Base(entry.Name), data: contents, etag: fmt.Sprintf(`"%x"`, sha256.Sum256(contents))}
	}
	if files["index.html"] == nil {
		return nil, fmt.Errorf("web bundle: index.html is missing")
	}
	return &assets{files: files, archive: archive}, nil
}

func (files *assets) Open(name string) (fs.File, error) {
	if !fs.ValidPath(name) {
		return nil, &fs.PathError{Op: "open", Path: name, Err: fs.ErrInvalid}
	}
	file, found := files.files[name]
	if !found {
		// zip.Reader supplies directory metadata/ReadDir without copying it.
		// Every regular file was validated and cached above, so only directory
		// handles or missing-path errors can reach this branch.
		return files.archive.Open(name)
	}
	return &assetFile{Reader: bytes.NewReader(file.data), asset: file}, nil
}

type assetFile struct {
	*bytes.Reader
	*asset
}

func (f *assetFile) Close() error               { return nil }
func (f *assetFile) Stat() (fs.FileInfo, error) { return f.asset, nil }
func (f *assetFile) ETag() string               { return f.etag }
func (a *asset) Name() string                   { return a.name }
func (a *asset) Size() int64                    { return int64(len(a.data)) }
func (a *asset) Mode() fs.FileMode              { return 0444 }
func (a *asset) ModTime() time.Time             { return time.Time{} }
func (a *asset) IsDir() bool                    { return false }
func (a *asset) Sys() any                       { return nil }
