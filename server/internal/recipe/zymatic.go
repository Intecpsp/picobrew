package recipe

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// StockZymaticProgram converts an explicitly routed recipe into the AVR's valve
// numbering. Archived Zymatic programs already use that numbering, including
// location 6 for a pause. An unmapped canonical step must stay unmapped.
func StockZymaticProgram(r domain.Recipe, profile domain.Profile) (domain.MachineRecipe, error) {
	var p domain.MachineRecipe
	if err := Validate(r); err != nil {
		return p, err
	}
	if profile.ID != "zymatic" {
		return p, errors.New("choose the Zymatic profile")
	}
	if IsNative(r) {
		if r.SourceModel != "zymatic" {
			return p, errors.New("select a native Zymatic program or create a routed ingredient recipe")
		}
		p = *r.NativeProgram
		p.Steps = append([]domain.MachineStep(nil), p.Steps...)
	} else {
		if r.SourceModel != "zymatic" {
			return p, errors.New("choose each step’s Zymatic flow path before transfer")
		}
		if err := ValidateCapacity(r, profile); err != nil {
			return p, err
		}
		p.Name = r.Name
		for i, s := range r.Steps {
			if s.Location < 1 || s.Location > 6 {
				return p, fmt.Errorf("choose the flow path for step %d in the advanced editor", i+1)
			}
			if s.InfuseLiters != 0 || s.RampMinutes != 0 || s.Kind == "fermentation" || (s.Type != "" && s.Type != "Temperature") {
				return p, fmt.Errorf("step %d needs an operation the stock hot-side protocol cannot encode", i+1)
			}
			f := 0
			if s.TargetC != 0 {
				f = int(math.Round(s.TargetC*1.8 + 32))
			}
			p.Steps = append(p.Steps, domain.MachineStep{Name: s.Name, Location: s.Location - 1, TargetF: f, Minutes: s.Minutes, DrainMinutes: s.DrainMinutes})
		}
	}
	if len(p.Steps) > 25 {
		return p, errors.New("the stock recipe limit is 25 steps")
	}
	p.Name = stockLabel(p.Name, "Recipe")
	for i := range p.Steps {
		s := &p.Steps[i]
		if s.TargetF > 207 || s.Minutes > 255 || s.DrainMinutes > 255 {
			return p, fmt.Errorf("step %d exceeds the Zymatic limit of 207 °F or 255 minutes", i+1)
		}
		s.Name = stockLabel(s.Name, fmt.Sprintf("Step %d", i+1))
	}
	// Pin each wire identifier to the contents so recovery cannot change after an edit.
	data, err := json.Marshal([]any{r.ID, p.Name, p.Steps})
	if err != nil {
		return p, err
	}
	digest := sha256.Sum256(data)
	p.ID = hex.EncodeToString(digest[:16])
	for excess := len(StockZymaticRecord(p)) - 511; excess > 0; excess-- {
		longest := 0
		for i := range p.Steps {
			if len(p.Steps[i].Name) > len(p.Steps[longest].Name) {
				longest = i
			}
		}
		name := p.Steps[longest].Name
		if len(name) <= 1 {
			return p, errors.New("recipe exceeds the 511-byte stock Ethernet record limit")
		}
		p.Steps[longest].Name = name[:len(name)-1]
	}
	return p, nil
}
func stockLabel(s, fallback string) string {
	s = strings.Map(func(c rune) rune {
		if c < ' ' || c > '~' || strings.ContainsRune("#|/,", c) {
			return ' '
		}
		return c
	}, s)
	s = strings.Join(strings.Fields(s), " ")
	if s == "" {
		return fallback
	}
	if len(s) > 20 {
		s = strings.TrimSpace(s[:20])
	}
	return s
}
func StockZymaticRecord(p domain.MachineRecipe) string {
	var b strings.Builder
	fmt.Fprintf(&b, "%s/%s/", p.Name, p.ID)
	for _, s := range p.Steps {
		fmt.Fprintf(&b, "%s,%d,%d,%d,%d/", s.Name, s.TargetF, s.Minutes, s.Location, s.DrainMinutes)
	}
	return b.String()
}
