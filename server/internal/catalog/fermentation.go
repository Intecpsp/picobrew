package catalog

import (
	"encoding/json"
	"fmt"
	"html"
	"slices"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// Earlier catalog bundles kept the final zero-duration setpoint only in source
// metadata. The Hub can represent it as the temperature that ends a timed plan.
func preserveFinalFermentationTarget(d *Detail, reviewed []domain.FermentationStep) {
	r := d.Recipe
	if len(r.Fermentation) == 0 || len(r.Fermentation) >= 32 {
		return
	}
	var source struct {
		FermentationSteps []struct {
			Name                       string
			Temp, Days, Hours, Minutes *float64
		}
	}
	if json.Unmarshal(d.SourceParameters, &source) != nil || len(source.FermentationSteps) != len(r.Fermentation)+1 {
		return
	}
	last := source.FermentationSteps[len(source.FermentationSteps)-1]
	if last.Temp == nil || last.Days == nil || last.Hours == nil || last.Minutes == nil {
		return
	}
	if *last.Days != 0 || *last.Hours != 0 || *last.Minutes != 0 {
		return
	}
	name := strings.TrimSpace(html.UnescapeString(last.Name))
	end := domain.FermentationStep{Name: name, TargetC: (*last.Temp - 32) * 5 / 9}
	if name == "" || len(name) > 512 || strings.ContainsRune(name, '\x00') || end.TargetC < -10 || end.TargetC > 60 {
		return
	}
	if reviewed != nil && (len(reviewed) == 0 || reviewed[len(reviewed)-1] != end) {
		return // An explicit conditioning review supersedes the archived plan.
	}
	r.Fermentation = append(r.Fermentation, end)

	// Remove only the legacy converter's notice for this exact endpoint.
	// The untouched source notes and all other review warnings stay available.
	line := fmt.Sprintf("Unscheduled fermentation phase: %s; source target %g°F; duration 0 d, 0 h, 0 min.", name, *last.Temp)
	if at := strings.LastIndex(r.Notes, "\n"+line+"\n"); at >= 0 {
		r.Notes = r.Notes[:at] + r.Notes[at+len(line)+1:]
	}
	const warning = "A fermentation/cold-crash phase has no usable duration or target. Its original values are retained in notes; no schedule was invented."
	isLegacyWarning := func(w string) bool { return w == warning }
	d.Warnings = slices.DeleteFunc(d.Warnings, isLegacyWarning)
	d.Provenance.Warnings = slices.DeleteFunc(d.Provenance.Warnings, isLegacyWarning)
	if r.Provenance != nil {
		r.Provenance.Warnings = slices.DeleteFunc(r.Provenance.Warnings, isLegacyWarning)
	}
}
