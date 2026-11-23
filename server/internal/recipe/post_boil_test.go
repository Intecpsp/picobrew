package recipe_test

import (
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
)

func TestRoutedHoldCannotSilentlyDiscardPostBoilIngredients(t *testing.T) {
	for _, profile := range recipe.Profiles() {
		r := source()
		r.SourceModel = profile.ID
		r.Steps[1].TargetC = 95
		r.Steps = append(r.Steps, domain.RecipeStep{Name: "Late hold", Kind: "hold", TargetC: 80, Minutes: 15, Location: 4})
		for _, addition := range []domain.Ingredient{
			{Name: "Centennial", Kind: "hop", Use: "aroma", Amount: 10, Unit: "g", Minutes: 15},
			{Name: "Citrus peel", Kind: "misc", Use: "whirlpool", Amount: 7, Unit: "g", Minutes: 15},
			{Name: "Cascade", Kind: "hop", Use: "Flameout", Amount: 5, Unit: "g"},
			{Name: "Mosaic", Kind: "hop", Use: "boil", Amount: 8, Unit: "g", Minutes: 0},
		} {
			edited := r
			edited.Ingredients = append(append([]domain.Ingredient(nil), r.Ingredients...), addition)
			for _, adapt := range []func(domain.Recipe, domain.Profile, float64) (domain.Adapted, error){recipe.Translate, recipe.Adapt} {
				plan, err := adapt(edited, profile, 5)
				if err != nil || plan.CanDeploy || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), addition.Name) || len(plan.Recipe.Ingredients) != len(edited.Ingredients) {
					t.Fatalf("%s lost or accepted %s: %v, %v", profile.ID, addition.Name, err, plan.DeploymentBlockers)
				}
			}
			if profile.ID == "zymatic" {
				if _, err := recipe.StockZymaticProgram(edited, profile); err == nil || !strings.Contains(err.Error(), addition.Name) {
					t.Fatalf("stock program lost %s: %v", addition.Name, err)
				}
			}
			if profile.ID == "z" {
				if _, err := recipe.ExportZProgram(edited, profile); err == nil || !strings.Contains(err.Error(), addition.Name) {
					t.Fatalf("Z program lost %s: %v", addition.Name, err)
				}
			}
		}
		r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Centennial", Kind: "hop", Use: "dry-hop", Amount: 12, Unit: "g", Minutes: 4320})
		plan, err := recipe.Translate(r, profile, 0)
		if err != nil || plan.MachineRecipe == nil {
			t.Fatalf("%s rejected a separate hold and dry hops: %v, %v", profile.ID, err, plan.DeploymentBlockers)
		}
	}
}
