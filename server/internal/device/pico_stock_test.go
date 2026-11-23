package device

import (
	"bytes"
	"context"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/stockfirmware"
)

func TestStockRestoreTransitionsAndAcknowledgments(t *testing.T) {
	for _, fault := range []string{"", "identity", "capability", "worker_hash", "app_chunk", "recovery_readback", "commit_lost", "reboot_lost", "persist_before_commit", "resume_commit", "resume_reboot", "resume_slot", "unknown_stage"} {
		t.Run(fault, func(t *testing.T) {
			uid, model, key := "test-brewer", "pico-s", strings.Repeat("a", 64)
			app := otaIdentity{UID: uid, Model: model, Digest: strings.Repeat("a", 64)}
			images := stockfirmware.RestoreImages{Worker: bytes.Repeat([]byte("w"), 1100), Application: bytes.Repeat([]byte("a"), 1900), Recovery: bytes.Repeat([]byte("r"), 1300)}
			phase, previous := "normal", ""
			state := stockState{OK: true, UID: uid}
			if fault == "resume_commit" || fault == "resume_reboot" {
				phase = "worker"
				previous = "committing"
				state.App = true
				state.Recovery = true
				state.Committed = true
				if fault == "resume_reboot" {
					previous = "rebooting"
				}
			}
			if fault == "resume_slot" {
				phase = "worker"
				previous = "recovery"
				state.App = true
			}
			if fault == "unknown_stage" {
				previous = "something_else"
			}
			transferred := map[string][]byte{}
			target := ""
			mutations := []string{}
			stages := []string{}
			h := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.Header.Get("Authorization") != "Bearer "+key {
					t.Error("missing authentication")
					w.WriteHeader(401)
					return
				}
				op := strings.TrimPrefix(r.URL.Path, "/api/v1")
				reply := func(v any) { _ = json.NewEncoder(w).Encode(v) }
				var body map[string]any
				if r.Method == "POST" {
					_ = json.NewDecoder(r.Body).Decode(&body)
					mutations = append(mutations, op)
				}
				if phase == "normal" {
					switch op {
					case "/ota":
						reply(otaState{otaIdentity: app, Size: 256, OK: true})
					case "":
						reply(map[string]any{"model": model, "hub_ota_v1": true, "stock_restore_ota_v1": fault != "capability"})
					case "/ota/stock/prepare":
						if body["sha256"] != app.Digest || body["device_uid"] != uid {
							t.Error("preparation not bound to old image and UID")
						}
						reply(otaState{otaIdentity: app, Size: 256, OK: true})
						phase = "recovery"
					default:
						w.WriteHeader(404)
					}
					return
				}
				if phase == "recovery" {
					ack := recoveryAck{OK: true, ID: 19}
					switch op {
					case "/recovery/identity":
						reply(map[string]any{"device_uid": uid, "hub_ota_protocol": 1})
						return
					case "/recovery":
						reply(map[string]any{"profile": "recovery", "version": "0.1.0", "boot": map[string]bool{"valid": true, "recovery_selected": true}, "slots": []map[string]any{{"slot": 3, "present": true, "allocated": 49152}}})
						return
					case "/recovery/plan":
						if body["slot"] != float64(3) || body["web"] != false || body["sha256"] != firmware.Digest(images.Worker) {
							t.Fatalf("unsafe worker plan %+v", body)
						}
					case "/recovery/verify":
						w.WriteHeader(409)
						reply(map[string]string{"error": "checksum"})
						return
					case "/recovery/begin":
					case "/recovery/chunk":
						b, _ := hex.DecodeString(body["data"].(string))
						transferred["worker"] = append(transferred["worker"], b...)
						ack.Offset = len(transferred["worker"])
					case "/recovery/finish":
						ack.Mask = 1
						if fault == "worker_hash" {
							ack.Mask = 0
						}
					case "/recovery/select":
						if body["trial"] != false || !bytes.Equal(transferred["worker"], images.Worker) {
							t.Error("unverified worker selected")
						}
						ack.Mask = 1
						ack.Boot = true
						phase = "worker"
					default:
						w.WriteHeader(404)
						return
					}
					reply(ack)
					return
				}
				if op == "/stock/status" {
					value := state
					// Reach the worker first, then change identity before the
					// first write, exercising the post-handoff live UID check.
					if fault == "identity" && target == "first_status_seen" {
						value.UID = "another"
					}
					if fault == "identity" {
						target = "first_status_seen"
					}
					reply(value)
					return
				}
				if body["device_uid"] != uid {
					t.Error("unbound stock write")
				}
				ack := 0
				switch op {
				case "/stock/begin":
					target = body["target"].(string)
					if target == "recovery" && !state.App {
						t.Error("recovery before verified app")
					}
				case "/stock/chunk":
					b, _ := hex.DecodeString(body["data"].(string))
					if len(b) > 1024 || body["offset"] != float64(len(transferred[target])) {
						t.Error("unexpected chunk")
					}
					transferred[target] = append(transferred[target], b...)
					ack = len(transferred[target])
					if fault == "app_chunk" {
						ack++
					}
				case "/stock/finish":
					ack = len(transferred[target])
					if target == "application" {
						state.App = bytes.Equal(transferred[target], images.Application)
					} else {
						state.Recovery = fault != "recovery_readback" && bytes.Equal(transferred[target], images.Recovery)
					}
				case "/stock/commit":
					if !state.App || !state.Recovery {
						t.Error("unverified commit")
					}
					state.Committed = true
					if fault == "commit_lost" {
						w.WriteHeader(503)
						return
					}
				case "/stock/reboot":
					if !state.Committed {
						t.Error("unverified reboot")
					}
					if fault == "reboot_lost" {
						w.WriteHeader(503)
						return
					}
				default:
					w.WriteHeader(404)
					return
				}
				reply(map[string]any{"ok": true, "next_offset": ack})
			}))
			defer h.Close()
			p, err := newPico(h.URL, key, true)
			if err != nil {
				t.Fatal(err)
			}
			err = p.RestoreStock(context.Background(), uid, model, images, previous, func(stage string) error {
				stages = append(stages, stage)
				if stage == "committing" && fault == "persist_before_commit" {
					return io.ErrUnexpectedEOF
				}
				return nil
			})
			success := fault == "" || strings.HasPrefix(fault, "resume")
			if (err == nil) != success {
				t.Fatal(err, mutations, stages)
			}
			contains := func(list []string, x string) bool {
				for _, item := range list {
					if item == x {
						return true
					}
				}
				return false
			}
			if success && !contains(mutations, "/stock/reboot") {
				t.Fatal("missing reboot")
			}
			if !success && fault != "reboot_lost" && contains(mutations, "/stock/reboot") {
				t.Fatal("reboot after failure")
			}
			if fault == "unknown_stage" || fault == "capability" {
				if len(mutations) != 0 {
					t.Fatal(mutations)
				}
			}
			if strings.HasPrefix(fault, "resume") && contains(mutations, "/ota/stock/prepare") {
				t.Fatal("restarted an interrupted restoration")
			}
			if fault == "resume_reboot" && !reflect.DeepEqual(mutations, []string{"/stock/reboot"}) {
				t.Fatal(mutations)
			}
			if fault == "identity" && contains(mutations, "/stock/begin") {
				t.Fatal("wrong worker identity accepted")
			}
			if fault == "" && !reflect.DeepEqual(stages, []string{"preparing", "staging_worker", "selecting_worker", "application", "recovery", "committing", "rebooting"}) {
				t.Fatal(stages)
			}
		})
	}
}
