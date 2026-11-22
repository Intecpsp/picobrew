package catalog

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// Read the separately authored machine program exactly as stored in Beer Crafter.
func publicStockProgram(d Detail) (*domain.MachineRecipe, error) {
	var params struct {
		RecipeID     int
		MachineSteps []struct {
			Name                                       string
			ID, StepLocation, Temperature, Time, Drain int
		}
	}
	if err := json.Unmarshal(d.SourceParameters, &params); err != nil {
		return nil, err
	}
	if params.RecipeID <= 0 || len(params.MachineSteps) == 0 {
		return nil, fmt.Errorf("catalog: missing public program for %s", d.ID)
	}
	p := domain.MachineRecipe{ID: strconv.Itoa(params.RecipeID), Name: d.Name}
	for _, s := range params.MachineSteps {
		if s.ID != params.RecipeID {
			return nil, fmt.Errorf("catalog: machine step belongs to another recipe in %s", d.ID)
		}
		p.Steps = append(p.Steps, domain.MachineStep{Name: s.Name, Location: s.StepLocation, TargetF: s.Temperature, Minutes: s.Time, DrainMinutes: s.Drain})
	}
	return &p, nil
}

func (d Detail) BrewingNotes() string {
	return "Zymatic brew sheet\n" + strings.Join(d.BrewSheet.Instructions, "\n") + "\nPitching guide: " + d.BrewSheet.YeastGuide
}

// A dedicated program copy follows the existing stock-menu transfer path. Changes
// to an ingredient plan can never silently deploy the original machine routing.
func (d Detail) StockRecipe() domain.Recipe {
	return domain.Recipe{Name: d.Name, Kind: "native-program", SourceModel: "zymatic", Style: d.Style, Description: d.Description, Provenance: &d.Provenance, NativeProgram: d.StockProgram, Notes: d.BrewingNotes() + "\n\n" + d.Recipe.Notes}
}
