package recipe_test

import (
	"encoding/json"
	"math"
	"reflect"
	"slices"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
)

func source() domain.Recipe {
	return domain.Recipe{ID: "house-ale", Name: "House Ale", BatchLiters: 10, Efficiency: 75, Revision: 4,
		Ingredients: []domain.Ingredient{{Name: "Pale malt", Kind: "fermentable", Amount: 2, Unit: "kg", Use: "mash"}, {Name: "Cascade", Kind: "hop", Amount: 20, Unit: "g", Use: "boil", Minutes: 30, Alpha: 5.5}},
		Steps:       []domain.RecipeStep{{Name: "Mash", Kind: "mash", TargetC: 65, Minutes: 60, DrainMinutes: 2, Location: 2}, {Name: "Boil", Kind: "boil", TargetC: 100, Minutes: 30, Location: 3}}}
}

func TestAdaptScalesWithExplicitMappingsWithoutMutatingSource(t *testing.T) {
	original := source()
	before, _ := json.Marshal(original)
	p := recipe.Profiles()[0]
	p.Efficiency = 60
	plan, err := recipe.Adapt(original, p, 5)
	if err != nil {
		t.Fatal(err)
	}
	if !plan.CanDeploy || plan.MachineRecipe == nil || plan.SourceRevision != 4 {
		t.Fatalf("incomplete plan: %+v", plan)
	}
	if plan.Recipe.Ingredients[0].Amount != 1 || plan.Recipe.Ingredients[1].Amount != 10 {
		t.Fatalf("wrong scaling: %+v", plan.Recipe.Ingredients)
	}
	if plan.Recipe.Steps[0].TargetC != 65 || plan.MachineRecipe.Steps[0].TargetF != 149 || plan.MachineRecipe.Steps[1].Location != 3 {
		t.Fatalf("units/routing altered: %+v", plan)
	}
	if !strings.Contains(strings.Join(plan.Warnings, " "), "utilization") {
		t.Fatal("hop limitation omitted")
	}
	plan.Recipe.Ingredients[0].Name = "mutated"
	plan.Recipe.Steps[0].Name = "mutated"
	after, _ := json.Marshal(original)
	if string(before) != string(after) {
		t.Fatal("adaptation shares mutable source slices")
	}
}

func TestUnsupportedModelsCannotBePromotedToControl(t *testing.T) {
	for _, p := range recipe.Profiles()[3:] {
		if p.ID == "z" {
			continue
		} // Canonical direct-valve Z programs have an adapter.
		p.ControlSupported = true // a caller cannot manufacture an adapter this way.
		plan, err := recipe.Adapt(source(), p, 5)
		if err != nil {
			t.Fatalf("%s: %v", p.ID, err)
		}
		if plan.CanDeploy || plan.MachineRecipe != nil || plan.Profile.ControlSupported {
			t.Fatalf("%s became deployable", p.ID)
		}
	}
	p := recipe.Profiles()[0]
	p.ID = "unknown"
	if _, err := recipe.Adapt(source(), p, 5); err == nil {
		t.Fatal("unknown profile accepted")
	}
}

func TestNativeLimitsCannotBeRelaxedByProfile(t *testing.T) {
	profile := recipe.Profiles()[0]
	profile.MaxSteps = 100
	cases := []struct {
		name   string
		change func(*domain.Recipe)
	}{
		{"unmapped", func(r *domain.Recipe) { r.Steps[0].Location = 0 }},
		{"unknown step", func(r *domain.Recipe) { r.Steps[0].Kind = "magic-valve" }},
		{"unsupported cooling", func(r *domain.Recipe) { r.Steps[0].Kind = "cool" }},
		{"too cold", func(r *domain.Recipe) { r.Steps[0].TargetC = 10 }},
		{"too long", func(r *domain.Recipe) { r.Steps[0].Minutes = 600 }},
		{"drain too long", func(r *domain.Recipe) { r.Steps[0].DrainMinutes = 121 }},
		{"too many steps", func(r *domain.Recipe) {
			for len(r.Steps) < 16 {
				r.Steps = append(r.Steps, r.Steps[0])
			}
		}},
	}
	for _, test := range cases {
		t.Run(test.name, func(t *testing.T) {
			r := source()
			test.change(&r)
			plan, err := recipe.Adapt(r, profile, 5)
			if test.name == "unknown step" {
				if err == nil {
					t.Fatal("invalid canonical recipe accepted")
				}
			} else if err != nil || plan.CanDeploy || plan.MachineRecipe != nil || len(plan.DeploymentBlockers) == 0 {
				t.Fatalf("invalid native program did not remain a blocked ingredient plan: %+v %v", plan, err)
			}
		})
	}
	if _, err := recipe.Adapt(source(), profile, 6); err == nil {
		t.Fatal("profile batch limit ignored")
	}
	profile.MaxGrainKG = 0.1
	if _, err := recipe.Adapt(source(), profile, 5); err == nil {
		t.Fatal("grain limit ignored")
	}
}

func TestUnmappedLargeRecipePlansAtProfileVolumeWithoutInventingRouting(t *testing.T) {
	r := source()
	r.BatchLiters = 14.0060236008
	r.BoilLiters = 16
	r.Steps[0].Location = 0
	r.Steps[1].Location = 0
	r.Steps[0].InfuseLiters = 6
	before, _ := json.Marshal(r)
	profile := recipe.Profiles()[0]
	plan, err := recipe.Adapt(r, profile, 0)
	if err != nil || plan.Recipe.BatchLiters != 5 || plan.CanDeploy || plan.MachineRecipe != nil {
		t.Fatalf("large imported recipe did not produce a usable 5 L plan: %+v %v", plan, err)
	}
	ratio := 5 / r.BatchLiters
	if math.Abs(plan.Recipe.Ingredients[0].Amount-2*ratio) > 1e-12 || math.Abs(plan.Recipe.Ingredients[1].Amount-20*ratio) > 1e-12 || plan.Recipe.BoilLiters != 16*ratio || plan.Recipe.Steps[0].InfuseLiters != 6*ratio {
		t.Fatalf("ingredient or infusion scaling changed: %+v", plan.Recipe)
	}
	for i, step := range plan.Recipe.Steps {
		if step.Location != 0 || step.Minutes != r.Steps[i].Minutes || step.TargetC != r.Steps[i].TargetC {
			t.Fatal("planning invented routing or changed schedule")
		}
	}
	blockers := strings.Join(plan.DeploymentBlockers, " ")
	if !strings.Contains(blockers, "Choose a flow path for step 1") || !strings.Contains(blockers, "Choose a flow path for step 2") || !strings.Contains(blockers, "infusion") {
		t.Fatalf("deployment blockers omitted: %s", blockers)
	}
	after, _ := json.Marshal(r)
	if string(before) != string(after) {
		t.Fatal("planning modified the imported recipe")
	}
	profile.BatchLiters, profile.Efficiency = 4.8, 55
	calibrated, err := recipe.Adapt(r, profile, 0)
	if err != nil || calibrated.Recipe.BatchLiters != 4.8 || calibrated.Recipe.Efficiency != 55 {
		t.Fatalf("calibrated profile default not applied: %+v %v", calibrated, err)
	}
}

func TestPlanningStillRejectsInvalidVolumesAndConfiguredCapacities(t *testing.T) {
	r := source()
	r.Steps[0].Location = 0
	for _, batch := range []float64{-1, 5.01, math.NaN(), math.Inf(1)} {
		if _, err := recipe.Adapt(r, recipe.Profiles()[0], batch); err == nil {
			t.Fatalf("invalid target batch accepted: %v", batch)
		}
	}
	for _, configure := range []func(*domain.Profile){
		func(p *domain.Profile) { p.MaxGrainKG = .1 },
		func(p *domain.Profile) { p.MaxSteps = 1 },
		func(p *domain.Profile) { p.MaxTargetC = 64 },
		func(p *domain.Profile) { p.Efficiency = 0 },
	} {
		profile := recipe.Profiles()[0]
		configure(&profile)
		if _, err := recipe.Adapt(r, profile, 5); err == nil {
			t.Fatalf("planning ignored configured limits: %+v", profile)
		}
	}
}

func TestDeviceLabelChangesAreExplicitAndStable(t *testing.T) {
	r := source()
	r.ID = "a-canonical-identifier-longer-than-twenty"
	r.Name = "Très long recipe name which exceeds the display limit"
	r.Steps[0].Name = "Ä very long mash step name exceeds the display"
	first, err := recipe.Adapt(r, recipe.Profiles()[0], 5)
	if err != nil {
		t.Fatal(err)
	}
	second, err := recipe.Adapt(r, recipe.Profiles()[0], 5)
	if err != nil {
		t.Fatal(err)
	}
	if first.MachineRecipe.ID != second.MachineRecipe.ID || len(first.MachineRecipe.ID) != 20 {
		t.Fatal("native id is unstable")
	}
	if first.Recipe.Name != r.Name || len(first.MachineRecipe.Name) > 31 || len(first.MachineRecipe.Steps[0].Name) > 21 {
		t.Fatal("label limits/source preservation")
	}
	if !strings.Contains(strings.Join(first.Warnings, " "), "canonical label is unchanged") {
		t.Fatal("transformation was silent")
	}
	bytes, err := recipe.NativeJSON(*first.MachineRecipe)
	if err != nil || len(bytes) > 4095 {
		t.Fatalf("wire payload: %d %v", len(bytes), err)
	}
}

func TestCanonicalValidationAndNativeBoundaryValues(t *testing.T) {
	r := source()
	r.Steps[0].Location = 0
	if err := recipe.Validate(r); err != nil {
		t.Fatalf("unmapped canonical plan should remain editable: %v", err)
	}
	r.Ingredients[0].Unit = "lb"
	if err := recipe.Validate(r); err == nil {
		t.Fatal("noncanonical mass silently accepted")
	}
	r = source()
	r.BatchLiters = math.NaN()
	if err := recipe.Validate(r); err == nil {
		t.Fatal("NaN accepted")
	}
	machine := domain.MachineRecipe{ID: "a", Name: strings.Repeat("<", 31), Steps: []domain.MachineStep{{Name: strings.Repeat("&", 21), Location: 6, TargetF: 60, Minutes: 599, DrainMinutes: 120}}}
	for len(machine.Steps) < 15 {
		machine.Steps = append(machine.Steps, machine.Steps[0])
	}
	data, err := recipe.NativeJSON(machine)
	if err != nil {
		t.Fatal(err)
	}
	var roundtrip domain.MachineRecipe
	if err := json.Unmarshal(data, &roundtrip); err != nil || !reflect.DeepEqual(machine, roundtrip) {
		t.Fatal("ASCII JSON escapes changed payload")
	}
	machine.Steps[0].TargetF = 59
	if _, err := recipe.NativeJSON(machine); err == nil {
		t.Fatal("native gap 1–59F accepted")
	}
}

func TestCStandaloneProfileProducesExplicitReviewedNativePlan(t *testing.T) {
	var profile domain.Profile
	for _, candidate := range recipe.Profiles() {
		if candidate.ID == "pico-c" {
			profile = candidate
		}
	}
	if !profile.ControlSupported || profile.MaxSteps != 15 {
		t.Fatalf("C source target missing native limits: %+v", profile)
	}
	plan, err := recipe.Adapt(source(), profile, 5)
	if err != nil || !plan.CanDeploy || plan.MachineRecipe == nil {
		t.Fatalf("C plan %+v %v", plan, err)
	}
}

func TestFirstWortAndBoilAtSameTimeShareSlotCapacity(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		r.BatchLiters = p.BatchLiters
		r.Ingredients = []domain.Ingredient{{Kind: "hop", Use: "boil", Amount: 25, Minutes: 60}, {Kind: "hop", Use: "first-wort", Amount: 20, Minutes: 60}}
		if err := recipe.ValidateCapacity(r, p); err == nil || !strings.Contains(err.Error(), "combined slot load") {
			t.Fatalf("%s: %v", p.ID, err)
		}
	}
}

func TestAutomaticLoadPreservesRecipeProportions(t *testing.T) {
	for _, profile := range recipe.Profiles() {
		r := source()
		r.SourceModel = profile.ID
		r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Dry hops", Kind: "hop", Use: "dry-hop", Amount: 50, Unit: "g"}, domain.Ingredient{Name: "Boil addition", Kind: "hop", Use: "boil", Minutes: 30, Amount: 10, Unit: "g"})
		for _, limit := range []string{"grain", "hop slot", "saved volume"} {
			p := profile
			want := p.MaxGrainKG / 2
			switch limit {
			case "grain":
				p.MaxHopSlotG = 100
			case "hop slot":
				p.MaxHopSlotG = 17
				want = 17.0 / 30
			case "saved volume":
				p.UserConfigured = true
				p.BatchLiters = 3.8
				want = 0.38
			}
			p.Efficiency = 32
			plan, err := recipe.Translate(r, p, 0)
			if err != nil {
				t.Fatal(err)
			}
			for i, in := range r.Ingredients {
				if math.Abs(plan.Recipe.Ingredients[i].Amount/in.Amount-want) > 1e-10 {
					t.Fatalf("%s/%s wrong load for %s", p.ID, limit, in.Name)
				}
			}
			if math.Abs(plan.Recipe.BatchLiters-r.BatchLiters*want) > 1e-9 {
				t.Fatal("incorrect estimated yield")
			}
		}
	}
}

func TestRecoveredAdjunctAndFirstWortLoads(t *testing.T) {
	r := source()
	for i := range r.Steps {
		r.Steps[i].Location = 0
	}
	r.Ingredients[1].Use = "first-wort"
	r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Coriander", Kind: "misc", Use: "boil", SlotAddition: true, Unit: "g", Amount: 28, Minutes: 10}, domain.Ingredient{Name: "Late hops", Kind: "hop", Use: "boil", Unit: "g", Amount: 14, Minutes: 10})
	for _, p := range recipe.Profiles() {
		p.MaxHopSlotG = 21
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe == nil || plan.Recipe.BatchLiters != 5 || plan.HopSlotsByMinute[10] != 2 {
			t.Fatalf("%s %v %+v", p.ID, err, plan)
		}
		found := false
		for _, step := range plan.Recipe.Steps {
			if step.Kind == "heat" && step.Location == 3 && step.Minutes == 0 {
				found = true
			}
		}
		if !found {
			t.Fatal("first-wort slot missed heat-up", p.ID)
		}
		edited := plan.Recipe
		for i, step := range edited.Steps {
			if step.Kind == "heat" && step.Location == 3 {
				edited.Steps[i].Location = 1
			}
		}
		if changed, err := recipe.Translate(edited, p, 0); err != nil || changed.MachineRecipe != nil {
			t.Fatal("edited schedule lost first-wort exposure", p.ID, err)
		}
	}
	r.Ingredients[2].Unit = "mL"
	if recipe.Validate(r) == nil {
		t.Fatal("volume was treated as a slot weight")
	}
}

func TestMashBasketCapacityUsesIngredientPlacement(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		r.BatchLiters = p.BatchLiters
		r.Ingredients = []domain.Ingredient{
			{Name: "Pale malt", Kind: "fermentable", Type: "Grain", Use: "mash", Amount: p.MaxGrainKG - 0.25, Unit: "kg"},
			{Name: "Lactose", Kind: "fermentable", Type: "Sugar", Use: "mash", Amount: 0.25, Unit: "kg"},
			{Name: "Mash hops", Kind: "hop", Use: "mash", Amount: 10, Unit: "g"},
		}
		if err := recipe.ValidateCapacity(r, p); err == nil {
			t.Fatal(p.ID, "mash sugar and mash hops exceeded basket capacity")
		}
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || math.Abs(plan.Recipe.Ingredients[1].Amount/0.25-p.MaxGrainKG/(p.MaxGrainKG+0.01)) > 1e-9 {
			t.Fatal(p.ID, err, "automatic scaling omitted basket ingredients")
		}
		r.Ingredients[1].Use = "boil"
		if err := recipe.ValidateCapacity(r, p); err != nil {
			t.Fatal(p.ID, "a keg addition used basket capacity", err)
		}
	}
}

func TestHopWettingAcrossBrewers(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		r.SourceModel, r.BatchLiters = p.ID, p.BatchLiters
		r.Ingredients[1].Minutes = 60
		r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Finishing hops", Kind: "hop", Use: "boil", Amount: 10, Unit: "g", Minutes: 5})
		r.Steps = []domain.RecipeStep{
			{Name: "Mash", Kind: "mash", Location: 2, TargetC: 65, Minutes: 60},
			{Name: "Hop slot 4", Kind: "hop", Location: 6, TargetC: 95, Minutes: 55},
			{Name: "Hop slot 1", Kind: "hop", Location: 3, TargetC: 95, Minutes: 5},
		}
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "wets Finishing hops") {
			t.Fatal(p.ID, "allowed early cascading exposure", err, plan.DeploymentBlockers)
		}
		if p.ID == "zymatic" {
			if _, err := recipe.StockZymaticProgram(r, p); err == nil {
				t.Fatal("stock program encoder accepted early exposure")
			}
		}
		r.Steps[1].Location, r.Steps[2].Location = 3, 6
		plan, err = recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe == nil || plan.HopSlotsByMinute[60] != 1 || plan.HopSlotsByMinute[5] != 4 || len(plan.Recipe.Steps) != 4 || plan.Recipe.Steps[1].Kind != "heat" || plan.Recipe.Steps[1].Location != 1 {
			t.Fatal(p.ID, "missing regular boil preheat or wrong slot", err, plan)
		}
		// The same heat routed into the ingredient is appropriate only for first wort.
		r.Steps = append([]domain.RecipeStep(nil), plan.Recipe.Steps...)
		r.Steps[1].Location = 3
		plan, err = recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe != nil || len(plan.DeploymentBlockers) == 0 {
			t.Fatal(p.ID, "regular hops wetted during heat-up", err)
		}
		r.Ingredients[1].Use = "first-wort"
		plan, err = recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe == nil || len(plan.Recipe.Steps) != 4 {
			t.Fatal(p.ID, "correct first-wort heat-up rejected", err, plan.DeploymentBlockers)
		}
	}
}

func TestMappedIngredientsReachBasketAndRespectBoilDuration(t *testing.T) {
	for _, p := range recipe.Profiles() {
		for _, check := range []struct {
			name    string
			edit    func(*domain.Recipe)
			message string
		}{
			{"mash routes through keg", func(r *domain.Recipe) { r.Steps[0].Location = 1 }, "grain basket"},
			{"mash after boil", func(r *domain.Recipe) { r.Steps[0], r.Steps[1] = r.Steps[1], r.Steps[0] }, "grain basket"},
			{"mash omitted", func(r *domain.Recipe) { r.Steps = r.Steps[1:] }, "grain basket"},
			{"boil durations disagree", func(r *domain.Recipe) { r.BoilMinutes = 60 }, "boil duration"},
		} {
			t.Run(p.ID+"/"+check.name, func(t *testing.T) {
				r := source()
				r.BatchLiters = p.BatchLiters
				check.edit(&r)
				plan, err := recipe.Translate(r, p, 0)
				if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), check.message) {
					t.Fatalf("unexpected plan %v, error %v", plan, err)
				}
				if p.ID == "zymatic" {
					if _, err := recipe.StockZymaticProgram(r, p); err == nil {
						t.Fatal("invalid routed program reached the stock encoder")
					}
				}
			})
		}
	}
}

func TestCrossModelBoilPreheatUsesBrewerTemperature(t *testing.T) {
	r := source()
	r.SourceModel = "zymatic"
	r.Steps[1].TargetC = (207.0 - 32) / 1.8
	r.Steps = append(r.Steps[:1:1], domain.RecipeStep{Name: "Heat to boil", Kind: "heat", Location: 1, TargetC: r.Steps[1].TargetC}, r.Steps[1])
	for _, p := range recipe.Profiles() {
		if p.ID == "zymatic" {
			continue
		}
		plan, err := recipe.Translate(r, p, 0)
		want := 203
		if p.ID == "z" {
			want = 202
		}
		if err != nil || plan.MachineRecipe == nil || len(plan.MachineRecipe.Steps) != 3 || plan.MachineRecipe.Steps[1].TargetF != want {
			t.Fatal(p.ID, err, plan)
		}
	}
}

func TestMashOperationsKeepTheirMeaningAcrossBrewers(t *testing.T) {
	for _, p := range recipe.Profiles() {
		for _, operation := range []string{"decoction", " DECOCTION ", "infusion", " Infusion "} {
			r := source()
			r.Steps[0].Location, r.Steps[1].Location = 0, 0
			r.Steps[0].Type = operation
			plan, err := recipe.Translate(r, p, 0)
			if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "mash operation") {
				t.Fatalf("%s converted %q to a temperature hold: %+v %v", p.ID, operation, plan, err)
			}
		}
		for _, loc := range []int{0, 2} {
			r := source()
			r.Steps[0].Type = " temperature "
			if loc == 0 {
				r.Steps[0].Location, r.Steps[1].Location = 0, 0
			}
			plan, err := recipe.Translate(r, p, 0)
			if err != nil || plan.MachineRecipe == nil {
				t.Fatalf("%s rejected temperature hold at location %d: %v %v", p.ID, loc, plan.DeploymentBlockers, err)
			}
		}
	}
}

func TestEveryRoutedMashStageReachesTheLoadedBasket(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		mashOut := domain.RecipeStep{Name: "Mash out", Kind: "mash-out", TargetC: 76, Minutes: 7, Location: 1}
		r.Steps = []domain.RecipeStep{r.Steps[0], mashOut, r.Steps[1]}
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "grain basket") {
			t.Fatalf("%s accepted a mash-out through the keg: %v %v", p.ID, plan, err)
		}
	}
}

func TestUnmappedKettleExtractProgram(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		r.Type = "Extract"
		r.Ingredients[0].Name, r.Ingredients[0].Type, r.Ingredients[0].Use = "Liquid malt extract", "Extract", "boil"
		r.Steps = []domain.RecipeStep{{Name: "Boil", Kind: "boil", TargetC: 100, Minutes: 60}}
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe == nil || plan.HopSlotsByMinute[30] != 1 || len(plan.MachineRecipe.Steps) != 3 {
			t.Fatalf("%s did not compile extract heat-up, boil and timed hops: %+v %v", p.ID, plan, err)
		}
		for _, s := range plan.Recipe.Steps {
			if s.Location == 2 {
				t.Fatalf("%s added a grain basket step to a kettle-extract recipe", p.ID)
			}
		}
		r.Ingredients[0].Type = "Grain"
		plan, err = recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe != nil {
			t.Fatalf("%s accepted a boil-only conversion with grain: %+v %v", p.ID, plan, err)
		}
	}
}

func TestTimedBoilCannotHideAnInterveningHold(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		r.Steps[1].Minutes = 20
		pause := domain.RecipeStep{Name: "Extra hold", Kind: "hold", TargetC: 60, Minutes: 30, Location: 1}
		finish := domain.RecipeStep{Name: "Finishing hops", Kind: "hop", TargetC: 95, Minutes: 10, Location: 4}
		r.Steps = append(r.Steps, pause, finish)
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "interrupts the timed boil") {
			t.Fatalf("%s omitted a 30-minute hold from hop exposure: %+v %v", p.ID, plan, err)
		}
		r.Steps[2], r.Steps[3] = finish, pause
		plan, err = recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe == nil {
			t.Fatalf("%s rejected a separate post-boil hold: %v %v", p.ID, plan.DeploymentBlockers, err)
		}
	}
}

func TestFinalBoilDrainCannotRunBetweenTimedHopSteps(t *testing.T) {
	for _, p := range recipe.Profiles() {
		r := source()
		r.Steps[1].Minutes = 20
		r.Steps[1].DrainMinutes = 5
		r.Steps = append(r.Steps, domain.RecipeStep{Name: "Finishing hops", Kind: "hop", TargetC: 95, Minutes: 10, Location: 4})
		plan, err := recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "interrupts hop circulation") {
			t.Fatalf("%s omitted an intermediate drain from the hop schedule: %+v %v", p.ID, plan, err)
		}
		r.Steps[1].DrainMinutes, r.Steps[2].DrainMinutes = 0, 5
		plan, err = recipe.Translate(r, p, 0)
		if err != nil || plan.MachineRecipe == nil {
			t.Fatalf("%s rejected a final drain: %+v %v", p.ID, plan.DeploymentBlockers, err)
		}
	}
}

func TestMashHopsUseTheHeatedGrainBasket(t *testing.T) {
	for _, profile := range recipe.Profiles() {
		t.Run(profile.ID, func(t *testing.T) {
			r := source()
			r.Steps[0].Location, r.Steps[1].Location = 0, 0
			r.Ingredients[1].Use, r.Ingredients[1].Minutes = "mash", 0
			r.Ingredients[1].Amount = profile.MaxHopSlotG * 2
			plan, err := recipe.Translate(r, profile, 0)
			if err != nil || plan.MachineRecipe == nil || len(plan.HopSlotsByMinute) != 0 {
				t.Fatal("mash hops occupied a boil slot or blocked the brew", err, plan.DeploymentBlockers)
			}
			var found bool
			for i, step := range plan.Recipe.Steps {
				if step.Kind == "mash" && step.Location == 2 && step.TargetC > 0 && step.Minutes > 0 {
					found = true
					plan.Recipe.Steps[i].Location = 1
				}
			}
			if !found {
				t.Fatal("no timed mash through the grain basket")
			}
			// The hops alone still require the mash basket even without a grain ingredient.
			plan.Recipe.Ingredients = plan.Recipe.Ingredients[1:]
			invalid, err := recipe.Translate(plan.Recipe, profile, 0)
			if err != nil || invalid.MachineRecipe != nil || !strings.Contains(strings.Join(invalid.DeploymentBlockers, " "), "grain basket") {
				t.Fatal("no mash-basket flow for the hops", err, invalid.DeploymentBlockers)
			}
		})
	}
}

func TestUnresolvedSourceReviewBlocksEveryProgramEncoding(t *testing.T) {
	const reason = "Source program review: the author and native program disagree about when the second hop charge is loaded."
	for _, profile := range recipe.Profiles() {
		t.Run(profile.ID, func(t *testing.T) {
			r := source()
			r.Steps[1].TargetC = 95
			r.Provenance = &domain.RecipeProvenance{Warnings: []string{reason}}
			r.SourceModel = profile.ID
			for _, routed := range []bool{true, false} {
				if !routed {
					r.Steps[0].Location, r.Steps[1].Location = 0, 0
				}
				for _, compile := range []func(domain.Recipe, domain.Profile, float64) (domain.Adapted, error){recipe.Adapt, recipe.Translate} {
					plan, err := compile(r, profile, 5)
					if err != nil || plan.CanDeploy || plan.MachineRecipe != nil || !slices.Contains(plan.DeploymentBlockers, reason) || len(plan.Recipe.Ingredients) != 2 {
						t.Fatalf("routed=%v: lost review or ingredients: %v %+v", routed, err, plan)
					}
				}
			}
			native := archivedProgram()
			native.SourceModel = profile.ID
			native.Provenance.Warnings = []string{reason}
			plan, err := recipe.Translate(native, profile, 0)
			if err != nil || plan.MachineRecipe != nil || !slices.Contains(plan.DeploymentBlockers, reason) || plan.Recipe.NativeProgram == nil {
				t.Fatal("native source review was lost", err, plan.DeploymentBlockers)
			}
			if profile.ID == "zymatic" {
				for _, candidate := range []domain.Recipe{r, native} {
					program, err := recipe.StockZymaticProgram(candidate, profile)
					if err == nil || err.Error() != reason || len(program.Steps) != 0 {
						t.Fatal("blocked stock program was compiled", err)
					}
				}
			}
		})
	}
}
