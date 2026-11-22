package domain

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"slices"
	"strings"
)

// ZProgram is the local Program Maker format. The archived cloud format
// assigns different meanings to the same numeric locations.
type ZProgram struct {
	Name           string         `json:"Name"`
	IsBeer         bool           `json:"IsBeer"`
	UsePicoPakMode bool           `json:"UsePicoPakMode"`
	IsMetric       *bool          `json:"IsMetric,omitempty"`
	Steps          []ZProgramStep `json:"Steps"`
}

type ZProgramStep struct {
	Name     string          `json:"Name"`
	Type     int             `json:"StepType"`
	Time     *int            `json:"Time,omitempty"`
	Location *int            `json:"Location,omitempty"`
	Temp     json.RawMessage `json:"Temp,omitempty"`
	Drain    *int            `json:"Drain,omitempty"`
}

// Field names are case-sensitive on the original appliance. Reject duplicates,
// omitted required fields, and unexpected fields before decoding Go values.
func zFields(b []byte, required, optional string) error {
	d := json.NewDecoder(bytes.NewReader(b))
	t, err := d.Token()
	if err != nil || t != json.Delim('{') {
		return errors.New("expected a Z Program Maker object")
	}
	fields := strings.Fields(required + " " + optional)
	seen := map[string]bool{}
	for d.More() {
		t, err := d.Token()
		if err != nil {
			return err
		}
		key, ok := t.(string)
		if !ok || seen[key] || !slices.Contains(fields, key) {
			return fmt.Errorf("unexpected or repeated Z program field %q", key)
		}
		seen[key] = true
		var raw json.RawMessage
		if err := d.Decode(&raw); err != nil {
			return err
		}
		if bytes.Equal(raw, []byte("null")) && key != "Temp" {
			return fmt.Errorf("%s cannot be null", key)
		}
	}
	for _, key := range strings.Fields(required) {
		if !seen[key] {
			return fmt.Errorf("missing Z program field %s", key)
		}
	}
	return nil
}
func (p *ZProgram) UnmarshalJSON(b []byte) error {
	if err := zFields(b, "Name IsBeer UsePicoPakMode Steps", "IsMetric"); err != nil {
		return err
	}
	type plain ZProgram
	return json.Unmarshal(b, (*plain)(p))
}
func (s *ZProgramStep) UnmarshalJSON(b []byte) error {
	if err := zFields(b, "Name StepType", "Time Location Temp Drain"); err != nil {
		return err
	}
	type plain ZProgramStep
	if err := json.Unmarshal(b, (*plain)(s)); err != nil {
		return err
	}
	if len(s.Temp) > 0 && string(s.Temp) != "null" {
		var c float64
		if err := json.Unmarshal(s.Temp, &c); err != nil {
			return errors.New("Z temperature must be a number or null")
		}
		s.Temp, _ = json.Marshal(c)
	}
	return nil
}

func (p ZProgram) Validate() error {
	validName := func(s string) bool {
		return strings.TrimSpace(s) != "" && len(s) <= 20 && strings.IndexFunc(s, func(c rune) bool { return c < 32 || c > 126 }) < 0
	}
	if !validName(p.Name) || len(p.Steps) < 1 || len(p.Steps) > 15 {
		return errors.New("Z programs require a printable name of up to 20 characters and 1–15 steps")
	}
	for i, s := range p.Steps {
		if !validName(s.Name) || s.Time != nil && (*s.Time < 0 || *s.Time > 86400) {
			return fmt.Errorf("check the name or duration of Z step %d", i+1)
		}
		switch s.Type {
		case 1:
			if s.Time == nil || s.Location == nil || *s.Location < 1 || *s.Location > 6 || s.Drain == nil || *s.Drain < 0 || *s.Drain > 7200 || len(s.Temp) == 0 {
				return fmt.Errorf("check the flow path, hold, temperature and drain on Z step %d", i+1)
			}
			var c float64
			if err := json.Unmarshal(s.Temp, &c); err != nil || math.IsNaN(c) || c < -29 || c > 98 {
				return fmt.Errorf("invalid temperature on Z step %d", i+1)
			}
		case 2:
			if s.Location != nil || len(s.Temp) > 0 || s.Drain != nil {
				return fmt.Errorf("pause step %d cannot heat, route or drain", i+1)
			}
		default:
			return fmt.Errorf("unsupported Z step type at step %d", i+1)
		}
	}
	return nil
}

func (r MachineRecipe) StepCount() int {
	if r.ZProgram != nil {
		return len(r.ZProgram.Steps)
	}
	return len(r.Steps)
}
