// Package firmwaretest constructs small synthetic package fixtures for tests.
// These bytes are not executable firmware and must never be installed.
package firmwaretest

import (
	"archive/zip"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"slices"
)

func Digest(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])
}

func JSON(value any) []byte {
	data, err := json.Marshal(value)
	if err != nil {
		panic(err)
	}
	return data
}

func Gzip(data []byte) []byte {
	var out bytes.Buffer
	z := gzip.NewWriter(&out)
	if _, err := z.Write(data); err != nil {
		panic(err)
	}
	if err := z.Close(); err != nil {
		panic(err)
	}
	return out.Bytes()
}

// Files includes a caller-controlled provenance claim to verify that the Hub
// derives provenance from the acquisition path instead of the uploaded report.
func Files(model string) map[string][]byte {
	image := make([]byte, 256)
	binary.LittleEndian.PutUint32(image[:4], 0x2003ff00)
	binary.LittleEndian.PutUint32(image[4:8], 0x20004009)
	copy(image[8:], []byte("SYNTHETIC NONEXECUTABLE TEST FIXTURE "+model))
	out := map[string][]byte{model + "-standalone.bin": image}
	assets := []map[string]any{}
	for _, entry := range []struct{ name, target, kind string }{
		{"index.html", "/local/web/i.gz", "text/html; charset=utf-8"},
		{"app.js", "/local/web/a.gz", "application/javascript; charset=utf-8"},
		{"api-explorer.js", "/local/web/e.gz", "application/javascript; charset=utf-8"},
		{"history.js", "/local/web/h.gz", "application/javascript; charset=utf-8"},
		{"style.css", "/local/web/s.gz", "text/css; charset=utf-8"},
	} {
		source := []byte("synthetic test content: " + entry.name)
		compressed := Gzip(source)
		out["web/"+entry.name+".gz"] = compressed
		blocks := (len(compressed) + 512 + 4095) / 4096
		assets = append(assets, map[string]any{
			"source": entry.name, "file_path": entry.target, "payload": entry.name + ".gz",
			"content_type": entry.kind, "content_encoding": "gzip", "length": len(compressed),
			"allocation_blocks": blocks, "max_size": blocks*4096 - 512,
			"sha256": Digest(compressed), "source_length": len(source), "source_sha256": Digest(source),
		})
	}
	manifest := map[string]any{
		"schema_version": 1, "block_size": 4096, "metadata_per_file": 512, "fail_safe": false, "assets": assets,
	}
	out["web/manifest.json"] = JSON(manifest)
	out["firmware-report.json"] = JSON(map[string]any{
		"artifact": "Nonexecutable test fixture", "profile": "standalone", "model": model, "model_family": "pico-s-pro",
		"firmware_version": "1.2.0", "binary_sha256": Digest(image), "sdk_link_passed": true,
		"api_key_embedded": false, "browser_manifest_sha256": Digest(out["web/manifest.json"]), "browser_storage": manifest,
		"files":      map[string]string{"binary": model + "-standalone.bin"},
		"provenance": map[string]string{"source": "github", "repository": "attacker/claim", "commit": "untrusted"},
		"image_layout": map[string]any{
			"passed": true, "load_address": "0x20004000", "ram_end": "0x20040000",
			"initial_stack_pointer": "0x2003ff00", "reset_vector": "0x20004009", "entry": "0x20004009",
			"binary_bytes": len(image), "bss_bytes": 0, "heap_bytes": 0, "sdk_stack_bytes": 0, "unallocated_ram_bytes": 0,
			"undefined_symbols": []string{}, "test_symbols": []string{},
			"required_entry_symbols": map[string]string{"ResetISR": "0x20004009", "g_pfnVectors": "0x20004000"},
			"load_segments":          []map[string]any{{"start": "0x20004000", "memory_bytes": len(image), "file_bytes": len(image)}},
		},
	})
	return out
}

// CFiles keeps controller identity explicit even in nonexecutable fixtures.
func CFiles(display string) map[string][]byte {
	files := Files("pico-c")
	var report map[string]any
	if err := json.Unmarshal(files["firmware-report.json"], &report); err != nil {
		panic(err)
	}
	height, rotation := 64, 2
	if display == "ssd1309" {
		height, rotation = 32, 0
	}
	report["model_family"], report["display_variant"], report["firmware_update"] = "pico-c", display, false
	report["display_geometry"] = map[string]int{"width": 128, "height": height, "rotation": rotation, "storage_bytes": 1024, "transfer_bytes": 1024}
	copy(files["pico-c-standalone.bin"][128:], []byte(display))
	report["binary_sha256"] = Digest(files["pico-c-standalone.bin"])
	files["firmware-report.json"] = JSON(report)
	return files
}

// COTAFiles produces a nonexecutable test image carrying the same target marker as a linked Pico C.
func COTAFiles(display string) map[string][]byte {
	files := CFiles(display)
	const name = "pico-c-standalone.bin"
	image := make([]byte, 1100)
	copy(image, files[name])
	copy(image[1024:], "PicoBrew:pico-c:"+display+"\x00")
	files[name] = image
	ChangeReport(files, func(report map[string]any) {
		report["hub_ota_protocol"] = 1
		report["binary_sha256"] = Digest(image)
		layout := report["image_layout"].(map[string]any)
		layout["binary_bytes"] = len(image)
		seg := layout["load_segments"].([]any)[0].(map[string]any)
		seg["memory_bytes"], seg["file_bytes"] = len(image), len(image)
	})
	return files
}

func Archive(files map[string][]byte) []byte {
	var out bytes.Buffer
	z := zip.NewWriter(&out)
	names := make([]string, 0, len(files))
	for name := range files {
		names = append(names, name)
	}
	slices.Sort(names)
	for _, name := range names {
		header := &zip.FileHeader{Name: name, Method: zip.Store, ModifiedDate: 33}
		header.SetMode(0644)
		w, err := z.CreateHeader(header)
		if err != nil {
			panic(err)
		}
		if _, err := w.Write(files[name]); err != nil {
			panic(err)
		}
	}
	if err := z.Close(); err != nil {
		panic(err)
	}
	return out.Bytes()
}

func ChangeReport(files map[string][]byte, change func(map[string]any)) {
	var report map[string]any
	if err := json.Unmarshal(files["firmware-report.json"], &report); err != nil {
		panic(err)
	}
	change(report)
	files["firmware-report.json"] = JSON(report)
}

// ChangeManifest also updates the report's hash chain. It allows tests to reach
// semantic/gzip validation instead of stopping at the outer manifest digest.
func ChangeManifest(files map[string][]byte, change func(map[string]any)) {
	var manifest map[string]any
	if err := json.Unmarshal(files["web/manifest.json"], &manifest); err != nil {
		panic(err)
	}
	change(manifest)
	files["web/manifest.json"] = JSON(manifest)
	ChangeReport(files, func(report map[string]any) {
		report["browser_manifest_sha256"] = Digest(files["web/manifest.json"])
		report["browser_storage"] = manifest
	})
}
