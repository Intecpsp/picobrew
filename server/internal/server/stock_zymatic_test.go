package server

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"reflect"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/ai"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
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
	if got := rawGet("SyncUSer" + params); got != wire || !strings.Contains(wire, "/Mash,153,60,1,0/Boil,203,60,0,0/|#") {
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

func TestStockManualPathsVerifyMeasuredHopTiming(t *testing.T) {
	s, h, c := newTestServer(t)
	token := signIn(t, c, h.URL)
	r := validRecipe()
	r.ID, r.Revision = "hop-recipe", 1
	r.Steps[0].Location, r.Steps[1].Location = 0, 0
	r.Steps[1].TargetC = 95
	r.Ingredients = append(r.Ingredients, domain.Ingredient{Name: "Cascade", Kind: "hop", Unit: "g", Amount: 10, Use: "boil", Minutes: 30})
	if _, err := s.db.Put("recipe", r.ID, r, 0); err != nil {
		t.Fatal(err)
	}
	owner := h.URL + "/api/v1/stock-zymatics/TEST/recipes"
	submit := stockSubmission{Recipes: []stockReference{{RecipeID: r.ID, Revision: r.Revision}}}
	code, body := request(t, c, "POST", owner, token, submit, nil)
	var preview struct{ Items []stockItem }
	if code != 200 || json.Unmarshal(body, &preview) != nil || len(preview.Items) != 1 ||
		preview.Items[0].BatchLiters <= 0 || preview.Items[0].HopSlotsByMinute[30] != 1 ||
		!strings.Contains(string(body), "Hop slot 1") {
		t.Fatal(code, string(body))
	}
	submit.Recipes[0].Locations = []int{2, 1}
	for _, publish := range []bool{false, true} {
		submit.Publish = publish
		if code, b := request(t, c, "POST", owner, token, submit, nil); code != 422 || !strings.Contains(string(b), "Cascade") {
			t.Fatal(code, string(b))
		}
	}
}

func TestEquivalentStockManualPathsKeepSourceDrainConversion(t *testing.T) {
	s, h, c := newTestServer(t)
	token := signIn(t, c, h.URL)
	r := validRecipe()
	r.ID = "small-brewer"
	r.Revision = 1
	r.SourceModel = "pico-s"
	r.Steps[0].Location = 2
	r.Steps[0].DrainMinutes = 2
	r.Steps[1].Location = 1
	r.Steps[1].TargetC = 95
	if _, err := s.db.Put("recipe", r.ID, r, 0); err != nil {
		t.Fatal(err)
	}
	submit := stockSubmission{Recipes: []stockReference{{RecipeID: r.ID, Revision: r.Revision}}}
	fetch := func() domain.MachineRecipe {
		t.Helper()
		code, body := request(t, c, "POST", h.URL+"/api/v1/stock-zymatics/TEST/recipes", token, submit, nil)
		var response struct {
			Items []stockItem `json:"items"`
		}
		if code != 200 || json.Unmarshal(body, &response) != nil || len(response.Items) != 1 {
			t.Fatal(code, string(body))
		}
		return response.Items[0].Program
	}
	auto := fetch()
	submit.Recipes[0].Locations = []int{2, 1}
	manual := fetch()
	if auto.Steps[0].DrainMinutes != 8 || !reflect.DeepEqual(auto, manual) {
		t.Fatal(auto, manual)
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

func TestZymaticDoesNotServeAnUnsafeSavedProgram(t *testing.T) {
	s, h, client := newTestServer(t)
	user, sid := strings.Repeat("a", 32), strings.Repeat("b", 32)
	valid := stockItem{RecipeID: "valid", Program: domain.MachineRecipe{
		ID: strings.Repeat("c", 32), Name: "Valid", Steps: []domain.MachineStep{{Name: "Mash", TargetF: 152, Location: 1, Minutes: 60}},
	}}
	bad := stockItem{RecipeID: "invalid", Program: domain.MachineRecipe{
		ID: strings.Repeat("d", 32), Name: "Unsafe", Steps: []domain.MachineStep{{Name: "Pause", Location: 6}},
	}}
	clean := bad
	clean.Maintenance = true
	b := stockBrewer{
		Machine: "TEST", User: user, Items: []stockItem{valid, bad, clean},
		Served: []stockItem{valid, bad}, ServedCleaning: []stockItem{clean},
		Session: stockSession{ID: sid, Program: bad.Program, Snapshot: "0/1/2/3/4/5/6/1"},
	}
	if _, err := s.db.Put(stockKind, "TEST", b, 0); err != nil {
		t.Fatal(err)
	}
	get := func(path string, status int) string {
		t.Helper()
		resp, err := client.Get(h.URL + "/API/" + path)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		body, err := io.ReadAll(resp.Body)
		if err != nil || resp.StatusCode != status {
			t.Fatalf("%s: %d %s %v", path, resp.StatusCode, body, err)
		}
		return strings.TrimSpace(string(body))
	}
	for _, version := range []string{"1.1.14", "1.0.0", "1.2.0"} {
		for _, account := range []string{user, cleaningUser} {
			get("logSession?machine=TEST&user="+account+"&code=0&recipe="+bad.Program.ID+"&firm="+version, 404)
		}
		if got := get("RecoverSession?session="+sid+"&code=1&firm="+version, 200); got != "#!#" {
			t.Fatal("returned an unsafe reset point", got)
		}
		get("RecoverSession?session="+sid+"&code=0&firm="+version, 404)
	}
	menu := get("SyncUser?machine=TEST&user="+user, 200)
	if strings.Contains(menu, bad.Program.ID) || !strings.Contains(menu, valid.Program.ID) {
		t.Fatal("incorrect old menu filtering", menu)
	}
	if got := get("SyncUser?machine=TEST&user="+cleaningUser, 200); got != "##" {
		t.Fatal("unsafe maintenance menu entry", got)
	}
	session := strings.Trim(get("logSession?machine=TEST&user="+user+"&code=0&recipe="+valid.Program.ID, 200), "#")
	get("LogSession?code=3&session="+session, 200)
	get("RecoverSession?code=0&session="+session, 404)
}

func TestStockOfflinePreviewUsesSameCompilerWithoutMachineState(t *testing.T) {
	s, h, c := newTestServer(t)
	base := h.URL + "/api/v1/stock-zymatics"
	preview := base + "/preview"
	source := validRecipe()
	source.ID, source.SourceModel, source.Revision = "offline-library", "zymatic", 1
	source.Steps[0].Location = 2
	source.Steps[1].TargetC = 95
	if _, err := s.db.Put("recipe", source.ID, source, 0); err != nil {
		t.Fatal(err)
	}
	ref := stockReference{RecipeID: source.ID, Revision: 1}
	payload := map[string]any{"recipes": []stockReference{ref}}
	if code, _ := request(t, c, "POST", preview, "", payload, nil); code != 401 {
		t.Fatalf("unauthed preview %d", code)
	}
	token := signIn(t, c, h.URL)
	want := stockSubmission{Recipes: []stockReference{ref}, Publish: false}
	code, menuJSON := request(t, c, "POST", base+"/ABC123/recipes", token, want, nil)
	if code != 200 {
		t.Fatal(code, string(menuJSON))
	}
	code, offlineJSON := request(t, c, "POST", preview, token, payload, nil)
	if code != 200 {
		t.Fatal(code, string(offlineJSON))
	}
	var menu, offline struct {
		Items []stockItem `json:"items"`
	}
	if json.Unmarshal(menuJSON, &menu) != nil || json.Unmarshal(offlineJSON, &offline) != nil || len(offline.Items) != 1 || !reflect.DeepEqual(menu.Items, offline.Items) {
		t.Fatalf("offline program differs from menu: %s", offlineJSON)
	}
	if err := recipe.ValidateStockZymaticProgram(offline.Items[0].Program); err != nil {
		t.Fatal(err)
	}
	if code, _ := request(t, c, "POST", preview, token, stockSubmission{Recipes: []stockReference{ref}, Publish: true}, nil); code != 400 {
		t.Fatalf("accepted publish in offline mode: %d", code)
	}
	ref.Revision = 2
	if code, _ := request(t, c, "POST", preview, token, map[string]any{"recipes": []stockReference{ref}}, nil); code != 409 {
		t.Fatalf("accepted stale recipe: %d", code)
	}
	rows, err := s.db.List(stockKind)
	if err != nil || len(rows) != 0 {
		t.Fatalf("offline preview modified store: %d / %v", len(rows), err)
	}
}
