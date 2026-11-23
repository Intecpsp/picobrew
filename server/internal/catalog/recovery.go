package catalog

import (
	"fmt"
	"maps"
	"slices"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// The generated corrections apply only to the specific archived source revision.
type recipeRecovery struct {
	Water        map[string]float64        `json:"water,omitempty"`
	Edition      string                    `json:"edition,omitempty"`
	SourceSHA256 string                    `json:"source_sha256"`
	Resolved     []string                  `json:"resolved"`
	Ingredients  []domain.Ingredient       `json:"ingredients"`
	BatchLimit   bool                      `json:"batch_limit"`
	Review       string                    `json:"review"`
	Evidence     string                    `json:"evidence"`
	Warnings     []string                  `json:"warnings"`
	Steps        []domain.RecipeStep       `json:"steps"`
	BoilMinutes  *float64                  `json:"boil_minutes"`
	BatchLiters  *float64                  `json:"batch_liters"`
	Efficiency   *float64                  `json:"efficiency"`
	SourceYeast  string                    `json:"source_yeast"`
	Fermentation []domain.FermentationStep `json:"fermentation"`
	Carbonation  *domain.Carbonation       `json:"carbonation"`
}

func (fix recipeRecovery) apply(d *Detail) error {
	r := d.Recipe
	if r == nil || r.Provenance == nil || d.SourceID != "public-recipes" || d.SourceRecords[0].SHA256 != fix.SourceSHA256 || r.Provenance.SHA256 != fix.SourceSHA256 {
		return fmt.Errorf("catalog: recovered source mismatch for %s", d.ID)
	}
	if fix.Ingredients != nil {
		r.Ingredients = slices.Clone(fix.Ingredients)
	}
	if fix.Steps != nil {
		r.Steps = slices.Clone(fix.Steps)
	}
	if fix.BoilMinutes != nil {
		r.BoilMinutes = *fix.BoilMinutes
	}
	if fix.BatchLiters != nil {
		r.BatchLiters = *fix.BatchLiters
	}
	if fix.Efficiency != nil {
		r.Efficiency = *fix.Efficiency
	}
	if fix.SourceYeast != "" {
		r.SourceYeast = fix.SourceYeast
	}
	if fix.Fermentation != nil {
		r.Fermentation = slices.Clone(fix.Fermentation)
	}
	if fix.Water != nil {
		r.Water = maps.Clone(fix.Water)
	}
	if fix.Carbonation != nil {
		r.Carbonation = *fix.Carbonation
	}
	r.Provenance.Warnings = slices.DeleteFunc(r.Provenance.Warnings, func(w string) bool { return slices.Contains(fix.Resolved, w) })
	evidence := "The adapted ingredient bill uses PicoBrew’s archived loading sheet. See the original recipe notes for manual additions and fermentation guidance."
	if fix.Evidence != "" {
		evidence = fix.Evidence
	} else {
		r.Provenance.Warnings = append(r.Provenance.Warnings, evidence)
	}
	if fix.Edition != "" {
		d.Edition = fix.Edition
		r.Notes = "Hub adaptation (" + fix.Edition + "). " + evidence + "\n\n" + r.Notes
	} else {
		r.Notes += "\n\n" + evidence
	}
	r.Provenance.Warnings = append(r.Provenance.Warnings, fix.Warnings...)
	if fix.Review != "" {
		r.Provenance.Warnings = append(r.Provenance.Warnings, "Source program review: "+fix.Review)
	}
	if fix.BatchLimit {
		r.Provenance.MaxBatchLiters = 13.248 // Whole milliliters below the exclusive 3.5 US gallon limit.
	}
	d.Provenance = *r.Provenance
	d.Warnings = slices.DeleteFunc(d.Warnings, func(w string) bool { return slices.Contains(fix.Resolved, w) })
	d.Warnings = append(d.Warnings, fix.Warnings...)
	d.Warnings = append(d.Warnings, evidence)
	if fix.Review != "" {
		d.Warnings = append(d.Warnings, fix.Review)
	}
	return nil
}
