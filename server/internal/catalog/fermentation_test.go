package catalog

import (
	"slices"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestArchivedFinalFermentationTemperature(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	read := func(id string) Detail {
		t.Helper()
		d, err := a.Detail(id)
		if err != nil {
			t.Fatal(err)
		}
		return d
	}
	// An otherwise complete source plan ends at its recorded temperature.
	d := read("public-93175b1f138a4f0b89ba401ece1ae43d")
	phases := d.Recipe.Fermentation
	end := domain.FermentationStep{Name: "Crash Chill", TargetC: (44. - 32) * 5 / 9}
	if len(phases) < 2 || phases[len(phases)-1] != end {
		t.Fatalf("missing final setpoint: %#v", phases)
	}
	obsolete := slices.ContainsFunc(d.Provenance.Warnings, func(s string) bool {
		return strings.Contains(s, "cold-crash phase has no usable duration")
	})
	if obsolete || strings.Contains(d.Recipe.Notes, "Unscheduled fermentation phase: Crash Chill") {
		t.Fatal("retained an obsolete warning for the recovered endpoint")
	}
	preserveFinalFermentationTarget(&d, nil)
	if len(d.Recipe.Fermentation) != len(phases) {
		t.Fatal("endpoint repeated")
	}
	// This source has two consecutive untimed phases; there is no timed plan.
	d = read("public-4102ee640da94a32bbf310f00c9b6947")
	if len(d.Recipe.Fermentation) != 0 || !strings.Contains(d.Recipe.Notes, "Unscheduled fermentation phase: Crash Chill") {
		t.Fatal("scheduled an ambiguous intermediate hold")
	}
	// The brewer's notes specify 34°F, superseding the form's 44°F value.
	d = read("public-caf4155411ac4027974789c0d27791eb")
	phases = d.Recipe.Fermentation
	if len(phases) != 3 || phases[2].TargetC != (34.-32)*5/9 || phases[2].Days != 0 {
		t.Fatalf("lost author override: %#v", phases)
	}
}

func TestTerminalFermentationRequiresRecordedDurationAndTemperature(t *testing.T) {
	for _, end := range []string{
		`{"Name":"Chill","Temp":40,"Days":0,"Hours":0}`,
		`{"Name":"Chill","Temp":40,"Days":0,"Hours":1,"Minutes":0}`,
		`{"Name":"Chill","Days":0,"Hours":0,"Minutes":0}`,
		`{"Name":"Chill","Temp":0,"Days":0,"Hours":0,"Minutes":0}`,
	} {
		const first = `{"FermentationSteps":[{"Name":"Primary","Temp":68,"Days":7,"Hours":0,"Minutes":0},`
		d := Detail{
			Recipe:           &domain.Recipe{Fermentation: []domain.FermentationStep{{Name: "Primary", TargetC: 20, Days: 7}}},
			SourceParameters: []byte(first + end + `]}`),
		}
		preserveFinalFermentationTarget(&d, nil)
		if len(d.Recipe.Fermentation) != 1 {
			t.Errorf("synthesized terminal stage for %s", end)
		}
	}
}
