package recipe

import (
	"encoding/json"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"reflect"
	"strings"
	"testing"
)

func TestProgramMakerExchangeAndTargetLimits(t *testing.T) {
	const data = `{"Name":"Test","IsMetric":true,"IsBeer":false,"UsePicoPakMode":true,"Steps":[{"Name":"Adjunct 4","StepType":1,"Location":6,"Temp":64.21,"Time":5401,"Drain":37},{"Name":"Operator","StepType":2},{"Name":"Unheated","StepType":1,"Location":1,"Temp":null,"Time":0,"Drain":0}]}`
	items, err := Import([]byte(data), "json")
	if err != nil {
		t.Fatal(err)
	}
	for _, p := range Profiles() {
		plan, err := Translate(items[0], p, 0)
		if err != nil || plan.CanDeploy != (p.ID == "z") {
			t.Fatal(p.ID, err, plan.DeploymentBlockers)
		}
	}
	out, err := Export(items[0], "z-program")
	if err != nil {
		t.Fatal(err)
	}
	var want, got any
	if json.Unmarshal([]byte(data), &want) != nil || json.Unmarshal(out, &got) != nil || !reflect.DeepEqual(want, got) {
		t.Fatal(string(out))
	}
	for _, bad := range []string{strings.Replace(data, `"StepType":2`, `"StepType":2,"Location":1`, 1), strings.Replace(data, `"Location":6`, `"Location":0`, 1), strings.Replace(data, `"Time":5401`, `"Time":-1`, 1), strings.Replace(data, `"Drain":37`, `"Drain":7201`, 1), strings.Replace(data, `"IsBeer":false,`, "", 1), strings.Replace(data, `"Temp":64.21`, `"Temp":100`, 1)} {
		if _, err := Import([]byte(bad), "json"); err == nil {
			t.Fatal("malformed program accepted", bad)
		}
	}
	r := items[0]
	s := r.NativeProgram.ZProgram.Steps[0]
	r.NativeProgram.ZProgram.Steps = nil
	for range 9 {
		r.NativeProgram.ZProgram.Steps = append(r.NativeProgram.ZProgram.Steps, s)
	}
	if err := Validate(r); err != nil {
		t.Fatal(err)
	}
	if _, err := Export(r, "z-program"); err == nil {
		t.Fatal("stock decoder cannot fit nine fully populated active steps")
	}
}

func TestExportIngredientProgramUsesCompiledZRoutes(t *testing.T) {
	r := domain.Recipe{Name: "Test", BatchLiters: 5, Efficiency: 55, Ingredients: []domain.Ingredient{{Name: "Malt", Kind: "fermentable", Use: "mash", Unit: "kg", Amount: 1}}, Steps: []domain.RecipeStep{{Name: "Mash", Kind: "mash", TargetC: 65, Minutes: 60}, {Name: "Boil", Kind: "boil", TargetC: 100, Minutes: 60}}}
	b, err := Export(r, "z-program")
	if err != nil {
		t.Fatal(err)
	}
	var p domain.ZProgram
	if err := json.Unmarshal(b, &p); err != nil {
		t.Fatal(err)
	}
	for _, s := range p.Steps {
		if s.Name == "Mash" && *s.Location == 2 && *s.Time == 3600 {
			return
		}
	}
	t.Fatal("lost the mash route or timing", p)
}
