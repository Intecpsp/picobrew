package catalog

import (
	"math"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
)

func TestBaysWaterWarmHoldsAndManualChilling(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		id       string
		sourTemp int
	}{
		{"public-3f7d55adf84343e3ae765ac47fc168b3", 90},
		{"public-834af9e5c5644d4ba6efb256e4e1e150", 100},
	} {
		d, err := a.Detail(tc.id)
		if err != nil {
			t.Fatal(err)
		}
		r := d.Recipe
		if r == nil || r.BoilMinutes != 0 || d.SourceReview == nil || d.SourceReview.Blocker != "" ||
			!strings.Contains(r.Notes, "After the hot-side program") || !strings.Contains(r.SourceYeast, "WLP645") {
			t.Fatalf("%s lost the manual chill or second pitch", tc.id)
		}
		if len(r.Fermentation) != 2 || int(math.Round(r.Fermentation[0].TargetC*1.8+32)) != tc.sourTemp ||
			r.Fermentation[0].Days != 4 || r.Fermentation[1].Days != 30 {
			t.Fatalf("%s lost the authored souring temperature or fermentation plan", tc.id)
		}
		var hops int
		for _, in := range r.Ingredients {
			if in.Kind == "hop" {
				hops++
				if in.Use != "mash" || in.Minutes != 0 || in.Unit != "g" || math.Abs(in.Amount-0.65*28.349523125) > 1e-8 {
					t.Fatal(tc.id, in)
				}
			}
		}
		if hops != 1 {
			t.Fatal(tc.id, "incorrect number of mash hops")
		}
		for _, p := range recipe.Profiles() {
			plan, err := recipe.Translate(*r, p, 0)
			if err != nil || len(plan.DeploymentBlockers) > 0 || plan.MachineRecipe == nil || len(plan.HopSlotsByMinute) != 0 {
				t.Fatal(tc.id, p.ID, err, plan.DeploymentBlockers)
			}
			program := plan.MachineRecipe
			if len(program.Steps) != 5 {
				t.Fatal(tc.id, p.ID, "unexpected machine-controlled stages", program.Steps)
			}
			for i, want := range []struct{ route, tempF, hold, drain int }{
				{1, 149, 0, 0}, {2, 149, 90, 8}, {1, 180, 0, 0}, {1, 180, 9, 0}, {3, 180, 10, 5},
			} {
				s := program.Steps[i]
				if p.ID == "zymatic" {
					want.route-- // The stock protocol numbers its first route zero.
				}
				if s.Location != want.route || s.TargetF != want.tempF || s.Minutes != want.hold || s.DrainMinutes != want.drain {
					t.Fatal(tc.id, p.ID, i, s, want)
				}
			}
		}
	}
}
