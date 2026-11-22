package recipe

import (
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"strings"
)

// SourceYeast also reads the attribution saved in older PicoBrew catalog copies.
func SourceYeast(r domain.Recipe) string {
	if r.SourceYeast != "" {
		return r.SourceYeast
	}
	if r.Provenance == nil || r.Provenance.SourceID != "public-recipes" {
		return ""
	}
	for line := range strings.SplitSeq(r.Notes, "\n") {
		value, ok := strings.CutPrefix(line, "Yeast: ")
		if !ok {
			continue
		}
		name, _, found := strings.Cut(value, "; quantity unspecified.")
		if found {
			return strings.TrimSpace(name)
		}
	}
	return ""
}
