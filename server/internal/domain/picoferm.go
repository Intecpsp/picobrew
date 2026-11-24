package domain

func (d Device) IsPicoFerm() bool {
	return d.Adapter == "pico-standalone" && d.Model == "picoferm"
}

func (d Device) IsBrewer() bool {
	if d.Adapter != "pico-standalone" {
		return false
	}
	switch d.Model {
	case "pico-s", "pico-pro", "pico-c", "z-series", "zymatic":
		return true
	}
	return false
}

// A manual registration can later identify itself as a different model.
func (d *Device) ClearInapplicableBrewer() {
	if d.BrewerID != "" && !d.IsPicoFerm() {
		d.BrewerID = ""
		d.BrewerRevision++
	}
}
