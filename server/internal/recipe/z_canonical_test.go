package recipe_test

import (
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
	"strings"
	"testing"
)

func TestZCanonicalRoutesPreservedButArchivedNativeGrammarBlocked(t *testing.T) {
	var profile domain.Profile
	for _, p := range recipe.Profiles() {
		if p.ID == "z" {
			profile = p
		}
	}
	r := source()
	r.Steps = []domain.RecipeStep{}
	for valve := 1; valve <= 6; valve++ {
		r.Steps = append(r.Steps, domain.RecipeStep{Name: "Direct valve", Kind: "mash", Location: valve, TargetC: 65, Minutes: 1})
	}
	plan, err := recipe.Adapt(r, profile, 5)
	if err != nil || !plan.CanDeploy || plan.MachineRecipe == nil {
		t.Fatalf("canonical Z unavailable %+v %v", plan, err)
	}
	for index, step := range plan.MachineRecipe.Steps {
		if step.Location != index+1 {
			t.Fatal("direct valve remapped")
		}
	}
	for _, model := range []string{"z", "zymatic"} {
		native := domain.Recipe{ID: "archive", Name: "Source", Kind: "native-program", SourceModel: model, NativeProgram: &domain.MachineRecipe{ID: "archive", Name: "Source", Steps: []domain.MachineStep{{Name: "Legacy pause", Location: 6, Minutes: 1}}}}
		plan, err := recipe.Adapt(native, profile, 0)
		if err != nil || plan.CanDeploy || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "pause") {
			t.Fatalf("native grammar was promoted %+v %v", plan, err)
		}
	}
	r.Steps[0].Location = 0
	if plan, err := recipe.Adapt(r, profile, 5); err != nil || plan.CanDeploy {
		t.Fatalf("unmapped valve accepted %+v %v", plan, err)
	}
}
