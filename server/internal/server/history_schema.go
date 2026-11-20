package server

import (
	"errors"
	"reflect"
	"strconv"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

// Retain the device's named channels and their physical units with the session,
// so later firmware/settings changes cannot relabel an archived measurement.
func (s *Server) saveHistorySchema(deviceID string, sessionID uint64, history domain.History) error {
	if len(history.Fields) <= 11 && len(history.FieldMetadata) == 0 {
		return nil
	}
	id := deviceID + "-" + strconv.FormatUint(sessionID, 10)
	for attempt := 0; attempt < 8; attempt++ {
		var brew domain.Brew
		version, err := s.db.Get("brew", id, &brew)
		if err != nil {
			return err
		}
		if reflect.DeepEqual(brew.SampleFields, history.Fields) && reflect.DeepEqual(brew.FieldMetadata, history.FieldMetadata) {
			return nil
		}
		brew.SampleFields = append([]string(nil), history.Fields...)
		brew.FieldMetadata = history.FieldMetadata
		if _, err = s.db.Put("brew", id, brew, version); errors.Is(err, store.ErrConflict) {
			continue
		}
		return err
	}
	return store.ErrConflict
}
func additionalSampleValue(sample domain.Sample, field string) *float64 {
	switch field {
	case "temperature_f":
		return sample.TemperatureF
	case "pressure_kpa":
		return sample.PressureKPa
	case "battery_v":
		return sample.BatteryV
	default:
		return sample.Readings[field]
	}
}
