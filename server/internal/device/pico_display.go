package device

import (
	"context"
	"encoding/hex"
	"errors"
	"net/http"
	"reflect"
	"strconv"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func displayInvalid() error {
	return &Error{Code: "invalid_response", Message: "Device display geometry or page sequence is unsupported"}
}

func validateDisplay(result domain.Display) error {
	if result.Width != 128 || (result.Height != 32 && result.Height != 64) {
		return displayInvalid()
	}
	logicalBytes := result.Width * ((result.Height + 7) / 8)
	limit := 1024
	if result.Format == "row-gray4-msb-hex" {
		if result.Height != 64 || result.Rotation == nil {
			return displayInvalid()
		}
		logicalBytes, limit = 4096, 4096
	} else if result.Format != "page-lsb-hex" {
		return displayInvalid()
	}
	storage := result.StorageBytes
	if storage == 0 {
		storage = logicalBytes
	}
	if result.TotalBytes != 0 && result.TotalBytes != storage {
		return displayInvalid()
	}
	if result.Rotation != nil && *result.Rotation != 0 && *result.Rotation != 2 {
		return displayInvalid()
	}
	if result.TransferBytes != 0 && (result.TransferBytes < logicalBytes || result.TransferBytes > storage) {
		return displayInvalid()
	}
	if storage < logicalBytes || storage > limit || len(result.Data) != storage*2 {
		return displayInvalid()
	}
	if _, err := hex.DecodeString(result.Data); err != nil {
		return displayInvalid()
	}
	return nil
}

// Assemble one revision of the real Z frame under the same bounded connection
// gate as legacy mono reads. A changed frame retries the complete read only;
// malformed metadata never becomes a partially valid browser image.
func (p *pico) Display(ctx context.Context) (domain.Display, error) {
	if err := p.lock(ctx); err != nil {
		return domain.Display{}, err
	}
	defer p.unlock()
	if p.capabilities != nil && !p.capabilities.Supports("front_panel") && !p.capabilities.Supports("display") {
		return domain.Display{}, &Error{StatusCode: 404, Code: "unsupported_capability", Message: "This device has no front panel"}
	}
	for attempt := 0; attempt < 3; attempt++ {
		result, err := p.readDisplay(ctx)
		var failure *Error
		if err == nil || !errors.As(err, &failure) || failure.StatusCode != 409 || failure.Code != "display_changed" || attempt == 2 {
			return result, err
		}
	}
	return domain.Display{}, displayInvalid()
}
func (p *pico) readDisplay(ctx context.Context) (domain.Display, error) {
	var result domain.Display
	if err := p.request(ctx, http.MethodGet, "/api/v1/display", nil, &result, 200); err != nil {
		return result, err
	}
	if result.NextOffset == nil {
		return result, validateDisplay(result)
	}
	if result.Offset == nil || *result.Offset != 0 || result.Revision == nil || result.TotalBytes < 1 || result.TotalBytes > 4096 ||
		len(result.Data) == 0 || len(result.Data) > 2048 || len(result.Data)%2 != 0 || *result.NextOffset != len(result.Data)/2 || *result.NextOffset >= result.TotalBytes {
		return domain.Display{}, displayInvalid()
	}
	if _, err := hex.DecodeString(result.Data); err != nil {
		return domain.Display{}, displayInvalid()
	}
	for offset := *result.NextOffset; offset < result.TotalBytes; {
		var page domain.Display
		path := "/api/v1/display/" + strconv.Itoa(offset) + "?revision=" + strconv.FormatUint(uint64(*result.Revision), 10)
		if err := p.request(ctx, http.MethodGet, path, nil, &page, 200); err != nil {
			return domain.Display{}, err
		}
		if page.Offset == nil || *page.Offset != offset || page.TotalBytes != result.TotalBytes || !reflect.DeepEqual(page.Revision, result.Revision) ||
			page.Width != result.Width || page.Height != result.Height || page.Format != result.Format || !reflect.DeepEqual(page.Rotation, result.Rotation) ||
			page.StorageBytes != result.StorageBytes || page.TransferBytes != result.TransferBytes || page.DisplayVariant != result.DisplayVariant ||
			len(page.Data) == 0 || len(page.Data) > 2048 || len(page.Data)%2 != 0 || offset+len(page.Data)/2 > result.TotalBytes {
			return domain.Display{}, displayInvalid()
		}
		if _, err := hex.DecodeString(page.Data); err != nil {
			return domain.Display{}, displayInvalid()
		}
		result.Data += page.Data
		offset += len(page.Data) / 2
		if offset < result.TotalBytes && (page.NextOffset == nil || *page.NextOffset != offset) || offset == result.TotalBytes && page.NextOffset != nil {
			return domain.Display{}, displayInvalid()
		}
	}
	result.Offset = nil
	result.NextOffset = nil
	return result, validateDisplay(result)
}
