package catalog

import (
	"fmt"
	"slices"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// The generated corrections apply only to the specific archived source revision.
type recipeRecovery struct {
	SourceSHA256 string              `json:"source_sha256"`
	Resolved     []string            `json:"resolved"`
	Ingredients  []domain.Ingredient `json:"ingredients"`
	BatchLimit   bool                `json:"batch_limit"`
	Review       string              `json:"review"`
}

func (fix recipeRecovery) apply(d *Detail) error {
	r := d.Recipe
	if r == nil || r.Provenance == nil || d.SourceID != "public-recipes" || d.SourceRecords[0].SHA256 != fix.SourceSHA256 || r.Provenance.SHA256 != fix.SourceSHA256 {
		return fmt.Errorf("catalog: recovered source mismatch for %s", d.ID)
	}
	if fix.Ingredients != nil {
		r.Ingredients = slices.Clone(fix.Ingredients)
	}
	r.Provenance.Warnings = slices.DeleteFunc(r.Provenance.Warnings, func(w string) bool { return slices.Contains(fix.Resolved, w) })
	evidence := "The adapted ingredient bill uses PicoBrew’s archived loading sheet. See the original recipe notes for manual additions and fermentation guidance."
	r.Notes += "\n\n" + evidence
	r.Provenance.Warnings = append(r.Provenance.Warnings, evidence)
	if fix.Review != "" {
		r.Provenance.Warnings = append(r.Provenance.Warnings, "Source program review: "+fix.Review)
	}
	if fix.BatchLimit {
		r.Provenance.MaxBatchLiters = 13.248 // Whole milliliters below the exclusive 3.5 US gallon limit.
	}
	d.Provenance = *r.Provenance
	d.Warnings = slices.DeleteFunc(d.Warnings, func(w string) bool { return slices.Contains(fix.Resolved, w) })
	d.Warnings = append(d.Warnings, evidence)
	if fix.Review != "" {
		d.Warnings = append(d.Warnings, fix.Review)
	}
	return nil
}
