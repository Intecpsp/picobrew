package server

import (
	"math"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func (s *Server) seedDemo() error {
	existing, e := s.db.List("device")
	if e != nil {
		return e
	}
	if len(existing) > 0 {
		return nil
	}
	now := time.Now().UTC()
	devices := []domain.Device{{ID: "demo-pro", Name: "Pico Pro · Studio", Model: "pico-pro", Adapter: "simulator", Location: "Brew studio", Enabled: true, CreatedAt: now}, {ID: "demo-s", Name: "Pico S · Test bench", Model: "pico-s", Adapter: "simulator", Location: "Test bench", Enabled: true, CreatedAt: now}, {ID: "demo-z", Name: "Z · Pilot batch", Model: "z", Adapter: "simulator", Location: "Pilot room", Enabled: true, CreatedAt: now}}
	for _, d := range devices {
		if _, e = s.db.Put("device", d.ID, d, 0); e != nil {
			return e
		}
	}
	recipes := []domain.Recipe{{ID: "studio-porter", Name: "Studio Porter", Style: "Robust Porter", Type: "All Grain", BoilLiters: 6.5, BoilMinutes: 60, Description: "Cocoa, toasted bread and a clean finish.", BatchLiters: 5, Efficiency: 55, Ingredients: []domain.Ingredient{{Name: "Pale malt", Kind: "fermentable", Type: "Grain", Amount: 1.2, Unit: "kg", Use: "mash"}, {Name: "Chocolate malt", Kind: "fermentable", Type: "Grain", Amount: 0.1, Unit: "kg", Use: "mash"}, {Name: "East Kent Goldings", Kind: "hop", Amount: 18, Unit: "g", Use: "boil", Minutes: 60, Alpha: 5}, {Name: "English ale yeast", Kind: "yeast", Type: "Ale", Form: "Dry", Amount: 5, Unit: "g", Use: "primary"}}, Steps: []domain.RecipeStep{{Name: "Mash", Kind: "mash", TargetC: 67, Minutes: 60, Location: 1}, {Name: "Mash out", Kind: "mash", TargetC: 76, Minutes: 10, Location: 1}, {Name: "Boil", Kind: "boil", TargetC: 100, Minutes: 60, Location: 2}}, Tags: []string{"dark", "studio"}, Notes: "Example profile: verify your physical chamber assignments before using a real machine.", Revision: 1, CreatedAt: now, UpdatedAt: now}, {ID: "weekend-pale", Name: "Weekend Pale", Style: "American Pale Ale", Type: "All Grain", BoilLiters: 6.5, BoilMinutes: 60, Description: "Bright citrus with a soft malt backbone.", BatchLiters: 5, Efficiency: 55, Ingredients: []domain.Ingredient{{Name: "Pale malt", Kind: "fermentable", Type: "Grain", Amount: 1.2, Unit: "kg", Use: "mash"}, {Name: "Cascade", Kind: "hop", Amount: 20, Unit: "g", Use: "boil", Minutes: 30, Alpha: 6}}, Steps: []domain.RecipeStep{{Name: "Mash", Kind: "mash", TargetC: 65, Minutes: 60, Location: 1}, {Name: "Boil", Kind: "boil", TargetC: 100, Minutes: 60, Location: 2}}, Tags: []string{"pale", "weekend"}, Revision: 1, CreatedAt: now, UpdatedAt: now}}
	for _, v := range recipes {
		if _, e = s.db.Put("recipe", v.ID, v, 0); e != nil {
			return e
		}
		_, _ = s.db.Put("recipe-revision", v.ID+":1", v, 0)
	}
	for j, d := range devices {
		start := now.Add(-time.Duration(j+1) * 24 * time.Hour)
		b := domain.Brew{ID: "demo-history-" + d.ID, DeviceID: d.ID, DeviceName: d.Name, Model: d.Model, RecipeName: recipes[j%2].Name, RecipeID: recipes[j%2].ID, RecipeRevision: 1, State: "completed", Command: "brew", StartedAt: &start, ObservedAt: start, Source: "simulation", Notes: "Simulated brew history for exploring graphs and exports.", Rating: 4}
		schedule := recipes[j%2].Steps
		b.StepNames = make(map[int]string, len(schedule))
		for i, step := range schedule {
			b.StepNames[i] = step.Name
			b.DurationMS += int64(step.Minutes) * 60000
		}
		b.SampleCount = int(b.DurationMS/15000) + 1
		samples := make([]domain.Sample, 0, b.SampleCount)
		for i := 0; i < b.SampleCount; i++ {
			elapsed := i * 15000
			minute := float64(elapsed) / 60000
			stepIndex := 0
			stepStart := 0
			for stepIndex < len(schedule)-1 && minute >= float64(stepStart+schedule[stepIndex].Minutes) {
				stepStart += schedule[stepIndex].Minutes
				stepIndex++
			}
			step := schedule[stepIndex]
			target := step.TargetC*1.8 + 32
			previous := 72.0
			if stepIndex > 0 {
				previous = schedule[stepIndex-1].TargetC*1.8 + 32
			}
			wort := previous + math.Min((minute-float64(stepStart))/math.Min(15, float64(step.Minutes)), 1)*(target-previous) + math.Sin(minute)*0.4
			thermo := wort + 6 + math.Sin(minute*0.4)*2
			at := start.Add(time.Duration(elapsed) * time.Millisecond)
			samples = append(samples, domain.Sample{ElapsedMS: int64(elapsed), ObservedAt: &at, WortF: floatPtr(wort), ThermoblockF: floatPtr(thermo), TargetF: floatPtr(target), StepSeconds: i*15 - stepStart*60, Phase: stepIndex + 1, Compartment: step.Location, CurrentStep: stepIndex, Flags: 11, DeviceState: 1, ControllerStatus: 1, Source: "simulation"})
		}
		if _, e = s.db.Put("brew", b.ID, b, 0); e != nil {
			return e
		}
		if e = s.db.AppendSamples(b.ID, samples); e != nil {
			return e
		}
	}
	return nil
}
func floatPtr(v float64) *float64 { return &v }
