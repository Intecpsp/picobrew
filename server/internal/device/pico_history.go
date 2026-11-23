package device

import (
	"context"
	"math"
	"net/http"
	"reflect"
	"regexp"
	"strconv"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

var additionalHistoryField = regexp.MustCompile(`^(sensor_[A-Za-z0-9_-]{1,31}|actuator_[A-Za-z0-9_-]{1,31}_(enabled|level|rpm))$`)

func (p *pico) validHistoryFields(fields []string) bool {
	if p.capabilities != nil && p.capabilities.Model == "zymatic" {
		return p.capabilities.ModelFamily == "zymatic" && p.capabilities.Supports("history_persistent") && domain.IsZymaticHistory(fields)
	}
	if len(fields) < len(historyFields) || len(fields) > 48 || !reflect.DeepEqual(fields[:len(historyFields)], historyFields) {
		return false
	}
	seen := map[string]bool{}
	for index, field := range fields {
		if seen[field] {
			return false
		}
		seen[field] = true
		if index >= len(historyFields) && field != "temperature_f" && field != "pressure_kpa" && field != "battery_v" && !additionalHistoryField.MatchString(field) {
			return false
		}
	}
	return true
}
func (p *pico) History(ctx context.Context) (domain.History, error) {
	var result domain.History
	if err := p.lock(ctx); err != nil {
		return result, err
	}
	defer p.unlock()
	err := p.request(ctx, http.MethodGet, "/api/v1/history", nil, &result, 200)
	if err == nil {
		if !p.validHistoryFields(result.Fields) || len(result.Sessions) > 8 {
			return result, &Error{Code: "invalid_response", Message: "Device history schema is unsupported"}
		}
		for _, session := range append([]domain.HistorySession{result.HistorySession}, result.Sessions...) {
			if domain.IsZymaticHistory(result.Fields) && (session.RecordCount < 0 || session.RecordCount > 222 || session.PersistedRecordCount < 0 || session.PersistedRecordCount > session.RecordCount) {
				return result, &Error{Code: "invalid_response", Message: "Zymatic history exceeds its record capacity"}
			}
			if session.SampleIntervalMS != 0 && (session.SampleIntervalMS < 15000 || session.SampleIntervalMS > 86400000) {
				return result, &Error{Code: "invalid_response", Message: "Device history sampling interval is invalid"}
			}
		}
		p.historyFields = append([]string(nil), result.Fields...)
		p.historyMetadata = result.FieldMetadata
	}
	return result, err
}
func (p *pico) HistoryPage(ctx context.Context, id uint64, offset int) ([]domain.Sample, *int, error) {
	page, err := p.HistoryPageWithSchema(ctx, id, offset)
	return page.Samples, page.Next, err
}
func (p *pico) HistoryPageWithSchema(ctx context.Context, id uint64, offset int) (domain.HistoryPage, error) {
	empty := domain.HistoryPage{}
	if id == 0 || id > math.MaxUint32 || offset < 0 || offset > 4224 {
		return empty, invalid("History ID or offset is invalid")
	}
	if err := p.lock(ctx); err != nil {
		return empty, err
	}
	defer p.unlock()
	var reply struct {
		ID       uint64                          `json:"id"`
		Offset   int                             `json:"offset"`
		Samples  [][]*float64                    `json:"samples"`
		Next     *int                            `json:"next_offset"`
		Fields   []string                        `json:"fields"`
		Metadata map[string]domain.FieldMetadata `json:"field_metadata"`
	}
	path := "/api/v1/history/samples/" + strconv.Itoa(offset) + "?id=" + strconv.FormatUint(id, 10)
	if err := p.request(ctx, http.MethodGet, path, nil, &reply, 200); err != nil {
		return empty, err
	}
	if reply.ID != id || reply.Offset != offset || len(reply.Samples) > 32 || offset+len(reply.Samples) > 4224 || reply.Next != nil && (*reply.Next != offset+len(reply.Samples) || len(reply.Samples) == 0 || *reply.Next > 4224) {
		return empty, &Error{Code: "invalid_response", Message: "Device history page identity or continuation is invalid"}
	}
	fields, metadata := reply.Fields, reply.Metadata
	if fields == nil {
		fields = p.historyFields
		metadata = p.historyMetadata
	}
	if fields == nil {
		fields = historyFields
	}
	if !p.validHistoryFields(fields) {
		return empty, &Error{Code: "invalid_response", Message: "Device history page schema is unsupported"}
	}
	zymatic := domain.IsZymaticHistory(fields)
	if zymatic && (len(reply.Samples) > 3 || offset+len(reply.Samples) > 222) {
		return empty, &Error{Code: "invalid_response", Message: "Zymatic history page exceeds its record capacity"}
	}
	result := make([]domain.Sample, 0, len(reply.Samples))
	for _, row := range reply.Samples {
		if len(row) != len(fields) {
			return empty, &Error{Code: "invalid_response", Message: "Device history row has an invalid field count"}
		}
		if zymatic {
			sample, err := decodeZymaticSample(row)
			if err != nil {
				return empty, err
			}
			if len(result) > 0 && sample.ElapsedMS <= result[len(result)-1].ElapsedMS {
				return empty, &Error{Code: "invalid_response", Message: "Zymatic history page elapsed times do not advance"}
			}
			result = append(result, sample)
			continue
		}
		for _, index := range []int{0, 4, 5, 6, 7, 8, 9, 10} {
			low, high := float64(0), float64(255)
			if index == 0 {
				high = math.MaxUint32
			} else if index == 4 {
				low, high = math.MinInt32, math.MaxInt32
			}
			if row[index] == nil || math.IsNaN(*row[index]) || math.IsInf(*row[index], 0) || *row[index] != math.Trunc(*row[index]) || *row[index] < low || *row[index] > high {
				return empty, &Error{Code: "invalid_response", Message: "Device history row has invalid integer fields"}
			}
		}
		for index, value := range row {
			if value != nil && (math.IsNaN(*value) || math.IsInf(*value, 0) || (index >= 1 && index <= 3 && (*value < -3276.7 || *value > 3276.7))) {
				return empty, &Error{Code: "invalid_response", Message: "Device history row has an invalid reading"}
			}
		}
		if len(result) > 0 && int64(*row[0]) < result[len(result)-1].ElapsedMS {
			return empty, &Error{Code: "invalid_response", Message: "Device history page elapsed times are not ordered"}
		}
		sample := domain.Sample{ElapsedMS: int64(*row[0]), WortF: row[1], ThermoblockF: row[2], TargetF: row[3], StepSeconds: int(*row[4]), Phase: int(*row[5]), ControllerStatus: int(*row[6]), Compartment: int(*row[7]), CurrentStep: int(*row[8]), Flags: int(*row[9]), DeviceState: int(*row[10]), Source: "device"}
		for index := len(historyFields); index < len(fields); index++ {
			switch fields[index] {
			case "temperature_f":
				sample.TemperatureF = row[index]
			case "pressure_kpa":
				sample.PressureKPa = row[index]
			case "battery_v":
				sample.BatteryV = row[index]
			default:
				if sample.Readings == nil {
					sample.Readings = map[string]*float64{}
				}
				sample.Readings[fields[index]] = row[index]
			}
		}
		result = append(result, sample)
	}
	return domain.HistoryPage{Samples: result, Next: reply.Next, Fields: fields, FieldMetadata: metadata}, nil
}
