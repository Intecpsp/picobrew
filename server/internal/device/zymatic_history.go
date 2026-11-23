package device

import (
	"math"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func decodeZymaticSample(row []*float64) (domain.Sample, error) {
	errorReading := &Error{Code: "invalid_response", Message: "Zymatic history row has an invalid reading"}
	if row[0] == nil || *row[0] != math.Trunc(*row[0]) || *row[0] < 0 || *row[0] > math.MaxUint32 {
		return domain.Sample{}, errorReading
	}
	s := domain.Sample{ZymaticHistory: true, ElapsedMS: int64(*row[0]), WortF: row[1], ThermoblockF: row[2], TargetF: row[3], Compartment: -1, CurrentStep: -1, Source: "device"}
	for i, limit := range []float64{400, 400, 255, 6, 24} {
		n := row[i+1]
		if n == nil {
			continue
		}
		if math.IsNaN(*n) || *n < 0 || *n > limit || (i >= 2 && *n != math.Trunc(*n)) {
			return domain.Sample{}, errorReading
		}
	}
	if row[4] != nil {
		s.Compartment = int(*row[4])
	}
	if row[5] != nil {
		s.CurrentStep = int(*row[5])
	}
	return s, nil
}
