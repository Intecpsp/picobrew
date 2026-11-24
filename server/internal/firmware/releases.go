package firmware

import (
	"archive/zip"
	"bytes"
	"context"
	"io"
	"io/fs"
	"slices"
	"strings"
	"time"
)

const MaxReleaseBytes = 12 << 20

type Release struct {
	Candidate
	Kind          string `json:"kind"`
	SourceCommit  string `json:"source_commit"`
	ArchiveSHA256 string `json:"archive_sha256"`
}
type ReleaseCatalog struct {
	Settings
	ReleaseChannel string    `json:"release_channel,omitempty"`
	Commit         string    `json:"commit"`
	Checked        time.Time `json:"checked"`
	Releases       []Release `json:"releases"`
}

// Withdrawn reports a previously staged public package that its source no longer lists.
// User-supplied archives and packages from other sources retain their original provenance.
func (c ReleaseCatalog) Withdrawn(p Package) bool {
	if c.ReleaseChannel != "stable" || p.Provenance.Source != "library" || p.Provenance.Repository != c.Repository {
		return false
	}
	return !slices.ContainsFunc(c.Releases, func(r Release) bool { return r.ArchiveSHA256 == p.ID })
}

func (r Release) valid() bool {
	if !ValidCommit(r.SourceCommit) || !ValidID(r.ArchiveSHA256) || !ValidID(r.BinarySHA256) || r.BinaryBytes <= 0 || r.BinaryBytes > MaxReleaseBytes || !versionPattern.MatchString(r.FirmwareVersion) {
		return false
	}
	if r.OTAProtocol != "" {
		return r.WirelessPackage()
	}
	switch r.Kind {
	case "standalone":
		return ValidVariant(r.Model, r.DisplayVariant) && r.ModelFamily == modelFamily(r.Model) && (r.HubOTAProtocol == 0 || r.SupportsCC3200OTA())
	case "historical":
		return r.Model == "pico-s-pro" && r.ModelFamily == r.Model && r.DisplayVariant == "" && r.HubOTAProtocol == 0
	case "reference":
		return (r.Model == "z-series" || r.Model == "picostill" || r.Model == "picoferm") && r.ModelFamily == r.Model && r.DisplayVariant == "" && r.HubOTAProtocol == 0
	}
	return false
}
func (g *GitHub) Releases(ctx context.Context, cfg Settings) (ReleaseCatalog, error) {
	var out ReleaseCatalog
	cfg, err := NormalizeSettings(cfg)
	if err != nil {
		return out, err
	}
	sha, err := g.resolve(ctx, cfg)
	if err != nil {
		return out, err
	}
	data, err := g.get(ctx, "https://raw.githubusercontent.com/"+cfg.Repository+"/"+sha+"/firmware/releases.json", 2<<20)
	if err != nil {
		return out, err
	}
	var index struct {
		ReleaseChannel string    `json:"release_channel,omitempty"`
		Schema         int       `json:"schema_version"`
		Releases       []Release `json:"releases"`
	}
	if decodeJSON(data, &index, true) != nil || index.Schema != 1 || (index.ReleaseChannel != "" && index.ReleaseChannel != "stable") || index.Releases == nil {
		return out, invalid("This source has no valid firmware version catalog")
	}
	ids := map[string]bool{}
	for _, r := range index.Releases {
		if !r.valid() || ids[r.ArchiveSHA256] {
			return out, invalid("The firmware version catalog contains an invalid or repeated release")
		}
		ids[r.ArchiveSHA256] = true
	}
	return ReleaseCatalog{Settings: cfg, ReleaseChannel: index.ReleaseChannel, Commit: sha, Checked: time.Now().UTC(), Releases: index.Releases}, nil
}
func (g *GitHub) DownloadRelease(ctx context.Context, c ReleaseCatalog, r Release) ([]byte, error) {
	cfg, err := NormalizeSettings(c.Settings)
	if err != nil {
		return nil, err
	}
	if !r.valid() || !ValidCommit(c.Commit) {
		return nil, invalid("Invalid release selection")
	}
	path := "https://raw.githubusercontent.com/" + cfg.Repository + "/" + c.Commit + "/firmware/" + r.ModelFamily + "/modified/releases/" + r.Model + "/"
	if r.DisplayVariant != "" {
		path += r.DisplayVariant + "/"
	}
	data, err := g.get(ctx, path+r.FirmwareVersion+".zip", MaxReleaseBytes)
	if err != nil {
		return nil, err
	}
	if err = ValidateRelease(data, r); err != nil {
		return nil, err
	}
	return data, nil
}
func ValidateRelease(data []byte, r Release) error {
	if !r.valid() || len(data) > MaxReleaseBytes || Digest(data) != r.ArchiveSHA256 {
		return invalid("The downloaded archive does not match the selected release")
	}
	if r.SupportsFermOTA() {
		_, err := ReadPicoFermUpdate(data, r.Candidate)
		return err
	}
	if r.Kind == "standalone" {
		candidate, err := ValidateArchive(data)
		if err != nil {
			return err
		}
		if candidate != r.Candidate {
			return invalid("The archive declares a different firmware target, version or digest")
		}
		return nil
	}
	// Validate download-only archives and USB reference packages with an optional
	// application-only update. Validate the member
	// names, CRCs, bounds and published application digest without extraction.
	reader, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil || len(reader.File) > 64 {
		return invalid("Invalid reference firmware archive")
	}
	image := "application.bin"
	if r.Kind == "historical" {
		image = "pico-s-pro-standalone.bin"
	}
	seen := map[string]bool{}
	remaining := int64(64 << 20)
	for _, f := range reader.File {
		if !fs.ValidPath(f.Name) || strings.ContainsAny(f.Name, "\\:") || !f.Mode().IsRegular() || seen[f.Name] || f.UncompressedSize64 > uint64(remaining) {
			return invalid("Invalid reference archive member")
		}
		seen[f.Name] = true
		src, err := f.Open()
		if err != nil {
			return invalid("Unreadable reference archive")
		}
		limit := int64(f.UncompressedSize64)
		var contents bytes.Buffer
		var dest io.Writer = io.Discard
		if f.Name == image {
			if limit != int64(r.BinaryBytes) {
				src.Close()
				return invalid("Incorrect application size")
			}
			dest = &contents
		}
		n, err := io.Copy(dest, io.LimitReader(src, limit+1))
		src.Close()
		if err != nil || n != limit {
			return invalid("Invalid reference archive content")
		}
		if f.Name == image && Digest(contents.Bytes()) != r.BinarySHA256 {
			return invalid("Incorrect application digest")
		}
		remaining -= n
	}
	if !seen[image] {
		return invalid("Missing reference application")
	}
	if r.SupportsFactoryOTA() {
		_, err = ReadFactoryUpdate(data, r.Candidate)
		return err
	}
	return nil
}
