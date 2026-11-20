package server

import (
	"errors"
	"math"
	"net/http"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

type profileCalibration struct {
	BatchLiters float64 `json:"batch_liters"`
	Efficiency  float64 `json:"efficiency"`
	MaxGrainKG  float64 `json:"max_grain_kg"`
}

func (s *Server) calibratedProfiles() ([]domain.Profile, error) {
	profiles := recipe.Profiles()
	for i := range profiles {
		var calibration profileCalibration
		if _, err := s.db.Get("profile", profiles[i].ID, &calibration); err == nil {
			profiles[i].BatchLiters = calibration.BatchLiters
			profiles[i].Efficiency = calibration.Efficiency
			profiles[i].MaxGrainKG = calibration.MaxGrainKG
			profiles[i].UserConfigured = true
			profiles[i].Notes = "Batch size, efficiency and grain capacity use your saved planning calibration. Source links describe published starting points, not your saved values. Verify the physical compartment mapping before deployment."
			if calibration.MaxGrainKG == 0 {
				profiles[i].Notes += " Grain capacity remains unverified."
			}
			if !profiles[i].ControlSupported {
				profiles[i].Notes += " Planning and export only: no live control adapter is implemented for this model."
			}
			if profiles[i].ID == "z" {
				profiles[i].Notes += " Hardware identity is generic z-series; Z1/Z2 mapping is unclaimed. Canonical programs require direct valve locations 1–6. Archived native Z/Zymatic routing and pause codes require explicit conversion before deployment."
			}
		} else if !errors.Is(err, store.ErrNotFound) {
			return nil, err
		}
	}
	return profiles, nil
}
func (s *Server) profiles(w http.ResponseWriter, r *http.Request) {
	values, err := s.calibratedProfiles()
	if err != nil {
		s.internal(w, err)
		return
	}
	s.json(w, 200, values)
}
func (s *Server) selectProfile(id string) (domain.Profile, bool) {
	// Hardware family identity stays z-series; the existing z profile is a
	// generic planning baseline, not a claim about Z1/Z2 hardware.
	if id == "z-series" {
		id = "z"
	}
	values, err := s.calibratedProfiles()
	if err != nil {
		s.cfg.Logger.Error("Could not load planning profiles", "error", err)
		return domain.Profile{}, false
	}
	for _, p := range values {
		if p.ID == id {
			return p, true
		}
	}
	return domain.Profile{}, false
}

func profileMatchesModel(profileID, model string) bool {
	return profileID == model || model == "z-series" && profileID == "z"
}
func (s *Server) saveProfile(w http.ResponseWriter, r *http.Request) {
	id := chi.URLParam(r, "id")
	var base domain.Profile
	for _, p := range recipe.Profiles() {
		if p.ID == id {
			base = p
			break
		}
	}
	if base.ID == "" {
		s.fail(w, 404, "profile", "Unknown machine profile")
		return
	}
	if r.Method == http.MethodDelete {
		if err := s.db.Delete("profile", id); err != nil {
			s.internal(w, err)
			return
		}
		s.json(w, 200, base)
		return
	}
	var in profileCalibration
	if !s.decode(w, r, &in) {
		return
	}
	valid := func(v float64) bool { return !math.IsNaN(v) && !math.IsInf(v, 0) }
	if !valid(in.BatchLiters) || in.BatchLiters <= 0 || in.BatchLiters > 50 || !valid(in.Efficiency) || in.Efficiency <= 0 || in.Efficiency > 100 || !valid(in.MaxGrainKG) || in.MaxGrainKG < 0 || in.MaxGrainKG > 25 {
		s.fail(w, 422, "calibration", "Use a batch volume above 0 and at most 50 L, efficiency above 0 and at most 100%, and grain capacity from 0 to 25 kg (0 means unverified)")
		return
	}
	if _, err := s.db.Put("profile", id, in, -1); err != nil {
		s.internal(w, err)
		return
	}
	profile, _ := s.selectProfile(id)
	s.json(w, 200, profile)
}
