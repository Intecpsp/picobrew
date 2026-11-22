package recipe

import (
	"fmt"
	"math"
	"slices"
	"sort"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// Translate derives a model version from the canonical recipe. Native programs
// keep their original encoding and follow the existing compatibility rules.
func Translate(r domain.Recipe, p domain.Profile, liters float64) (domain.Adapted, error) {
	if IsNative(r) || len(r.Ingredients) == 0 {
		return Adapt(r, p, liters)
	}

	if err := Validate(r); err != nil {
		return domain.Adapted{}, err
	}
	staged := r
	staged.SourceModel = p.ID
	routed := 0
	for _, s := range r.Steps {
		if s.Location != 0 {
			routed++
		}
	}
	issue := ""
	switch {
	case routed == 0:
		var steps []domain.RecipeStep
		steps, issue = routeIngredientSteps(r, p)
		if issue == "" {
			staged.Steps = steps
			if err := Validate(staged); err != nil {
				issue = err.Error()
				staged.Steps = r.Steps
			}
		}
	case routed < len(r.Steps):
		issue = "Complete the partially assigned flow paths in the recipe."
	case r.SourceModel != p.ID:
		staged.Steps = translateRoutedSteps(r, p)
	}
	slots := firstHopRoutes(staged.Steps)
	if issue == "" && routed > 0 {
		issue = checkRoutedHops(staged, slots)
	}
	plan, err := adapt(staged, p, liters, true)
	if err != nil {
		return plan, err
	}
	if issue != "" {
		plan.CanDeploy = false
		plan.MachineRecipe = nil
		plan.DeploymentBlockers = []string{issue}
		if capacityErr := ValidateCapacity(plan.Recipe, p); capacityErr != nil {
			plan.DeploymentBlockers = append(plan.DeploymentBlockers, capacityErr.Error())
		}
	} else if p.ID == "zymatic" && ValidateCapacity(plan.Recipe, p) == nil {
		program, err := StockZymaticProgram(plan.Recipe, p)
		if err != nil {
			plan.DeploymentBlockers = []string{err.Error()}
		} else {
			plan.MachineRecipe = &program
			plan.DeploymentBlockers = nil
		}
	}
	if plan.MachineRecipe != nil {
		plan.HopSlotsByMinute = slots
	}
	for i, in := range plan.Recipe.Ingredients {
		if in.Name == "Source starting water" && in.Kind == "water" {
			plan.Recipe.Ingredients[i].Name = "Scaled source water (reference)"
		}
	}
	if plan.Recipe.BatchLiters < p.BatchLiters {
		plan.Warnings = append(plan.Warnings, "Check the working water volume for this smaller batch and your insert.")
	}
	if plan.Recipe.BatchLiters > p.BatchLiters+1e-9 {
		plan.Warnings = append(plan.Warnings, "The source recipe implies a yield above this brewer’s nominal volume. Use the brewer’s working fill level. Actual yield depends on absorption and losses.")
	}
	if routed == 0 && plan.MachineRecipe != nil {
		plan.Warnings = append(plan.Warnings, "This program follows the declared mash and hop times. Check the original instructions for manual additions, pauses and chilling.")
	}
	if r.SourceModel != p.ID && routed > 0 && !slices.Equal(r.Steps, staged.Steps) {
		plan.Warnings = append(plan.Warnings, "The schedule uses the destination brewer’s temperature limits and standard drain times.")
	}
	if r.Provenance != nil {
		plan.Warnings = append(plan.Warnings, "Review the source notes for yeast pitch, manual additions and water losses on your equipment.")
	}
	return plan, nil
}

// Use the first direct flow to a slot to assign its ingredient load. Later
// visits to the same slot recirculate the existing load.
func firstHopRoutes(steps []domain.RecipeStep) map[int]int {
	remaining := 0
	for _, step := range steps {
		if step.Kind == "boil" || step.Kind == "hop" {
			remaining += step.Minutes
		}
	}
	first := map[int]int{}
	visited := map[int]bool{}
	for _, step := range steps {
		if step.Kind != "boil" && step.Kind != "hop" && step.Kind != "heat" {
			continue
		}
		if step.Location >= 3 && !visited[step.Location] {
			visited[step.Location] = true
			first[remaining] = step.Location - 2
		}
		if step.Kind != "heat" {
			remaining -= step.Minutes
		}
	}
	return first
}

func checkRoutedHops(r domain.Recipe, first map[int]int) string {
	boilMinutes, preheatedSlot := 0, 0
	for i, step := range r.Steps {
		if step.Kind != "boil" && step.Kind != "hop" {
			continue
		}
		if boilMinutes == 0 && i > 0 {
			heat := r.Steps[i-1]
			if heat.Kind == "heat" && heat.Location >= 3 && heat.Location == step.Location {
				preheatedSlot = heat.Location - 2
			}
		}
		boilMinutes += step.Minutes
	}
	for _, in := range r.Ingredients {
		use := strings.ToLower(strings.TrimSpace(in.Use))
		if in.SlotAddition || in.Kind == "hop" && (use == "boil" || use == "first-wort" || use == "first wort") {
			if use == "first-wort" || use == "first wort" {
				if in.Minutes != float64(boilMinutes) || preheatedSlot == 0 || first[boilMinutes] != preheatedSlot {
					return "First-wort hops must receive flow during heat-up and the full boil."
				}
			}
			if in.Minutes != math.Trunc(in.Minutes) || first[int(in.Minutes)] == 0 {
				return fmt.Sprintf("Assign %s a hop slot that first receives direct flow at %g minutes before boil ends.", in.Name, in.Minutes)
			}
		}
	}
	return ""
}

func equipmentDefaults(model string) (mashDrain, hopDrain int, hopF float64) {
	if model == "z" || model == "zymatic" {
		return 8, 10, 207
	}
	return 2, 5, 203
}

// Keep explicit routes (including return visits), holds and custom drains.
// Change the standard drain allowances and constrain hot-side targets on the
// destination. A lower authored temperature remains a lower target.
func translateRoutedSteps(r domain.Recipe, p domain.Profile) []domain.RecipeStep {
	steps := slices.Clone(r.Steps)
	originalMash, originalHop, _ := equipmentDefaults(r.SourceModel)
	mash, hop, hotF := equipmentDefaults(p.ID)
	for i := range steps {
		s := &steps[i]
		if r.SourceModel != "" {
			if (s.Kind == "mash" || s.Kind == "mash-out") && s.DrainMinutes == originalMash {
				s.DrainMinutes = mash
			}
			if s.Kind == "hop" && s.DrainMinutes == originalHop {
				s.DrainMinutes = hop
			}
		}
		if s.Kind == "boil" || s.Kind == "hop" || s.Kind == "heat" && s.Location >= 3 {
			s.TargetC = math.Min(s.TargetC, (hotF-32)/1.8)
		}
		if p.ID == "z" && s.Location == 1 {
			s.TargetC = math.Min(s.TargetC, (202.0-32)/1.8)
		}
	}
	return steps
}

func routeIngredientSteps(r domain.Recipe, p domain.Profile) ([]domain.RecipeStep, string) {
	var rests []domain.RecipeStep
	var boil domain.RecipeStep
	for _, s := range r.Steps {
		switch s.Kind {
		case "mash", "mash-out":
			if boil.Minutes > 0 || s.InfuseLiters != 0 || s.RampMinutes != 0 || (s.Type == "Decoction" || s.Type == "Infusion" && (r.Provenance == nil || r.Provenance.SourceID != "public-recipes")) || s.Minutes <= 0 || s.TargetC <= 0 {
				return nil, "Review the source mash operation and its equipment requirements."
			}
			s.Type = "Temperature"
			s.Location = 2
			rests = append(rests, s)
		case "boil":
			if boil.Minutes != 0 || s.TargetC < 85 || s.DrainMinutes != 0 || s.RampMinutes != 0 || s.InfuseLiters != 0 || s.Type != "" && s.Type != "Temperature" {
				return nil, "Review the source boil stages and temperatures."
			}
			boil = s
		default:
			return nil, fmt.Sprintf("Map the source step %q (%s) for this brewer.", s.Name, s.Kind)
		}
	}
	if r.BoilMinutes != 0 && float64(boil.Minutes) != r.BoilMinutes {
		return nil, "The declared boil duration and scheduled boil disagree. Choose the intended duration."
	}
	if len(rests) == 0 || boil.Minutes <= 0 {
		return nil, "Supply a timed mash and boil before preparing a machine program."
	}
	if r.Provenance != nil {
		for _, warning := range r.Provenance.Warnings {
			if strings.HasPrefix(warning, "Source program review:") || strings.Contains(warning, "first-wort") || strings.Contains(warning, "non-default source BoilUse") || strings.Contains(warning, "original source contains errors") || strings.Contains(warning, "fractional") || strings.Contains(warning, "unsupported mash style") || strings.Contains(warning, "not ingredient totals") {
				return nil, warning
			}
		}
	}
	times := map[int]bool{}
	firstWort := false
	for _, in := range r.Ingredients {
		if in.Kind == "misc" && in.Use == "boil" && !in.SlotAddition {
			return nil, fmt.Sprintf("Confirm the addition method for %s during the boil.", in.Name)
		}
		if in.Kind == "hop" && in.Use != "first-wort" && in.Use != "boil" && in.Use != "dry-hop" && in.Use != "dry hop" {
			return nil, fmt.Sprintf("Confirm the %s addition method for %s.", in.Use, in.Name)
		}
		if in.Kind == "hop" && in.Use == "first-wort" {
			if in.Minutes != float64(boil.Minutes) {
				return nil, "First-wort hops must receive flow during heat-up and the full boil."
			}
			firstWort = true
		}
		if !in.SlotAddition && (in.Kind != "hop" || in.Use != "boil" && in.Use != "first-wort") {
			continue
		}
		if in.Minutes <= 0 || in.Minutes > float64(boil.Minutes) || in.Minutes != math.Trunc(in.Minutes) {
			return nil, "The source includes a flameout, fractional or out-of-range hop time. Assign its manual or timed addition."
		}
		times[int(in.Minutes)] = true
	}
	if len(times) > p.HopSlots || len(times) > 4 {
		return nil, "More than four separate hop times require a manual addition or a revised recipe."
	}
	var starts []int
	for t := range times {
		starts = append(starts, t)
	}
	sort.Sort(sort.Reverse(sort.IntSlice(starts)))
	mashDrain, hopDrain, maxF := equipmentDefaults(p.ID)
	target := math.Min(boil.TargetC, (maxF-32)/1.8)
	keg := target
	if p.ID == "z" {
		keg = math.Min(keg, (202.0-32)/1.8)
	}
	steps := []domain.RecipeStep{{Name: "Heat water", Kind: "heat", Location: 1, TargetC: rests[0].TargetC}}
	if rests[len(rests)-1].DrainMinutes == 0 {
		rests[len(rests)-1].DrainMinutes = mashDrain
	}
	steps = append(steps, rests...)
	heat := domain.RecipeStep{Name: "Heat to boil", Kind: "heat", Location: 1, TargetC: keg}
	if firstWort {
		for _, in := range r.Ingredients {
			if (in.SlotAddition || in.Kind == "hop" && in.Use == "boil") && in.Minutes == float64(boil.Minutes) {
				return nil, "First-wort and boil-start additions require separate exposure times."
			}
		}
		heat.Location, heat.TargetC = 3, target
	}
	steps = append(steps, heat)
	starts = append(starts, 0)
	if n := boil.Minutes - starts[0]; n > 0 {
		steps = append(steps, domain.RecipeStep{Name: "Boil", Kind: "boil", Location: 1, TargetC: keg, Minutes: n})
	}
	for i := 0; i+1 < len(starts); i++ {
		step := domain.RecipeStep{Name: fmt.Sprintf("Hop slot %d", i+1), Kind: "hop", Location: i + 3, TargetC: target, Minutes: starts[i] - starts[i+1]}
		if i+2 == len(starts) {
			step.DrainMinutes = hopDrain
		}
		steps = append(steps, step)
	}
	return steps, ""
}
