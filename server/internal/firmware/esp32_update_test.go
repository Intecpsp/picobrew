package firmware

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/json"
	"testing"
)

func TestESP32UpdateTransfersOnlyTheTaggedApplication(t *testing.T) {
	for _, target := range []string{"picostill", "z-series"} {
		for _, variant := range []string{"valid", "wrong model", "USB only", "altered report", "missing marker", "wrong binary", "missing component", "duplicate image", "bad segment length", "wrong flash checksum", "unmapped entry point"} {
			t.Run(target+"/"+variant, func(t *testing.T) {
				image := make([]byte, 384)
				image[0], image[1], image[23] = 0xe9, 2, 1
				binary.LittleEndian.PutUint32(image[4:], 0x40080000)
				binary.LittleEndian.PutUint32(image[24:], 0x3f400020)
				binary.LittleEndian.PutUint32(image[28:], 320)
				binary.LittleEndian.PutUint32(image[32:], 0xabcd5432)
				if variant != "missing marker" {
					copy(image[0x120:0x160], "PicoBrew OTA1|"+target+"|1.2.7")
				}
				binary.LittleEndian.PutUint32(image[352:], 0x40080000)
				binary.LittleEndian.PutUint32(image[356:], 16)
				check := byte(0xef)
				for _, b := range image[32:352] {
					check ^= b
				}
				image[383] = check
				switch variant {
				case "bad segment length":
					binary.LittleEndian.PutUint32(image[356:], 8192)
				case "wrong flash checksum":
					image[383] ^= 1
				case "unmapped entry point":
					binary.LittleEndian.PutUint32(image[4:], 0x40081000)
				}
				digest := sha256.Sum256(image)
				image = append(image, digest[:]...)
				c := Candidate{OTAProtocol: ESP32FactoryProtocol, Model: target, ModelFamily: target, FirmwareVersion: "1.2.7", BinaryBytes: len(image), BinarySHA256: Digest(image)}
				model := target
				if variant == "wrong model" {
					model = "picoferm"
				}
				report, _ := json.Marshal(map[string]string{"model": model, "firmware_version": "1.2.7"})
				component := map[string]any{"file": "application.bin", "bytes": len(image), "sha256": Digest(image)}
				if variant == "missing component" {
					component["file"] = "bootloader.bin"
				}
				manifest := map[string]any{"model": target, "ota_image": "application.bin", "ota_protocol": ESP32FactoryProtocol, "build_report_sha256": Digest(report), "components": []map[string]any{component}}
				if variant == "USB only" {
					delete(manifest, "ota_protocol")
				}
				if variant == "altered report" {
					report = append(report, ' ')
				}
				if variant == "wrong binary" {
					image[1] ^= 1
				}
				metadata, _ := json.Marshal(manifest)
				var buf bytes.Buffer
				z := zip.NewWriter(&buf)
				files := map[string][]byte{"application.bin": image, "build-report.json": report, "manifest.json": metadata, "bootloader.bin": []byte("USB only")}
				for name, contents := range files {
					entry, err := z.Create(name)
					if err != nil {
						t.Fatal(err)
					}
					if _, err = entry.Write(contents); err != nil {
						t.Fatal(err)
					}
				}
				if variant == "duplicate image" {
					if _, err := z.Create("application.bin"); err != nil {
						t.Fatal(err)
					}
				}
				if err := z.Close(); err != nil {
					t.Fatal(err)
				}
				pkg, err := ReadFactoryUpdate(buf.Bytes(), c)
				if variant != "valid" {
					if err == nil {
						t.Fatal("invalid package accepted")
					}
					return
				}
				if err != nil || !bytes.Equal(pkg.Files[0], image) {
					t.Fatal(err)
				}
				for _, sidecar := range pkg.Files[1:] {
					if sidecar != nil {
						t.Error("USB-only component selected for OTA")
					}
				}
			})
		}
	}
}
