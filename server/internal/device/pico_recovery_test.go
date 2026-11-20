package device

import (
	"context"
	"encoding/json"
	"math"
	"net/http"
	"reflect"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestInterruptedRecoveryStatusIsOptionalAndBounded(t *testing.T) {
	for _, recovery := range []*domain.Recovery{nil, {Valid: true, CurrentStep: -1},
		{Available: true, Valid: true, RecipeName: "Saved ale", CurrentStep: -1, ElapsedSeconds: math.MaxUint32},
		{Available: true, Valid: true, RecipeName: "Saved ale", StepName: "Mash", CurrentStep: 2, StepSeconds: 1234},
		{Available: false, Valid: false, CurrentStep: -1}} {
		p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
			if r.Method != http.MethodGet || r.URL.Path != "/api/v1/status" {
				t.Fatalf("status read caused mutation: %s %s", r.Method, r.URL.Path)
			}
			replyJSON(w, 200, domain.Status{Ready: true, Recovery: recovery})
		})
		status, err := p.Status(context.Background())
		if err != nil || !reflect.DeepEqual(status.Recovery, recovery) {
			t.Fatalf("recovery metadata changed: %+v %v", status.Recovery, err)
		}
	}
	for _, recovery := range []domain.Recovery{{Available: true, Valid: true}, {CurrentStep: -2}, {CurrentStep: 256},
		{StepSeconds: math.MaxUint32}, {RecipeName: strings.Repeat("a", 32)}, {StepName: "bad\x00name"}} {
		p := testPico(t, func(w http.ResponseWriter, _ *http.Request) { replyJSON(w, 200, domain.Status{Recovery: &recovery}) })
		if _, err := p.Status(context.Background()); err == nil {
			t.Fatalf("accepted malformed recovery: %+v", recovery)
		}
	}
}

func TestInterruptedResumeRequiresConfirmationAndDiscardSendsNoExtraArguments(t *testing.T) {
	caps := domain.Capabilities{Commands: []string{"resume-interrupted", "discard-interrupted"}, CommandFields: map[string]domain.CommandFields{
		"resume-interrupted":  {Allowed: []string{"confirm"}, Required: []string{"confirm"}},
		"discard-interrupted": {Allowed: []string{}, Required: []string{}},
	}}
	var sent []map[string]any
	p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Fatal(err)
		}
		sent = append(sent, body)
		replyJSON(w, 202, map[string]any{"accepted": true, "command": body["command"]})
	})
	p.capabilities = &caps
	for _, command := range []domain.Command{{Command: "resume-interrupted"}, {Command: "resume-interrupted", Confirm: ptr(false)},
		{Command: "resume-interrupted", Confirm: ptr(true), RecipeID: "other"}, {Command: "discard-interrupted", Confirm: ptr(true)}} {
		if err := p.Command(context.Background(), command); err == nil {
			t.Fatalf("accepted %+v", command)
		}
	}
	if len(sent) != 0 {
		t.Fatal("unconfirmed or malformed command reached firmware")
	}
	for _, command := range []domain.Command{{Command: "resume-interrupted", Confirm: ptr(true)}, {Command: "discard-interrupted"}} {
		if err := p.Command(context.Background(), command); err != nil {
			t.Fatal(err)
		}
	}
	if !reflect.DeepEqual(sent, []map[string]any{{"command": "resume-interrupted", "confirm": true}, {"command": "discard-interrupted"}}) {
		t.Fatal("wire command changed", sent)
	}
}
