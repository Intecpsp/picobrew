package server

import (
	"encoding/json"
	"math"
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
	if preview.Recipe.BatchLiters != 5 || preview.CanDeploy || preview.MachineRecipe != nil || len(preview.DeploymentBlockers) != 2 || preview.ProfileFingerprint == "" {
		t.Fatalf("incomplete or unexpectedly deployable planning preview: %+v", preview)
	}
	expected := source.Ingredients[0].Amount * (5 / source.BatchLiters) * (source.Efficiency / 50)
	if math.Abs(preview.Recipe.Ingredients[0].Amount-expected) > 1e-12 || preview.Recipe.Steps[0].Location != 0 || preview.Recipe.Steps[0].Minutes != source.Steps[0].Minutes {
		t.Fatal("planning lost ingredients or invented schedule/routing")
	}
	status, body = request(t, c, "POST", h.URL+"/api/v1/devices/bulk/deploy", csrf, map[string]any{
		"recipe_id": source.ID, "profile_id": "pico-pro", "batch_liters": 5,
		"source_revision": preview.SourceRevision, "profile_fingerprint": preview.ProfileFingerprint,
	}, nil)
	if status != 422 || !strings.Contains(string(body), "chamber location") || adapter.writes != 0 {
		t.Fatalf("unmapped single deployment was not denied: %d writes=%d %s", status, adapter.writes, body)
	}
	selection := DeploymentInput{ProfileID: "pico-pro", Items: []DeploymentSelection{{RecipeID: source.ID, SourceRevision: source.Revision, BatchLiters: 5}}}
	plan := previewTransfer(t, c, h.URL, csrf, selection)
	if plan.PlannedCount != 0 || plan.Items[0].Status != "skipped" || plan.Items[0].Native != nil || !strings.Contains(plan.Items[0].Reason, "chamber location") {
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
					"recipe_id": source.ID, "profile_id": plan.Profile.ID, "batch_liters": plan.Recipe.BatchLiters,
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
