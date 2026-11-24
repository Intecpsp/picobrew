package firmware

import (
	"archive/zip"
	"bytes"
	"encoding/binary"
	"io"
)

const (
	picoFermBootstrapSHA256 = "d6e1a05f85732b1bcc94400c8c0f5fb505bb9c5e5d69820a93145620705eb2de"
	picoFermBlankFS         = "18a484702761fe942d97a59918fabc46d3f375c6e7c33ad252fbf612797aff48"
	picoFermBankSize        = 0x7d000
)

type PicoFermBank struct {
	Bank   int    `json:"bank"`
	Offset int    `json:"offset"`
	Size   int    `json:"size"`
	File   string `json:"file"`
	SHA256 string `json:"sha256"`
	Image  []byte `json:"-"`
}

type PicoFermUpdate struct {
	BootstrapSHA256 string
	Banks           [2]PicoFermBank
}

type fermManifest struct {
	Schema          int            `json:"schema_version"`
	Protocol        string         `json:"protocol"`
	Model           string         `json:"model"`
	Version         string         `json:"version"`
	BootstrapSHA256 string         `json:"bootstrap_sha256"`
	Images          []PicoFermBank `json:"images"`
}

type fermSegment struct {
	start uint32
	end   uint32
}

// Both the bootstrap and applications use the pinned SDK's ESP8266 image
// encoding. Reject truncated, overlapping-by-wrap, or unchecked trailing data.
func decodeFermSegments(data []byte, count int) (uint32, []fermSegment, bool) {
	if len(data) < 16 || len(data)%16 != 0 || !bytes.Equal(data[:4], []byte{0xe9, byte(count), 2, 0x30}) {
		return 0, nil, false
	}
	pc := binary.LittleEndian.Uint32(data[4:])
	pos := 8
	segments := make([]fermSegment, 0, count)
	checksum := byte(0xef)
	for i := 0; i < count; i++ {
		if len(data)-pos < 8 {
			return 0, nil, false
		}
		start := binary.LittleEndian.Uint32(data[pos:])
		size := binary.LittleEndian.Uint32(data[pos+4:])
		pos += 8
		if size == 0 || size%4 != 0 || start%4 != 0 || size > uint32(len(data)-pos) || start+size <= start {
			return 0, nil, false
		}
		for _, b := range data[pos : pos+int(size)] {
			checksum ^= b
		}
		segments = append(segments, fermSegment{start, start + size})
		pos += int(size)
	}
	if len(data) != (pos|15)+1 || data[len(data)-1] != checksum {
		return 0, nil, false
	}
	for _, b := range data[pos : len(data)-1] {
		if b != 0 {
			return 0, nil, false
		}
	}
	return pc, segments, true
}

func readFermMembers(archive []byte) (map[string][]byte, error) {
	limits := map[string]uint64{"ota-manifest.json": 4096, "bootstrap.bin": 4096, "bank-0.bin": picoFermBankSize, "bank-1.bin": picoFermBankSize, "spiffs.bin": 0xfb000}
	reader, err := zip.NewReader(bytes.NewReader(archive), int64(len(archive)))
	if err != nil || len(reader.File) != len(limits) {
		return nil, invalid("Invalid PicoFerm update package")
	}
	files := make(map[string][]byte, len(limits))
	for _, f := range reader.File {
		limit := limits[f.Name]
		if limit == 0 || files[f.Name] != nil || !f.Mode().IsRegular() || f.UncompressedSize64 == 0 || f.UncompressedSize64 > limit {
			return nil, invalid("Invalid PicoFerm package member")
		}
		r, err := f.Open()
		if err != nil {
			return nil, invalid("Unreadable PicoFerm package member")
		}
		data, err := io.ReadAll(io.LimitReader(r, int64(limit)+1))
		closeErr := r.Close()
		if err != nil || closeErr != nil || uint64(len(data)) != f.UncompressedSize64 {
			return nil, invalid("Damaged PicoFerm package member")
		}
		files[f.Name] = data
	}
	return files, nil
}

// ReadPicoFermUpdate verifies all components, then returns only the two bank
// images and the bootstrap fingerprint required to select one on the device.
func ReadPicoFermUpdate(archive []byte, expected Candidate) (UpdatePayload, error) {
	if expected.Model != "picoferm" || expected.ModelFamily != "picoferm" || expected.DisplayVariant != "" ||
		expected.OTAProtocol != "esp8266-ab-v1" || expected.HubOTAProtocol != 0 ||
		!ValidID(expected.BinarySHA256) || !versionPattern.MatchString(expected.FirmwareVersion) ||
		expected.BinaryBytes <= 0 || expected.BinaryBytes > picoFermBankSize || len(archive) > 2<<20 {
		return UpdatePayload{}, invalid("Unsupported PicoFerm update")
	}
	files, err := readFermMembers(archive)
	if err != nil {
		return UpdatePayload{}, err
	}
	return validateFermUpdate(files, expected, picoFermBootstrapSHA256, picoFermBlankFS)
}

func validateFermUpdate(files map[string][]byte, expected Candidate, bootstrapDigest, blankFSDigest string) (UpdatePayload, error) {
	var m fermManifest
	if decodeJSON(files["ota-manifest.json"], &m, true) != nil || m.Schema != 1 || m.Model != expected.Model ||
		m.Version != expected.FirmwareVersion || m.Protocol != expected.OTAProtocol || len(m.Images) != 2 {
		return UpdatePayload{}, invalid("PicoFerm model, version or protocol differs from the selected release")
	}
	boot := files["bootstrap.bin"]
	pc, segs, ok := decodeFermSegments(boot, 1)
	if !ok || segs[0].start != 0x4010f000 || segs[0].end > 0x40110000 || pc < segs[0].start || pc >= segs[0].end || pc%4 != 0 ||
		m.BootstrapSHA256 != bootstrapDigest || Digest(boot) != bootstrapDigest ||
		len(files["spiffs.bin"]) != 0xfb000 || Digest(files["spiffs.bin"]) != blankFSDigest {
		return UpdatePayload{}, invalid("Unsupported PicoFerm bootstrap or USB filesystem")
	}
	updates := &PicoFermUpdate{BootstrapSHA256: bootstrapDigest}
	var marker [32]byte
	if len("PicoFerm-AB1:")+len(m.Version) >= len(marker) {
		return UpdatePayload{}, invalid("Invalid PicoFerm build version")
	}
	copy(marker[:], "PicoFerm-AB1:"+m.Version)
	for i, bank := range m.Images {
		name := []string{"bank-0.bin", "bank-1.bin"}[i]
		base := 0x3000 + i*0x80000
		data := files[name]
		pc, seg, ok := decodeFermSegments(data, 4)
		if bank.Bank != i || bank.Offset != base || bank.File != name || bank.Size != len(data) || Digest(data) != bank.SHA256 ||
			!ok || len(data) < 48 || seg[0].start != uint32(0x40200010+base) || seg[0].end-seg[0].start < 32 ||
			seg[1].start != 0x40100000 || seg[1].end > 0x40108000 || pc < seg[1].start || pc >= seg[1].end || pc%4 != 0 ||
			seg[2].start < 0x3ffe8000 || seg[2].end > seg[3].start || seg[3].end > 0x3fff8000 || !bytes.Equal(marker[:], data[16:48]) {
			return UpdatePayload{}, invalid("PicoFerm bank image differs from its intended slot, version or digest")
		}
		if i == 0 && (bank.Size != expected.BinaryBytes || bank.SHA256 != expected.BinarySHA256) {
			return UpdatePayload{}, invalid("PicoFerm bank zero differs from the release index")
		}
		bank.Image = data
		updates.Banks[i] = bank
	}
	return UpdatePayload{Candidate: expected, PicoFerm: updates}, nil
}
