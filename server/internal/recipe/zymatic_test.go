package recipe_test

import (
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
	"strings"
	"testing"
)

func TestStockWireBoundsAndNativeEncoding(t *testing.T) {
	var profile domain.Profile
	for _, p := range recipe.Profiles() {
		if p.ID == "zymatic" {
			profile = p
		}
	}
	r := archivedProgram()
	r.SourceModel = "zymatic"
	r.Name = "Brew # /|,"
	r.NativeProgram.Name = r.Name
	r.NativeProgram.Steps = []domain.MachineStep{{Name: "Pause", Location: 6}}
	for i := 1; i < 25; i++ {
		r.NativeProgram.Steps = append(r.NativeProgram.Steps, domain.MachineStep{Name: strings.Repeat("H", 25), Location: 5, Minutes: 18, DrainMinutes: 7, TargetF: 207})
	}
	p, err := recipe.StockZymaticProgram(r, profile)
	if err != nil {
		t.Fatal(err)
	}
	record := recipe.StockZymaticRecord(p)
	if len(record) > 511 || strings.ContainsAny(record, "#|") || !strings.Contains(record, "Pause,0,0,6,0/") || len(r.NativeProgram.Steps[1].Name) != 25 {
		t.Fatal("bad framing or source mutation", record)
	}
	r.NativeProgram.Steps[1].Minutes = 256
	if _, err := recipe.StockZymaticProgram(r, profile); err == nil {
		t.Fatal("silently truncated duration")
	}
	r = source()
	r.SourceModel = "zymatic"
	r.BatchLiters = 5
	r.Steps[0].Location = 0
	if _, err := recipe.StockZymaticProgram(r, profile); err == nil {
		t.Fatal("unmapped route compiled")
	}
}
