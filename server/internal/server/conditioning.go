package server

import (
	"errors"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

func conditioningPlan(r domain.Recipe) *domain.Conditioning {
	plan := &domain.Conditioning{RecipeName: r.Name, RecipeRevision: r.Revision}
	for _, step := range r.Fermentation {
		target := step.TargetC
		plan.Stages = append(plan.Stages, domain.ConditioningStage{Name: step.Name, TargetC: &target, Days: step.Days})
	}
	c := r.Carbonation
	if c != (domain.Carbonation{}) {
		label := "Carbonation"
		switch c.Method {
		case "forced":
			label = "Force carbonation"
		case "natural":
			label = "Bottle or keg conditioning"
		}
		stage := domain.ConditioningStage{Name: label, Volumes: c.Volumes}
		stage.TargetC = c.TemperatureC
		plan.Stages = append(plan.Stages, stage)
	}
	if len(plan.Stages) == 0 {
		return nil
	}
	return plan
}

type ConditioningInput struct {
	Revision       int64      `json:"revision"`
	Action         string     `json:"action"`
	RecipeID       string     `json:"recipe_id,omitempty"`
	RecipeRevision int64      `json:"recipe_revision,omitempty"`
	Stage          *int       `json:"stage,omitempty"`
	Name           string     `json:"name,omitempty"`
	At             *time.Time `json:"at,omitempty"`
}

func (s *Server) archivedConditioning(id string, revision int64) (*domain.Conditioning, error) {
	var r domain.Recipe
	_, err := s.db.Get("recipe-revision", id+":"+strconv.FormatInt(revision, 10), &r)
	if err != nil {
		return nil, err
	}
	return conditioningPlan(r), nil
}
func (s *Server) updateConditioning(w http.ResponseWriter, r *http.Request) {
	var in ConditioningInput
	if !s.decode(w, r, &in) {
		return
	}
	id := chi.URLParam(r, "id")
	var brew domain.Brew
	version, err := s.db.Get("brew", id, &brew)
	if err != nil {
		s.storeError(w, err)
		return
	}
	if brew.Command != "brew" {
		s.fail(w, 400, "conditioning", "Choose a brewing session.")
		return
	}
	plan := brew.Conditioning
	if plan == nil && brew.RecipeID != "" && brew.RecipeRevision > 0 {
		plan, err = s.archivedConditioning(brew.RecipeID, brew.RecipeRevision)
		if err != nil && !errors.Is(err, store.ErrNotFound) {
			s.storeError(w, err)
			return
		}
	}
	if plan == nil {
		plan = &domain.Conditioning{}
	}
	if in.Revision != plan.Revision {
		s.storeError(w, store.ErrConflict)
		return
	}
	switch in.Action {
	case "use_recipe":
		if len(plan.Stages) > 0 {
			s.fail(w, 409, "conditioning", "This brew already has a plan.")
			return
		}
		if in.RecipeID == "" || in.RecipeRevision <= 0 {
			s.fail(w, 400, "conditioning", "Choose a recipe revision.")
			return
		}
		plan, err = s.archivedConditioning(in.RecipeID, in.RecipeRevision)
		if err != nil {
			s.storeError(w, err)
			return
		}
		if plan == nil {
			s.fail(w, 400, "conditioning", "This recipe has no fermentation or carbonation stages.")
			return
		}
	case "add":
		name := strings.TrimSpace(in.Name)
		if name == "" || len(name) > 512 || len(plan.Stages) >= 64 {
			s.fail(w, 400, "conditioning", "Enter a stage name. Each brew supports up to 64 stages.")
			return
		}
		plan.Stages = append(plan.Stages, domain.ConditioningStage{Name: name})
	case "start", "finish":
		if in.Stage == nil || *in.Stage < 0 || *in.Stage >= len(plan.Stages) {
			s.fail(w, 400, "conditioning", "Choose a stage.")
			return
		}
		stage := &plan.Stages[*in.Stage]
		now := time.Now().UTC()
		at := now
		if in.At != nil {
			at = in.At.UTC()
		}
		if at.IsZero() || at.After(now.Add(time.Minute)) {
			s.fail(w, 400, "conditioning", "Choose a time in the past or present.")
			return
		}
		if in.Action == "start" && stage.StartedAt == nil {
			stage.StartedAt = &at
		} else if in.Action == "finish" && stage.StartedAt != nil && stage.CompletedAt == nil && !at.Before(*stage.StartedAt) {
			stage.CompletedAt = &at
		} else {
			s.fail(w, 409, "conditioning", "Check the stage status and its start time.")
			return
		}
	default:
		s.fail(w, 400, "conditioning", "Unknown journal action.")
		return
	}
	plan.Revision = in.Revision + 1
	brew.Conditioning = plan
	if _, err = s.db.Put("brew", id, brew, version); err != nil {
		s.storeError(w, err)
		return
	}
	s.json(w, 200, plan)
}
