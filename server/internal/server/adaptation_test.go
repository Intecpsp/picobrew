package server

import (
	"encoding/json"
	"math"
	"reflect"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestUnmappedRecipePreviewRetainsPlanButSingleAndBulkDeploymentCannotWrite(t *testing.T) {
	s, h, c := newTestServer(t)
	csrf := signIn(t, c, h.URL)
	adapter := newTransferAdapter()
	installTestDevice(t, s, "bulk", adapter)
	source := validRecipe()
	source.BatchLiters = 14.0060236008
	source.Steps[0].Type = "Decoction"
	for i := range source.Steps {
		source.Steps[i].Location = 0 // Ordinary external recipes carry no verified chamber routing.
	}
	status, body := request(t, c, "POST", h.URL+"/api/v1/recipes", csrf, source, nil)
	if status != 201 || json.Unmarshal(body, &source) != nil {
		t.Fatalf("source: %d %s", status, body)
	}
	status, body = request(t, c, "POST", h.URL+"/api/v1/recipes/"+source.ID+"/adapt", csrf, map[string]any{"profile_id": "pico-pro", "batch_liters": 0}, nil)
	var preview domain.Adapted
	if status != 200 || json.Unmarshal(body, &preview) != nil {
		t.Fatalf("planning preview: %d %s", status, body)
	}
	if math.Abs(preview.Recipe.BatchLiters-source.BatchLiters*2/1.2) > 1e-10 || preview.CanDeploy || preview.MachineRecipe != nil || len(preview.DeploymentBlockers) == 0 || preview.ProfileFingerprint == "" {
		t.Fatalf("incomplete or unexpectedly deployable planning preview: %+v", preview)
	}
	expected := source.Ingredients[0].Amount * (preview.Recipe.BatchLiters / source.BatchLiters)
	if math.Abs(preview.Recipe.Ingredients[0].Amount-expected) > 1e-12 || preview.Recipe.Steps[0].Location != 0 || preview.Recipe.Steps[0].Minutes != source.Steps[0].Minutes {
		t.Fatal("planning lost ingredients or invented schedule/routing")
	}
	status, body = request(t, c, "POST", h.URL+"/api/v1/devices/bulk/deploy", csrf, map[string]any{
		"recipe_id": source.ID, "profile_id": "pico-pro", "batch_liters": 5,
		"source_revision": preview.SourceRevision, "profile_fingerprint": preview.ProfileFingerprint,
	}, nil)
	if status != 422 || !strings.Contains(string(body), "Review the source mash operation") || adapter.writes != 0 {
		t.Fatalf("unmapped single deployment was not denied: %d writes=%d %s", status, adapter.writes, body)
	}
	selection := DeploymentInput{ProfileID: "pico-pro", Items: []DeploymentSelection{{RecipeID: source.ID, SourceRevision: source.Revision, BatchLiters: 5}}}
	plan := previewTransfer(t, c, h.URL, csrf, selection)
	if plan.PlannedCount != 0 || plan.Items[0].Status != "skipped" || plan.Items[0].Native != nil || !strings.Contains(plan.Items[0].Reason, "Review the source mash operation") {
		t.Fatalf("bulk preview included an unmapped program: %+v", plan)
	}
	status, _ = executeTransfer(t, c, h.URL, csrf, plan)
	if status != 200 || adapter.writes != 0 || adapter.commands != 0 {
		t.Fatalf("unmapped bulk plan wrote or started a machine: %d writes=%d commands=%d", status, adapter.writes, adapter.commands)
	}
	status, _ = request(t, c, "POST", h.URL+"/api/v1/recipes/"+source.ID+"/adapt", csrf, map[string]any{"profile_id": "pico-pro", "batch_liters": source.BatchLiters}, nil)
	if status != 422 {
		t.Fatal("planning bypassed the profile's batch capacity")
	}
}

func TestSingleDeploymentRejectsStaleRecipeOrCalibrationPreview(t *testing.T) {
	for _, change := range []string{"recipe", "calibration"} {
		t.Run(change, func(t *testing.T) {
			s, h, c := newTestServer(t)
			csrf := signIn(t, c, h.URL)
			adapter := newTransferAdapter()
			installTestDevice(t, s, "bulk", adapter)
			status, body := request(t, c, "POST", h.URL+"/api/v1/recipes", csrf, validRecipe(), nil)
			var source domain.Recipe
			if status != 201 || json.Unmarshal(body, &source) != nil {
				t.Fatalf("source: %d %s", status, body)
			}
			preview := func() domain.Adapted {
				t.Helper()
				status, body := request(t, c, "POST", h.URL+"/api/v1/recipes/"+source.ID+"/adapt", csrf, map[string]any{"profile_id": "pico-pro"}, nil)
				var plan domain.Adapted
				if status != 200 || json.Unmarshal(body, &plan) != nil || !plan.CanDeploy || plan.ProfileFingerprint == "" {
					t.Fatalf("preview: %d %s", status, body)
				}
				return plan
			}
			deploy := func(plan domain.Adapted) int {
				t.Helper()
				status, _ := request(t, c, "POST", h.URL+"/api/v1/devices/bulk/deploy", csrf, map[string]any{
					"recipe_id": source.ID, "profile_id": plan.Profile.ID, "batch_liters": 0,
					"source_revision": plan.SourceRevision, "profile_fingerprint": plan.ProfileFingerprint,
				}, nil)
				return status
			}
			old := preview()
			if change == "calibration" {
				status, body = request(t, c, "PUT", h.URL+"/api/v1/profiles/pico-pro", csrf, map[string]any{"batch_liters": 4.8, "efficiency": 60, "max_grain_kg": 0}, nil)
				if status != 200 {
					t.Fatalf("calibration: %d %s", status, body)
				}
			} else {
				source.Revision++
				if _, err := s.db.Put("recipe", source.ID, source, -1); err != nil {
					t.Fatal(err)
				}
			}
			if status := deploy(old); status != 409 || adapter.writes != 0 {
				t.Fatalf("stale %s preview deployed: status=%d writes=%d", change, status, adapter.writes)
			}
			current := preview()
			if change == "calibration" && (current.Recipe.BatchLiters != 4.8 || current.ProfileFingerprint == old.ProfileFingerprint) {
				t.Fatal("fresh preview did not use updated calibration")
			}
			if status := deploy(current); status != 200 || adapter.writes != 1 || adapter.commands != 0 {
				t.Fatalf("fresh reviewed plan did not deploy without starting: status=%d writes=%d", status, adapter.writes)
			}
		})
	}
}

func TestSaveTranslatesEveryBrewerAndRebuildsEditedVersions(t *testing.T) {
	s, h, c := newTestServer(t)
	token := signIn(t, c, h.URL)
	source := validRecipe()
	source.SourceModel = "pico-s"
	source.BatchLiters = 4
	source.Efficiency = 50
	source.Steps = []domain.RecipeStep{
		{Name: "Mash", Kind: "mash", Location: 2, Minutes: 45, DrainMinutes: 2, TargetC: 65},
		{Name: "Boil", Kind: "boil", Location: 1, Minutes: 30, TargetC: 95},
		{Name: "Hop slot 2", Kind: "hop", Location: 4, Minutes: 20, TargetC: 95},
		{Name: "Hop slot 1", Kind: "hop", Location: 3, Minutes: 5, TargetC: 95},
		{Name: "Return to slot 2", Kind: "hop", Location: 4, Minutes: 5, DrainMinutes: 5, TargetC: 95},
	}
	source.Ingredients = append(source.Ingredients, domain.Ingredient{Name: "Cascade", Kind: "hop", Amount: 12, Unit: "g", Use: "boil", Minutes: 30})
	code, body := request(t, c, "POST", h.URL+"/api/v1/recipes", token, source, nil)
	var result recipeView
	if code != 201 || json.Unmarshal(body, &result) != nil || len(result.MachineVersions) != 5 {
		t.Fatal(code, string(body))
	}
	for _, version := range result.MachineVersions {
		p := version.Profile
		if version.MachineRecipe == nil || len(version.MachineRecipe.Steps) != 5 || version.SourceRevision != 1 {
			t.Fatal(p.ID, version.DeploymentBlockers)
		}
		stock := p.ID == "zymatic"
		loc := 4
		if stock {
			loc--
		}
		steps := version.MachineRecipe.Steps
		if version.HopSlotsByMinute[30] != 2 || steps[2].Location != loc || steps[4].Location != loc || steps[2].Minutes != 20 || steps[4].Minutes != 5 {
			t.Fatal("changed explicit hop routing", p.ID)
		}
		if p.ID == "pico-s" && (result.BatchLiters != 4 || math.Abs(version.Recipe.BatchLiters-4*2/1.2) > 1e-10) {
			t.Fatal("automatic scaling lost the source or failed to fill the grain compartment")
		}
		if (stock || p.ID == "z") && steps[4].DrainMinutes != 10 {
			t.Fatal("wrong target drain", p.ID)
		}
		code, body = request(t, c, "POST", h.URL+"/api/v1/recipes/"+result.ID+"/adapt", token, map[string]string{"profile_id": p.ID}, nil)
		var preview domain.Adapted
		if code != 200 || json.Unmarshal(body, &preview) != nil || !reflect.DeepEqual(preview, version) {
			t.Fatal("preview differs from save", p.ID)
		}
		if stock {
			code, body = request(t, c, "POST", h.URL+"/api/v1/stock-zymatics/PLAN/recipes", token, stockSubmission{Recipes: []stockReference{{RecipeID: result.ID, Revision: 1}}}, nil)
			var menu struct{ Items []stockItem }
			if code != 200 || json.Unmarshal(body, &menu) != nil || len(menu.Items) != 1 || !reflect.DeepEqual(menu.Items[0].Program, *version.MachineRecipe) {
				t.Fatal("stock preview mismatched", string(body))
			}
		}
	}
	result.Recipe.Steps[0].Minutes = 53
	code, body = request(t, c, "PUT", h.URL+"/api/v1/recipes/"+result.ID, token, result, nil)
	if code != 200 || json.Unmarshal(body, &result) != nil {
		t.Fatal(code, string(body))
	}
	for _, v := range result.MachineVersions {
		if v.SourceRevision != 2 || v.MachineRecipe.Steps[0].Minutes != 53 {
			t.Fatal("stale version", v.Profile.ID)
		}
	}
	rows, err := s.db.List("recipe")
	if err != nil || len(rows) != 1 {
		t.Fatal("unexpected library copies", len(rows), err)
	}
}

func TestShoppingListKeepsUnspecifiedPitchSeparateFromMeasuredYeast(t *testing.T) {
	s, h, client := newTestServer(t)
	signIn(t, client, h.URL)
	r := validRecipe()
	r.SourceYeast = "SafAle US-05"
	if _, err := s.db.Put("recipe", "source", r, 0); err != nil {
		t.Fatal(err)
	}
	r.Ingredients = append(r.Ingredients, domain.Ingredient{Kind: "yeast", Name: r.SourceYeast, Amount: 8, Unit: "g", Use: "fermentation"})
	if _, err := s.db.Put("recipe", "measured", r, 0); err != nil {
		t.Fatal(err)
	}
	code, b := request(t, client, "GET", h.URL+"/api/v1/shopping-list?recipes=source,measured", "", nil, nil)
	var list []struct {
		domain.Ingredient
		AmountUnspecified bool `json:"amount_unspecified"`
	}
	if code != 200 || json.Unmarshal(b, &list) != nil {
		t.Fatalf("shopping list: %d %s", code, b)
	}
	var measured, unspecified int
	for _, i := range list {
		if i.Kind != "yeast" {
			continue
		}
		if i.Name != r.SourceYeast {
			t.Fatal(i)
		}
		if i.AmountUnspecified && i.Amount == 0 {
			unspecified++
		} else if !i.AmountUnspecified && i.Amount == 8 && i.Unit == "g" {
			measured++
		} else {
			t.Fatal(i)
		}
	}
	if measured != 1 || unspecified != 1 {
		t.Fatalf("mixed doses: %s", b)
	}
}

func TestJournalPreservesExplicitZeroCarbonationTemperature(t *testing.T) {
	r := domain.Recipe{Carbonation: domain.Carbonation{Method: "forced", TemperatureC: new(float64)}}
	plan := conditioningPlan(r)
	if plan.Stages[0].TargetC == nil || *plan.Stages[0].TargetC != 0 {
		t.Fatal(plan)
	}
	r.Carbonation.TemperatureC = nil
	if conditioningPlan(r).Stages[0].TargetC != nil {
		t.Fatal("invented temperature")
	}
}
