package catalog

import (
	"math"
	"slices"
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

// Clony's known mash additions survive, but the author leaves the division
// of the one-ounce dry-hop dose ambiguous across three varieties.
func TestClonyPreservesMeasuredMashHopsAndWithholdsAmbiguousDryHopAmounts(t *testing.T) {
	a, err := Open()
	if err != nil {
		t.Fatal(err)
	}
	d, err := a.Detail("public-823b71d5482840c18854cab4635568a9")
	if err != nil {
		t.Fatal(err)
	}
	var mash []string
	for _, in := range d.Recipe.Ingredients {
		if in.Kind == "hop" && in.Use == "mash" {
			mash = append(mash, in.Name)
		}
		if in.Use == "dry-hop" {
			t.Fatal("unconfirmed allocation", in)
		}
	}
	if !slices.Equal(mash, []string{"Amarillo", "Cascade"}) || d.SourceReview == nil || d.SourceReview.Blocker == "" || d.BrewReady {
		t.Fatal("incomplete source review", mash, d.SourceReview)
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
