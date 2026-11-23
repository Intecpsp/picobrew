package recipe

import (
	"fmt"
	"math"
	"slices"
	"sort"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

const minBoilTargetC = 85

// A recipe-wide blocker applies even when the flow paths have been assigned.
func programBlocker(r domain.Recipe) string {
	if r.Provenance != nil {
		for _, warning := range r.Provenance.Warnings {
			if strings.HasPrefix(warning, "Source program review:") {
				return warning
			}
		}
	}
	for _, in := range r.Ingredients {
		if in.Kind != "hop" && in.Kind != "misc" {
			continue
		}
		switch strings.ToLower(strings.TrimSpace(in.Use)) {
		case "aroma", "whirlpool", "flameout", "flame out", "flame-out", "hop stand", "hop-stand", "hopstand", "post-boil", "post boil":
		case "boil":
			if in.Kind != "hop" || in.Minutes != 0 {
				continue
			}
		default:
			continue
		}
		return fmt.Sprintf("The post-boil addition of %s has no verified loading slot and contact time at its target temperature. This recipe is available for planning.", in.Name)
	}
	return ""
}

func applyProgramBlocker(plan *domain.Adapted) {
	reason := programBlocker(plan.Recipe)
	if reason == "" {
		return
	}
	plan.CanDeploy = false
	plan.MachineRecipe = nil
	plan.HopSlotsByMinute = nil
	if !slices.Contains(plan.DeploymentBlockers, reason) {
		plan.DeploymentBlockers = append([]string{reason}, plan.DeploymentBlockers...)
	}
}

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
	if issue == "" && routed == len(r.Steps) {
		staged.Steps = prepareBoilStart(staged, p)
	}
	slots := firstHopRoutes(staged.Steps)
	if issue == "" {
		issue = checkRoutedIngredients(staged, slots)
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
	applyProgramBlocker(&plan)
	return plan, nil
}

// A regular full-boil addition must stay dry while wort heats after the mash.
// Older wizard recipes went directly from mash-out to a timed hop step.
func prepareBoilStart(r domain.Recipe, p domain.Profile) []domain.RecipeStep {
	for i, s := range r.Steps {
		if s.Kind != "boil" && s.Kind != "hop" {
			continue
		}
		if s.Location < 3 || s.TargetC == 0 {
			break
		}
		fullBoil := boilDuration(r.Steps)
		regular := false
		for _, in := range r.Ingredients {
			use := strings.ToLower(strings.TrimSpace(in.Use))
			if in.Minutes == float64(fullBoil) && (in.SlotAddition || in.Kind == "hop" && use == "boil") {
				regular = true
				break
			}
		}
		if !regular {
			break
		}
		target := s.TargetC
		if p.ID == "z" {
			target = math.Min(target, (202.0-32)/1.8)
		}
		if i > 0 && r.Steps[i-1].TargetC >= target {
			break
		}
		heat := domain.RecipeStep{Name: "Heat to boil", Kind: "heat", Location: 1, TargetC: target}
		return slices.Insert(slices.Clone(r.Steps), i, heat)
	}
	return r.Steps
}

func boilDuration(steps []domain.RecipeStep) int {
	total := 0
	for _, step := range steps {
		if step.Kind == "boil" || step.Kind == "hop" {
			total += step.Minutes
		}
	}
	return total
}

// A loaded slot is identified by its first timed direct visit. Later visits
// recirculate its existing load.
func firstHopRoutes(steps []domain.RecipeStep) map[int]int {
	remaining := boilDuration(steps)
	first := map[int]int{}
	visited := map[int]bool{}
	for _, step := range steps {
		if step.Kind != "boil" && step.Kind != "hop" {
			continue
		}
		if step.Location >= 3 && !visited[step.Location] {
			visited[step.Location] = true
			first[remaining] = step.Location - 2
		}
		remaining -= step.Minutes
	}
	return first
}

func checkRoutedIngredients(r domain.Recipe, first map[int]int) string {
	type exposure struct{ index, minutes int }
	wet := map[int]exposure{}
	duration := boilDuration(r.Steps)
	if r.BoilMinutes != 0 && r.BoilMinutes != float64(duration) {
		return "The declared boil duration and scheduled boil disagree. Choose the intended duration."
	}
	remaining := duration
	mashRest := false
	mashLoad, additions := ingredientLoads(r)
	var firstAddition float64
	for minute := range additions {
		firstAddition = max(firstAddition, minute)
	}
	for i, step := range r.Steps {
		if mashLoad > 0 && (step.Kind == "mash" || step.Kind == "mash-out") && (remaining != duration || step.Location != 2 || step.TargetC == 0) {
			return fmt.Sprintf("The mash step %q must heat through the grain basket before the boil.", step.Name)
		}
		if float64(remaining) < firstAddition && remaining > 0 && step.Kind != "boil" && step.Kind != "hop" {
			return fmt.Sprintf("The step %q interrupts the timed boil and changes hop exposure. Keep the boil and hop stages together.", step.Name)
		}
		if remaining == duration && step.Location == 2 && step.TargetC > 0 && step.Minutes > 0 && (step.Kind == "mash" || step.Kind == "hold") {
			mashRest = true
		}
		// Wort cascades through the lower-numbered slots on every visit.
		for slot := 1; slot <= step.Location-2; slot++ {
			if _, seen := wet[slot]; !seen {
				wet[slot] = exposure{i, remaining}
			}
		}
		if step.Kind == "boil" || step.Kind == "hop" {
			if step.Minutes > 0 && step.TargetC < minBoilTargetC {
				return fmt.Sprintf("The timed boil step %q needs a target of at least %d °C.", step.Name, minBoilTargetC)
			}
			remaining -= step.Minutes
			if step.DrainMinutes > 0 && remaining > 0 && float64(remaining) < firstAddition {
				return fmt.Sprintf("The drain after %q interrupts hop circulation before the timed boil ends. Move the drain to the final boil step.", step.Name)
			}
		}
	}
	for _, in := range r.Ingredients {
		use := strings.ToLower(strings.TrimSpace(in.Use))
		if !mashRest && mashLoadKG(in) > 0 {
			return "Add a heated, timed mash rest through the grain basket for " + in.Name + " before the boil."
		}
		firstWort := use == "first-wort" || use == "first wort"
		if !in.SlotAddition && !(in.Kind == "hop" && (use == "boil" || firstWort)) {
			continue
		}
		slot := first[int(in.Minutes)]
		if in.Minutes != math.Trunc(in.Minutes) || slot == 0 {
			return fmt.Sprintf("Assign %s a hop slot that first receives direct flow at %g minutes before boil ends.", in.Name, in.Minutes)
		}
		touch := wet[slot]
		step := r.Steps[touch.index]
		if firstWort {
			next := touch.index + 1
			if in.Minutes != float64(duration) || step.Kind != "heat" || step.Location != slot+2 || step.DrainMinutes != 0 ||
				next >= len(r.Steps) || (r.Steps[next].Kind != "boil" && r.Steps[next].Kind != "hop") || r.Steps[next].Location != slot+2 {
				return "First-wort hops must receive flow during heat-up and the full boil."
			}
		} else if touch.minutes != int(in.Minutes) || (step.Kind != "boil" && step.Kind != "hop") || step.Location != slot+2 {
			return fmt.Sprintf("Slot %d wets %s before its %g-minute boil addition. Start loaded slots in ascending order and heat through the keg before regular boil hops.", slot, in.Name, in.Minutes)
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
		if s.Kind == "boil" || s.Kind == "hop" || s.Kind == "heat" && (s.Location >= 3 || s.Location == 1 && i+1 < len(steps) && (steps[i+1].Kind == "hop" || steps[i+1].Kind == "boil")) {
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
		operation := strings.ToLower(strings.TrimSpace(s.Type))
		switch s.Kind {
		case "mash", "mash-out":
			if boil.Minutes > 0 || s.InfuseLiters != 0 || s.RampMinutes != 0 || (operation == "decoction" || operation == "infusion" && (r.Provenance == nil || r.Provenance.SourceID != "public-recipes")) || s.Minutes <= 0 || s.TargetC <= 0 {
				return nil, "Review the source mash operation and its equipment requirements."
			}
			s.Type = "Temperature"
			s.Location = 2
			rests = append(rests, s)
		case "boil":
			if boil.Minutes != 0 || s.TargetC < minBoilTargetC || s.DrainMinutes != 0 || s.RampMinutes != 0 || s.InfuseLiters != 0 || operation != "" && operation != "temperature" {
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
	// Keg extract recipes need only the boil. Verify the ingredients so a
	// mislabeled grain recipe cannot lose its mash during conversion.
	extract := strings.EqualFold(strings.TrimSpace(r.Type), "Extract") &&
		slices.ContainsFunc(r.Ingredients, func(in domain.Ingredient) bool { return in.Kind == "fermentable" }) &&
		!slices.ContainsFunc(r.Ingredients, func(in domain.Ingredient) bool {
			return mashLoadKG(in) > 0 || in.Kind == "fermentable" && !kettleFermentable(in.Type)
		})
	if boil.Minutes <= 0 || len(rests) == 0 && !extract {
		return nil, "Supply a timed mash and boil before preparing a machine program."
	}
	if r.Provenance != nil {
		for _, warning := range r.Provenance.Warnings {
			if strings.Contains(warning, "first-wort") || strings.Contains(warning, "non-default source BoilUse") || strings.Contains(warning, "original source contains errors") || strings.Contains(warning, "fractional") || strings.Contains(warning, "unsupported mash style") || strings.Contains(warning, "not ingredient totals") {
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
		if in.Kind == "hop" && in.Use != "first-wort" && in.Use != "boil" && in.Use != "mash" && in.Use != "dry-hop" && in.Use != "dry hop" {
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
	var steps []domain.RecipeStep
	if len(rests) > 0 {
		steps = append(steps, domain.RecipeStep{Name: "Heat water", Kind: "heat", Location: 1, TargetC: rests[0].TargetC})
		if rests[len(rests)-1].DrainMinutes == 0 {
			rests[len(rests)-1].DrainMinutes = mashDrain
		}
		steps = append(steps, rests...)
	}
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
