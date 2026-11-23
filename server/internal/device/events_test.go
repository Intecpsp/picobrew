package device

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"strings"
	"testing"
)

func TestEventsRequireDiscoveryAndNeverProbeUnsupportedFirmware(t *testing.T) {
	for _, fixture := range []struct {
		name, marker string
		supported    bool
	}{
		{"CC3200 or ESP32", `,"events_persistent":false`, true},
		{"Zymatic without event discovery", "", false},
		{"unknown persistent log", `,"events_persistent":true`, false},
	} {
		t.Run(fixture.name, func(t *testing.T) {
			reads := 0
			p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
				if r.Method != http.MethodGet {
					t.Errorf("unexpected method %s", r.Method)
				}
				switch r.URL.Path {
				case "/api/v1":
					fmt.Fprintf(w, `{"api_version":1,"mode":"standalone"%s}`, fixture.marker)
				case "/api/v1/events":
					reads++
					fmt.Fprint(w, `{"events":[{"sequence":4294967295,"uptime_ms":4294967294,"kind":"command","detail":"clean"},{"sequence":1,"uptime_ms":12,"kind":"command","detail":"http://127.0.0.1"}],"capacity":8,"volatile":true,"next_sequence":2}`)
				default:
					t.Errorf("unexpected path %s", r.URL.Path)
					http.NotFound(w, r)
				}
			})
			got, err := p.Events(context.Background())
			if !fixture.supported {
				if deviceFailure(t, err).Code != "unsupported_capability" || reads != 0 {
					t.Fatal(err, reads)
				}
				return
			}
			if err != nil || reads != 1 || len(got.Events) != 2 || got.Events[0].Sequence != 4294967295 || got.Events[1].Sequence != 1 || strings.Contains(got.Events[1].Detail, "127.0.0.1") {
				t.Fatalf("response %+v, reads %d, err %v", got, reads, err)
			}
		})
	}
}

func TestFirmwareEventLogIsBoundedAndComplete(t *testing.T) {
	const event = `{"sequence":1,"uptime_ms":0,"kind":"command","detail":"clean"}`
	for _, c := range []struct {
		name, events, next string
		ok                 bool
	}{
		{"empty", "[]", "1", true},
		{"startup", "[" + event + "]", "2", true},
		{"duplicate", "[" + event + "," + event + "]", "2", false},
		{"missing timestamp", "[" + strings.Replace(event, `"uptime_ms":0,`, "", 1) + "]", "2", false},
		{"missing kind", "[" + strings.Replace(event, `"command"`, `""`, 1) + "]", "2", false},
		{"wrong next", "[" + event + "]", "3", false},
		{"missing array", "null", "1", false},
		{"too many", "[" + strings.Repeat(event+",", 8) + event + "]", "2", false},
		{"oversize detail", "[" + strings.Replace(event, "clean", strings.Repeat("x", 64), 1) + "]", "2", false},
	} {
		t.Run(c.name, func(t *testing.T) {
			p := testPico(t, func(w http.ResponseWriter, r *http.Request) {
				if r.URL.Path == "/api/v1" {
					io.WriteString(w, `{"api_version":1,"mode":"standalone","events_persistent":false}`)
					return
				}
				fmt.Fprintf(w, `{"events":%s,"next_sequence":%s,"capacity":8,"volatile":true}`, c.events, c.next)
			})
			log, err := p.Events(context.Background())
			if c.ok {
				if err != nil {
					t.Fatal(err)
				}
				return
			}
			if deviceFailure(t, err).Code != "invalid_response" || log.Events != nil {
				t.Fatalf("accepted partial reply: %+v %v", log, err)
			}
		})
	}
}
