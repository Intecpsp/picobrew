package recipe_test

import (
	"math"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
)

func TestKnownStartingWaterLimitsTheWholeBill(t *testing.T) {
	const gallon = 3.785411784
	workingFill := map[string]float64{
		"pico-s": 1.375 * gallon, "pico-pro": 1.375 * gallon, "pico-c": 5.2,
		"z": 4.27 * gallon, "zymatic": 4.27 * gallon,
	}
	for _, p := range recipe.Profiles() {
		r := source()
		r.SourceModel = p.ID
		r.BatchLiters = 5
		r.Steps[1].TargetC = 94
		r.Ingredients[0].Amount = 0.3
		r.Ingredients[1].Amount = 1
		r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Source starting water", Kind: "water", Unit: "L", Use: "mash", Amount: 20})
		limit := workingFill[p.ID]
		auto, err := recipe.Translate(r, p, 0)
		if err != nil || auto.MachineRecipe == nil {
			t.Fatalf("%s: %v %v", p.ID, err, auto.DeploymentBlockers)
		}
		for i, in := range auto.Recipe.Ingredients {
			if math.Abs(in.Amount-r.Ingredients[i].Amount*limit/20) > 1e-9 {
				t.Fatalf("%s changed the ratio of %s: %.8f", p.ID, in.Name, in.Amount)
			}
		}
		if math.Abs(auto.Recipe.BatchLiters-r.BatchLiters*limit/20) > 1e-9 || !strings.Contains(strings.Join(auto.Warnings, " "), "starting water") {
			t.Fatalf("%s missing the fill-limited yield or explanation: %v", p.ID, auto)
		}
		// A plan returned by Translate uses the scaled water label. Validate the
		// edited plan again at the actual addition, including the stock encoder.
		larger := auto.Recipe
		larger.Ingredients = append([]domain.Ingredient(nil), auto.Recipe.Ingredients...)
		larger.Ingredients[2].Amount += 0.1
		if err := recipe.ValidateCapacity(larger, p); err == nil || !strings.Contains(err.Error(), "working fill") {
			t.Fatalf("%s accepted an overfilled edited plan: %v", p.ID, err)
		}
		if p.ID == "zymatic" {
			if _, err := recipe.StockZymaticProgram(larger, p); err == nil || !strings.Contains(err.Error(), "working fill") {
				t.Fatalf("stock sync accepted an overfilled recipe: %v", err)
			}
		}
		requested := (limit + 0.1) / 20 * r.BatchLiters
		plan, err := recipe.Translate(r, p, requested)
		if err != nil || plan.MachineRecipe != nil || !strings.Contains(strings.Join(plan.DeploymentBlockers, " "), "working fill") {
			t.Fatalf("%s accepted explicit overfill: %v %v", p.ID, err, plan.DeploymentBlockers)
		}
		if _, err := recipe.Adapt(r, p, requested); err == nil || !strings.Contains(err.Error(), "working fill") {
			t.Fatalf("%s adapted explicit overfill: %v", p.ID, err)
		}
		p.UserConfigured = true
		p.BatchLiters = requested / 2
		smaller, err := recipe.Translate(r, p, 0)
		if err != nil || smaller.MachineRecipe == nil || math.Abs(smaller.Recipe.BatchLiters-p.BatchLiters) > 1e-9 {
			t.Fatalf("%s lost the owner’s lower yield calibration: %v %v", p.ID, err, smaller.DeploymentBlockers)
		}
	}
}

func TestWaterInventoryIsNotAWorkingKegFill(t *testing.T) {
	p := recipe.Profiles()[0]
	r := source()
	r.BatchLiters = 5
	r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Water profile", Kind: "water", Unit: "L", Use: "mash", Amount: 30})
	if err := recipe.ValidateCapacity(r, p); err != nil {
		t.Fatalf("an unassigned water inventory was treated as the brew keg: %v", err)
	}
}

func TestEstimateAndLegacyWaterLimits(t *testing.T) {
	p := recipe.Profiles()[0]
	water := func(name, use string, liters float64) domain.Ingredient {
		return domain.Ingredient{Name: name, Unit: "L", Kind: "water", Use: use, Amount: liters}
	}
	for _, tc := range []struct {
		name   string
		rows   []domain.Ingredient
		reject bool
	}{
		{"estimated named source", []domain.Ingredient{water("Tap water", "starting keg", 6)}, true},
		{"pre-marker Hub estimate", []domain.Ingredient{water("Brewing water", "mash", 6)}, true},
		{"distinct water inventory", []domain.Ingredient{water("Brewing water", "mash", 6), water("Sparge", "sparge", 1)}, false},
		{"combined marked fill", []domain.Ingredient{water("RO", "starting keg", 3), water("Tap", "starting keg", 3)}, true},
		{"separate sparge", []domain.Ingredient{water("RO", "starting keg", 4), water("Tap", "sparge", 3)}, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := source()
			r.BatchLiters = 5
			r.Ingredients = append(r.Ingredients, tc.rows...)
			err := recipe.ValidateCapacity(r, p)
			if (err != nil) != tc.reject || err != nil && !strings.Contains(err.Error(), "working fill") {
				t.Fatal(err)
			}
		})
	}
}

func TestBeerXMLKeepsNamedStartingKegWater(t *testing.T) {
	r := exchangeRecipe()
	original := &r.Ingredients[len(r.Ingredients)-1]
	original.Name, original.Use = "RO well water", "starting keg"
	data, err := recipe.Export(r, "beerxml")
	if err != nil {
		t.Fatal(err)
	}
	imported, err := recipe.Import(data, "xml")
	if err != nil {
		t.Fatal(err)
	}
	got := imported[0].Ingredients[len(imported[0].Ingredients)-1]
	if got != *original {
		t.Fatalf("changed working water: got %+v, want %+v", got, *original)
	}
}
