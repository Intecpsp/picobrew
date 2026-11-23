package catalog

import (
	"math"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
)

func TestDeclaredHopSchedulesFitStockKegs(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		id            string
		first, second int
		zFill         float64
	}{
		{"public-b0b8996eb0ae446a91b89942e087c3df", 90, 60, 13.9980},
		{"public-18876c309fc4471995f5e74e99a5980e", 40, 5, 14.2237},
		{"public-2ab67da295bc4bdba33a50878a60c710", 60, 10, 8.0272},
	} {
		d, err := a.Detail(tc.id)
		if err != nil || d.Recipe == nil || d.Edition != "Declared hop schedule" {
			t.Fatalf("missing declared edition for %s: %v", tc.id, err)
		}
		for _, p := range recipe.Profiles() {
			plan, err := recipe.Translate(*d.Recipe, p, 0)
			if err != nil || plan.MachineRecipe == nil || len(plan.DeploymentBlockers) > 0 {
				t.Fatal(tc.id, p.ID, err, plan.DeploymentBlockers)
			}
			expected := tc.zFill
			switch p.ID {
			case "pico-c":
				expected = 5.2
			case "pico-pro", "pico-s":
				expected = 5.204941203
			}
			var fill float64
			scale := plan.Recipe.BatchLiters / d.Recipe.BatchLiters
			for i, in := range plan.Recipe.Ingredients {
				if math.Abs(in.Amount-d.Recipe.Ingredients[i].Amount*scale) > 1e-8 {
					t.Fatal(tc.id, p.ID, "changed ingredient proportion", in)
				}
				if in.Name == "Scaled source water (reference)" {
					fill += in.Amount
				}
			}
			if math.Abs(fill-expected) > 0.001 {
				t.Fatalf("%s %s starting water %.4f L, want %.4f L", tc.id, p.ID, fill, expected)
			}
			if plan.HopSlotsByMinute[tc.first] != 1 || plan.HopSlotsByMinute[tc.second] != 2 || len(plan.HopSlotsByMinute) != 2 {
				t.Fatal(tc.id, p.ID, "incorrect first contact in declared cages", plan.HopSlotsByMinute)
			}
			remaining := int(d.Recipe.BoilMinutes)
			var wet int
			for i, s := range plan.MachineRecipe.Steps {
				route := s.Location
				if p.ID == "zymatic" {
					route++
				}
				switch d.Recipe.Steps[i].Kind {
				case "boil", "hop":
					if s.TargetF != 202 || route > 4 {
						t.Fatal(tc.id, p.ID, "incorrect boil target or route", s)
					}
					if route > 2+wet {
						wet++
						if route != 2+wet || plan.HopSlotsByMinute[remaining] != wet {
							t.Fatal(tc.id, p.ID, "premature hop contact", s, remaining)
						}
					}
					remaining -= s.Minutes
				default:
					if route > 2 {
						t.Fatal(tc.id, p.ID, "hop cage wetted before boil", s)
					}
				}
			}
			last := plan.MachineRecipe.Steps[len(plan.MachineRecipe.Steps)-1]
			if remaining != 0 || wet != 2 || last.DrainMinutes != 5 {
				t.Fatal(tc.id, p.ID, remaining, wet, last)
			}
		}
	}
}
