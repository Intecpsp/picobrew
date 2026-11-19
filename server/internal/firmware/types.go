// Package firmware validates and acquires standalone firmware packages. Its
// results establish package integrity, not physical compatibility or a safe
// installation path. This package never contacts or writes to a machine.
package firmware

import "time"

const (
	DefaultRepository = "jeffreydwalter/picobrew-pico-pro-firmware"
	MaxArchiveBytes   = 2 << 20
	MaxExpandedBytes  = 2 << 20
	MaxReportBytes    = 512 << 10
	MaxManifestBytes  = 16 << 10
	MaxBinaryBytes    = 184 << 10
	MaxAssetBytes     = 65024
	MaxSourceBytes    = 1 << 20
	MaxPackages       = 20
)

type Settings struct {
	Repository string `json:"repository"`
	Ref        string `json:"ref"`
}

// Candidate describes a report. The catalog does not assert that the image
// and browser payloads have been downloaded or validated.
type Candidate struct {
	Model           string `json:"model"`
	ModelFamily     string `json:"model_family"`
	FirmwareVersion string `json:"firmware_version"`
	BinarySHA256    string `json:"binary_sha256"`
	BinaryBytes     int    `json:"binary_bytes"`
}

type Catalog struct {
	Repository string      `json:"repository"`
	Ref        string      `json:"ref"`
	Commit     string      `json:"commit"`
	Packages   []Candidate `json:"packages"`
}

// Provenance is produced by the Hub acquisition path, never by an uploaded
// report. A GitHub commit identifies the retrieved files, not hardware testing.
type Provenance struct {
	Source     string `json:"source"`
	Repository string `json:"repository,omitempty"`
	Commit     string `json:"commit,omitempty"`
}

type Package struct {
	Candidate
	ID           string     `json:"id"`
	PackageBytes int        `json:"package_bytes"`
	CreatedAt    time.Time  `json:"created_at"`
	Validation   string     `json:"validation"`
	Provenance   Provenance `json:"provenance"`
}

type MachineFirmware struct {
	DeviceID         string    `json:"device_id"`
	Model            string    `json:"model"`
	CurrentVersion   string    `json:"current_version"`
	InstallSupported bool      `json:"install_supported"`
	Reason           string    `json:"reason"`
	Packages         []Package `json:"packages"`
}

// Error contains a bounded, user-facing diagnostic, without a remote response
// body or an uploaded filename. Transport errors are deliberately sanitized.
type Error struct {
	Code    string
	Message string
}

func (e *Error) Error() string { return e.Message }

func invalid(message string) error {
	return &Error{Code: "firmware_package_invalid", Message: message}
}
