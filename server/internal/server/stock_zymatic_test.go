package server

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/ai"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestStockRecipeSyncAndRecovery(t *testing.T) {
	s, h, c := newTestServer(t)
	owner := h.URL + "/api/v1/stock-zymatics/ABC123XYZ999/recipes"
	submit := stockSubmission{Recipes: []stockReference{{RecipeID: "library", Revision: 1}}, Publish: true}
	if code, _ := request(t, c, "POST", owner, "", submit, nil); code != 401 {
		t.Fatalf("owner route returned %d", code)
	}
	token := signIn(t, c, h.URL)
	r := validRecipe()
	r.SourceModel = "zymatic"
	r.ID = "library"
	r.Revision = 1
	r.Steps[0].Location = 2
	r.Steps[1].TargetC = 95
	if _, err := s.db.Put("recipe", r.ID, r, 0); err != nil {
		t.Fatal(err)
	}
	rawGet := func(path string) string {
		// Exercise the bare-LF HTTP/1.0 request emitted by the AVR on a TCP socket.
		conn, err := net.DialTimeout("tcp", h.Listener.Addr().String(), c.Timeout)
		if err != nil {
			t.Fatal(err)
		}
		defer conn.Close()
		fmt.Fprintf(conn, "GET /API/%s HTTP/1.0\nHost: picobrew.com\nConnection: close\n\n", path)
		resp, err := http.ReadResponse(bufio.NewReader(conn), nil)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		body, err := io.ReadAll(resp.Body)
		if err != nil {
			t.Fatal(err)
		}
		return strings.TrimSpace(string(body))
	}
	const account = "usersetup?machine=ABC123XYZ999&admin=0"
	submit.Publish = false
	if code, b := request(t, c, "POST", owner, token, submit, nil); code != 200 {
		t.Fatalf("preview %d: %s", code, b)
	}
	if rawGet(account) != "##" {
		t.Fatal("preview published a menu")
	}
	submit.Publish = true
	if code, b := request(t, c, "POST", owner, token, submit, nil); code != 200 {
		t.Fatalf("publish %d: %s", code, b)
	}
	users := rawGet(account)
	user, ok := strings.CutSuffix(strings.TrimPrefix(users, "#"), "/PicoBrew Hub|#")
	if !ok || len(user) != 32 {
		t.Fatalf("invalid account: %q", users)
	}
	params := "?machine=ABC123XYZ999&user=" + user
	wire := rawGet("SyncUser" + params)
	if got := rawGet("SyncUSer" + params); got != wire || !strings.Contains(wire, "/Mash,153,60,1,0/Boil,203,60,1,0/|#") {
		t.Fatalf("Wi-Fi/Ethernet mismatch or bad record %q", wire)
	}
	if rawGet("SyncUser?machine=OTHER&user="+user) != "##" || rawGet("SyncUser?machine=ABC123XYZ999&user="+cleaningUser) != "##" {
		t.Fatal("cross-menu disclosure")
	}
	rid := strings.Split(wire, "/")[1]
	session := strings.Trim(rawGet("logSession"+params+"&recipe="+rid+"&code=0&firm=1.1.14"), "#")
	if len(session) != 32 {
		t.Fatal("invalid session", session)
	}
	snapshot := "0/1/2/3/4/5/6/1"
	rawGet("LogSession?code=2&session=" + session + "&step=" + snapshot)
	rawGet("LogSession?code=2&session=" + session + "&step=bad")
	if rawGet("recoversession?code=1&session="+session) != "#"+snapshot+"#" {
		t.Fatal("recovery snapshot changed")
	}
	if code, _ := request(t, c, "DELETE", owner+"/library", token, nil, nil); code != 204 {
		t.Fatal(code)
	}
	if rawGet("SyncUser"+params) != "##" || rawGet("recoversession?code=0&session="+session) != strings.TrimSuffix(wire, "#")+"!#" {
		t.Fatal("published removal or in-progress recipe recovery failed")
	}
	submit.Recipes[0].Revision = 2
	if code, _ := request(t, c, "POST", owner, token, submit, nil); code != 409 {
		t.Fatal("stale revision", code)
	}
}

func TestAssistantSourcesAreIndependent(t *testing.T) {
	s, _, _ := newTestServer(t)
	const id = "picopak-64"
	r := validRecipe()
	r.Name = "Personal recipe with the same ID"
	if _, err := s.db.Put("recipe", id, r, 0); err != nil {
		t.Fatal(err)
	}
	l, err := s.aiContext(ai.Request{Mode: "recipe", RecipeID: id})
	if err != nil || l["recipe"].(domain.Recipe).Name != r.Name {
		t.Fatal(err)
	}
	data, err := s.aiContext(ai.Request{Mode: "recipe", CatalogID: id})
	if err != nil {
		t.Fatal(err)
	}
	b, _ := json.Marshal(data)
	if !strings.Contains(string(b), "native_program") || strings.Contains(string(b), r.Name) {
		t.Fatal("catalog context did not resolve its own program")
	}
	for _, input := range []ai.Request{{Mode: "live", CatalogID: id}, {Mode: "recipe", CatalogID: id, RecipeID: id}, {Mode: "recipe", CatalogID: "missing-example"}} {
		if _, err := s.aiContext(input); err == nil {
			t.Fatal("accepted invalid context", input)
		}
	}
}
