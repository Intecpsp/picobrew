package device

import (
	"context"
	"net/http"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

// EventReader is implemented by adapters with an on-demand firmware event log.
type EventReader interface {
	Events(context.Context) (domain.DeviceEvents, error)
}

func (p *pico) Events(ctx context.Context) (domain.DeviceEvents, error) {
	var result domain.DeviceEvents
	if err := p.lock(ctx); err != nil {
		return result, err
	}
	defer p.unlock()
	if p.capabilities == nil {
		if _, err := p.discover(ctx); err != nil {
			return result, err
		}
	}
	if p.capabilities.EventsPersistent == nil || *p.capabilities.EventsPersistent {
		return result, unsupportedCapability("a volatile event log")
	}
	if err := p.request(ctx, http.MethodGet, "/api/v1/events", nil, &result, http.StatusOK); err != nil {
		return domain.DeviceEvents{}, err
	}
	// Both firmware implementations keep the last eight events in sequence
	// order, skip sequence zero on wrap, and reset numbering on reboot.
	if !validEvents(result) {
		return domain.DeviceEvents{}, &Error{Code: "invalid_response", Message: "The device returned an invalid event log"}
	}
	for i := range result.Events {
		event := &result.Events[i]
		event.Kind = p.cleanMessage(event.Kind)
		event.Detail = p.cleanMessage(event.Detail)
	}
	return result, nil
}

func validEvents(log domain.DeviceEvents) bool {
	if !log.Volatile || log.Capacity != 8 || log.NextSequence == 0 || log.Events == nil || len(log.Events) > log.Capacity {
		return false
	}
	var next uint32
	for i, event := range log.Events {
		if event.Sequence == 0 || event.UptimeMS == nil || i > 0 && event.Sequence != next || event.Kind == "" || len(event.Kind) > 19 || len(event.Detail) > 63 {
			return false
		}
		next = event.Sequence + 1
		if next == 0 {
			next = 1
		}
	}
	return len(log.Events) == 0 && log.NextSequence == 1 || next == log.NextSequence
}
