package firmware

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware/firmwaretest"
)

func TestValidateCompletePackagesForBothModels(t *testing.T) {
	for _, model := range []string{"pico-s", "pico-pro"} {
		files := firmwaretest.Files(model)
		result, err := ValidateArchive(firmwaretest.Archive(files))
		if err != nil || result.Model != model || result.ModelFamily != "pico-s-pro" ||
			result.FirmwareVersion != "1.2.0" || result.BinaryBytes != 256 ||
			result.BinarySHA256 != firmwaretest.Digest(files[model+"-standalone.bin"]) {
			t.Fatalf("%s: %+v, %v", model, result, err)
		}
	}
}

func editFirstAsset(files map[string][]byte, fn func(map[string]any)) {
	firmwaretest.ChangeManifest(files, func(m map[string]any) {
		fn(m["assets"].([]any)[0].(map[string]any))
	})
}

func replaceGzip(files map[string][]byte, data []byte) {
	files["web/index.html.gz"] = data
	editFirstAsset(files, func(a map[string]any) {
		a["length"] = len(data)
		a["sha256"] = firmwaretest.Digest(data)
		blocks := (len(data) + 512 + 4095) / 4096
		a["allocation_blocks"] = blocks
		a["max_size"] = blocks*4096 - 512
	})
}

func TestRejectInconsistentModelImageAndBrowserPackages(t *testing.T) {
	tests := map[string]func(map[string][]byte){
		"unsupported model": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["model"] = "pico-c" })
		},
		"wrong family": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["model_family"] = "z-series" })
		},
		"wrong profile": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["profile"] = "oem" })
		},
		"embedded key": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["api_key_embedded"] = true })
		},
		"failed link": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["sdk_link_passed"] = false })
		},
		"wrong binary filename": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["files"] = map[string]any{"binary": "pico-s-standalone.bin"} })
		},
		"binary hash": func(f map[string][]byte) {
			f["pico-pro-standalone.bin"][20] ^= 1
		},
		"image vector differs from report": func(f map[string][]byte) {
			f["pico-pro-standalone.bin"][0] ^= 8
			firmwaretest.ChangeReport(f, func(r map[string]any) {
				r["binary_sha256"] = firmwaretest.Digest(f["pico-pro-standalone.bin"])
			})
		},
		"invalid reset vector": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["image_layout"].(map[string]any)["reset_vector"] = "0x20004000" })
		},
		"test symbols": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) { r["image_layout"].(map[string]any)["test_symbols"] = []string{"test_override"} })
		},
		"overlapping segments": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) {
				layout := r["image_layout"].(map[string]any)
				layout["load_segments"] = []map[string]any{
					{"start": "0x20004000", "memory_bytes": 256, "file_bytes": 256},
					{"start": "0x20004008", "memory_bytes": 8, "file_bytes": 8},
				}
			})
		},
		"manifest hash": func(f map[string][]byte) { f["web/manifest.json"] = append(f["web/manifest.json"], '\n') },
		"storage differs from report": func(f map[string][]byte) {
			firmwaretest.ChangeReport(f, func(r map[string]any) {
				r["browser_storage"].(map[string]any)["assets"].([]any)[0].(map[string]any)["source_sha256"] = strings.Repeat("0", 64)
			})
		},
		"browser logical path": func(f map[string][]byte) {
			editFirstAsset(f, func(a map[string]any) { a["file_path"] = "/sys/mcuimg.bin" })
		},
		"browser content type": func(f map[string][]byte) {
			editFirstAsset(f, func(a map[string]any) { a["content_type"] = "application/javascript" })
		},
		"browser allocation": func(f map[string][]byte) {
			editFirstAsset(f, func(a map[string]any) { a["max_size"] = 65536 })
		},
		"browser payload hash": func(f map[string][]byte) {
			f["web/index.html.gz"][12] ^= 1
		},
		"gzip checksum": func(f map[string][]byte) {
			data := bytes.Clone(f["web/index.html.gz"])
			data[len(data)-8] ^= 1
			replaceGzip(f, data)
		},
		"gzip trailing bytes": func(f map[string][]byte) {
			replaceGzip(f, append(f["web/index.html.gz"], 0))
		},
		"gzip extra empty member": func(f map[string][]byte) {
			replaceGzip(f, append(f["web/index.html.gz"], firmwaretest.Gzip(nil)...))
		},
		"expanded payload exceeds declared size": func(f map[string][]byte) {
			replaceGzip(f, firmwaretest.Gzip(bytes.Repeat([]byte("x"), MaxSourceBytes+1)))
			editFirstAsset(f, func(a map[string]any) { a["source_length"] = MaxSourceBytes })
		},
		"uncompressed hash": func(f map[string][]byte) {
			editFirstAsset(f, func(a map[string]any) { a["source_sha256"] = strings.Repeat("0", 64) })
		},
		"unknown manifest field": func(f map[string][]byte) {
			firmwaretest.ChangeManifest(f, func(m map[string]any) { m["run_script"] = "ignored?" })
		},
		"duplicate case folded model": func(f map[string][]byte) {
			f["firmware-report.json"] = bytes.Replace(f["firmware-report.json"], []byte(`"model":"pico-pro"`), []byte(`"model":"pico-c","Model":"pico-pro"`), 1)
		},
		"duplicate exact model": func(f map[string][]byte) {
			f["firmware-report.json"] = bytes.Replace(f["firmware-report.json"], []byte(`"model":"pico-pro"`), []byte(`"model":"pico-c","model":"pico-pro"`), 1)
		},
		"multiple report JSON values": func(f map[string][]byte) {
			f["firmware-report.json"] = append(f["firmware-report.json"], []byte("\n{}")...)
		},
	}
	for name, change := range tests {
		t.Run(name, func(t *testing.T) {
			files := firmwaretest.Files("pico-pro")
			change(files)
			if got, err := ValidateArchive(firmwaretest.Archive(files)); err == nil {
				t.Fatalf("unsafe package accepted: %+v", got)
			}
		})
	}
}

func zipWithHeaders(files map[string][]byte, changed string, mutate func(*zip.FileHeader)) []byte {
	var out bytes.Buffer
	z := zip.NewWriter(&out)
	for name, data := range files {
		h := &zip.FileHeader{Name: name, Method: zip.Store}
		h.SetMode(0644)
		if name == changed {
			mutate(h)
		}
		w, err := z.CreateHeader(h)
		if err != nil {
			panic(err)
		}
		_, _ = w.Write(data)
	}
	_ = z.Close()
	return out.Bytes()
}

func TestRejectArchivePathEntryAndSizeAttacks(t *testing.T) {
	for _, name := range []string{"../firmware-report.json", "/firmware-report.json", `web\index.html.gz`, "web/./index.html.gz", "C:firmware-report.json", "unexpected.txt"} {
		t.Run(name, func(t *testing.T) {
			data := zipWithHeaders(firmwaretest.Files("pico-pro"), "firmware-report.json", func(h *zip.FileHeader) { h.Name = name })
			if _, err := ValidateArchive(data); err == nil {
				t.Fatal("unsafe path accepted")
			}
		})
	}
	t.Run("symlink", func(t *testing.T) {
		data := zipWithHeaders(firmwaretest.Files("pico-pro"), "firmware-report.json", func(h *zip.FileHeader) { h.ExternalAttrs = 0120777 << 16 })
		if _, err := ValidateArchive(data); err == nil {
			t.Fatal("symlink accepted")
		}
	})
	t.Run("duplicate", func(t *testing.T) {
		data := zipWithHeaders(firmwaretest.Files("pico-pro"), "web/index.html.gz", func(h *zip.FileHeader) { h.Name = "web/app.js.gz" })
		if _, err := ValidateArchive(data); err == nil {
			t.Fatal("duplicate file accepted")
		}
	})
	t.Run("too many entries", func(t *testing.T) {
		files := firmwaretest.Files("pico-pro")
		files["extra"] = []byte("extra")
		if _, err := ValidateArchive(firmwaretest.Archive(files)); err == nil {
			t.Fatal("extra file accepted")
		}
	})
	t.Run("report expansion", func(t *testing.T) {
		files := firmwaretest.Files("pico-pro")
		files["firmware-report.json"] = bytes.Repeat([]byte(" "), MaxReportBytes+1)
		data := zipWithHeaders(files, "firmware-report.json", func(h *zip.FileHeader) { h.Method = zip.Deflate })
		if _, err := ValidateArchive(data); err == nil {
			t.Fatal("oversized expanded report accepted")
		}
	})
	t.Run("archive size", func(t *testing.T) {
		if _, err := ValidateArchive(make([]byte, MaxArchiveBytes+1)); err == nil {
			t.Fatal("oversized archive accepted")
		}
	})
	t.Run("truncated", func(t *testing.T) {
		data := firmwaretest.Archive(firmwaretest.Files("pico-pro"))
		if _, err := ValidateArchive(data[:len(data)/2]); err == nil {
			t.Fatal("truncated archive accepted")
		}
	})
}

func TestCatalogCandidateDoesNotClaimCompleteValidation(t *testing.T) {
	report, err := parseReport(firmwaretest.Files("pico-pro")["firmware-report.json"])
	if err != nil {
		t.Fatal(err)
	}
	data, _ := json.Marshal(candidate(report))
	if bytes.Contains(data, []byte("validation")) || bytes.Contains(data, []byte("provenance")) {
		t.Fatal("candidate claims validation or acquisition provenance")
	}
}
