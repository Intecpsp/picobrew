package catalog

import (
	"math"
	"slices"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
)

func TestUnresolvedReviewsKeepTheirEvidenceAndCannotProducePrograms(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	profiles := recipe.Profiles()
	for _, entry := range a.entries {
		review := a.recovered[entry.ID]
		if review.Evidence == "" || review.Review == "" {
			continue
		}
		d, err := a.Detail(entry.ID)
		if err != nil {
			t.Fatal(entry.ID, err)
		}
		if entry.allBrewers || entry.BrewReady || d.BrewReady || d.StockProgram != nil || d.Recipe == nil {
			t.Fatal(entry.ID, "unresolved recipe available to a machine")
		}
		if d.SourceReview == nil || d.SourceReview.Evidence != review.Evidence || d.SourceReview.Blocker != review.Review {
			t.Fatal(entry.ID, "source review was lost")
		}
		reason := "Source program review: " + review.Review
		for _, profile := range profiles {
			plan, err := recipe.Translate(*d.Recipe, profile, 0)
			if err != nil || plan.MachineRecipe != nil || plan.CanDeploy || !slices.Contains(plan.DeploymentBlockers, reason) {
				t.Fatalf("%s/%s: %v %v", entry.ID, profile.ID, err, plan.DeploymentBlockers)
			}
		}
	}
}

func TestAuthorPermittedHopAmountsScaleForEachBrewer(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	const gramsPerOunce = 28.349523125
	for _, tc := range []struct {
		id       string
		mashOz   map[string]float64
		dryNames []string
		drains   []int
	}{
		{"public-823b71d5482840c18854cab4635568a9", map[string]float64{"Amarillo": 1, "Cascade": 0.5}, []string{"Columbus", "Centennial", "Chinook"}, []int{8, 0, 8, 6}},
		{"public-7462ae29eff04c6c97052fd87c21d7b7", map[string]float64{"Cascade": 2}, []string{"Columbus", "Centennial", "Simcoe"}, []int{8, 8}},
	} {
		d, err := a.Detail(tc.id)
		if err != nil {
			t.Fatal(tc.id, err)
		}
		if d.SourceReview == nil || d.SourceReview.Blocker != "" || !strings.Contains(d.Recipe.Notes, d.SourceReview.Evidence) {
			t.Fatal(tc.id, "missing the reviewed option and its instructions")
		}
		for _, profile := range recipe.Profiles() {
			plan, err := recipe.Translate(*d.Recipe, profile, 0)
			if err != nil || plan.MachineRecipe == nil || len(plan.DeploymentBlockers) != 0 {
				t.Fatal(tc.id, profile.ID, err, plan.DeploymentBlockers)
			}
			r := plan.Recipe
			grams := gramsPerOunce * r.BatchLiters / d.Recipe.BatchLiters
			seenMash, seenDry := map[string]bool{}, map[string]bool{}
			for _, in := range r.Ingredients {
				if in.Use != "mash" && in.Use != "dry-hop" || in.Kind != "hop" {
					continue
				}
				oz, ok := tc.mashOz[in.Name]
				seen, minutes := seenMash, 0
				if in.Use == "dry-hop" {
					oz, ok = 1.5, slices.Contains(tc.dryNames, in.Name)
					seen, minutes = seenDry, 5*24*60
				}
				if !ok || seen[in.Name] || in.Unit != "g" || in.Minutes != float64(minutes) || math.Abs(in.Amount-grams*oz) > 1e-8 {
					t.Fatal(tc.id, profile.ID, "unexpected hop amount or contact time", in)
				}
				seen[in.Name] = true
			}
			if len(seenMash) != len(tc.mashOz) || len(seenDry) != len(tc.dryNames) {
				t.Fatal(tc.id, profile.ID, "missing mash or dry hops", seenMash, seenDry)
			}
			var drains []int
			for _, step := range r.Steps {
				if step.Kind == "mash" || step.Kind == "mash-out" {
					drains = append(drains, step.DrainMinutes)
				}
			}
			if !slices.Equal(drains, tc.drains) || len(plan.HopSlotsByMinute) != 4 || plan.HopSlotsByMinute[8] != 4 {
				t.Fatal(tc.id, profile.ID, "incorrect mash drains or late boil additions", drains, plan.HopSlotsByMinute)
			}
			if len(r.Fermentation) != 3 || r.Fermentation[0].Days != 6 || r.Fermentation[1].Days != 8 || r.Fermentation[2].Days != 3 {
				t.Fatal(tc.id, profile.ID, "missing original fermentation plan", r.Fermentation)
			}
		}
	}
}

func TestFinalSaisonNotesCarryThroughToTheBrewPlan(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	for _, id := range []string{"public-caf4155411ac4027974789c0d27791eb", "public-dd877a925a134bc38d7bd137c0d4f33f"} {
		d, err := a.Detail(id)
		if err != nil {
			t.Fatal(err)
		}
		for _, profile := range recipe.Profiles() {
			plan, err := recipe.Translate(*d.Recipe, profile, 0)
			if err != nil || plan.MachineRecipe == nil {
				t.Fatal(id, profile.ID, err, plan.DeploymentBlockers)
			}
			r := plan.Recipe
			if r.BoilMinutes != 90 || plan.HopSlotsByMinute[90] != 1 || plan.HopSlotsByMinute[10] != 2 || len(plan.HopSlotsByMinute) != 2 {
				t.Fatal(id, profile.ID, "lost the 90-minute boil or final 10-minute addition")
			}
			var last []string
			for _, in := range r.Ingredients {
				if (in.Kind == "hop" || in.SlotAddition) && in.Use == "boil" && in.Minutes == 10 {
					last = append(last, in.Name)
				}
			}
			if !slices.Equal(last, []string{"Styrian Goldings", "Saaz", "Whirlfloc", "Yeast Nutrient"}) {
				t.Fatal(id, profile.ID, "incorrect final hop slot", last)
			}
			if len(r.Fermentation) != 3 || r.Fermentation[0].Days != 4 || r.Fermentation[1].Days != 7 ||
				r.Fermentation[1].TargetC != 25 || r.Fermentation[2].Days != 0 || math.Abs(r.Fermentation[2].TargetC-10.0/9) > 1e-9 ||
				r.Carbonation.Method != "natural" || r.Carbonation.Volumes != 3 {
				t.Fatal(id, profile.ID, "lost the author's cold-side plan", r.Fermentation, r.Carbonation)
			}
		}
	}
}
