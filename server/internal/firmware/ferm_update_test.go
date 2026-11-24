package firmware

import (
	"archive/zip"
	"bytes"
	"encoding/binary"
	"encoding/json"
	"os"
	"testing"
)

func fermTestImage(pc uint32, sections ...struct {
	address uint32
	data    []byte
}) []byte {
	out := new(bytes.Buffer)
	out.Write([]byte{0xe9, byte(len(sections)), 2, 0x30})
	binary.Write(out, binary.LittleEndian, pc)
	sum := byte(0xef)
	for _, s := range sections {
		binary.Write(out, binary.LittleEndian, s.address)
		binary.Write(out, binary.LittleEndian, uint32(len(s.data)))
		out.Write(s.data)
		for _, b := range s.data {
			sum ^= b
		}
	}
	for out.Len()%16 != 15 {
		out.WriteByte(0)
	}
	out.WriteByte(sum)
	return out.Bytes()
}

func TestPicoFermUpdateComponents(t *testing.T) {
	type section = struct {
		address uint32
		data    []byte
	}
	for _, tc := range []string{"valid", "swapped bank", "wrong marker", "wrong segment", "bad checksum", "bad bootstrap", "wrong filesystem", "wrong release hash", "duplicate manifest field"} {
		t.Run(tc, func(t *testing.T) {
			c := Candidate{Model: "picoferm", ModelFamily: "picoferm", FirmwareVersion: "1.2.7", OTAProtocol: "esp8266-ab-v1"}
			fs := make([]byte, 0xfb000)
			boot := fermTestImage(0x4010f000, section{0x4010f000, make([]byte, 32)})
			files := map[string][]byte{"bootstrap.bin": boot, "spiffs.bin": fs}
			manifest := fermManifest{Schema: 1, Model: c.Model, Version: c.FirmwareVersion, Protocol: c.OTAProtocol, BootstrapSHA256: Digest(boot)}
			for i, name := range []string{"bank-0.bin", "bank-1.bin"} {
				base := 0x3000 + i*0x80000
				marker := make([]byte, 32)
				copy(marker, "PicoFerm-AB1:"+c.FirmwareVersion)
				addr := uint32(base) + 0x40200010
				dram := uint32(0x3ffe8000)
				if i == 1 {
					switch tc {
					case "swapped bank":
						addr -= 0x80000
					case "wrong marker":
						marker[5] ^= 1
					case "wrong segment":
						dram = 0x3fffff00
					}
				}
				app := fermTestImage(0x40100004, section{addr, marker}, section{0x40100000, make([]byte, 8)},
					section{dram, make([]byte, 4)}, section{0x3ffe8010, make([]byte, 4)})
				if tc == "bad checksum" && i == 1 {
					app[len(app)-1] ^= 1
				}
				files[name] = app
				bank := PicoFermBank{Bank: i, Offset: base, Size: len(app), File: name, SHA256: Digest(app)}
				manifest.Images = append(manifest.Images, bank)
				if i == 0 {
					c.BinaryBytes = len(app)
					c.BinarySHA256 = bank.SHA256
				}
			}
			loaderDigest, fsDigest := Digest(boot), Digest(fs)
			switch tc {
			case "bad bootstrap":
				files["bootstrap.bin"][22] ^= 1
			case "wrong filesystem":
				fs[4096] ^= 1
			case "wrong release hash":
				c.BinarySHA256 = Digest(nil)
			}
			files["ota-manifest.json"], _ = json.Marshal(manifest)
			if tc == "duplicate manifest field" {
				files["ota-manifest.json"] = append([]byte(`{"model":"picoferm",`), files["ota-manifest.json"][1:]...)
			}
			got, err := validateFermUpdate(files, c, loaderDigest, fsDigest)
			if tc != "valid" {
				if err == nil {
					t.Fatal("invalid PicoFerm package accepted")
				}
				return
			}
			if err != nil || got.PicoFerm == nil {
				t.Fatal(err)
			}
			for i, bank := range got.PicoFerm.Banks {
				if i != bank.Bank || !bytes.Equal(bank.Image, files[bank.File]) {
					t.Fatal("bank mismatch")
				}
			}
			for _, single := range got.Files {
				if single != nil {
					t.Fatal("unexpected single-bank or USB payload")
				}
			}
		})
	}
}

func TestPicoFermRejectsUntrustedArchiveMembers(t *testing.T) {
	for _, name := range []string{"bank-0.bin", "../bank-0.bin", "random"} {
		var buf bytes.Buffer
		z := zip.NewWriter(&buf)
		for _, n := range []string{"ota-manifest.json", "bootstrap.bin", "bank-0.bin", "bank-1.bin", name} {
			entry, _ := z.Create(n)
			entry.Write([]byte("fixture"))
		}
		z.Close()
		if _, err := readFermMembers(buf.Bytes()); err == nil {
			t.Fatalf("accepted %q", name)
		}
	}
}

func TestPicoFermLinkedPackage(t *testing.T) {
	path := os.Getenv("PICOFERM_AB_PACKAGE")
	if path == "" {
		t.Skip("Set PICOFERM_AB_PACKAGE to the cross-compiled reference ZIP")
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	files, err := readFermMembers(data)
	if err != nil {
		t.Fatal(err)
	}
	var m fermManifest
	if err := json.Unmarshal(files["ota-manifest.json"], &m); err != nil || len(m.Images) != 2 {
		t.Fatal("missing bank records")
	}
	c := Candidate{Model: "picoferm", ModelFamily: "picoferm", FirmwareVersion: m.Version, OTAProtocol: "esp8266-ab-v1", BinaryBytes: m.Images[0].Size, BinarySHA256: m.Images[0].SHA256}
	payload, err := ReadPicoFermUpdate(data, c)
	if err != nil {
		t.Fatal(err)
	}
	if len(payload.PicoFerm.Banks[1].Image) != m.Images[1].Size {
		t.Fatal("wrong bank payload")
	}
}
