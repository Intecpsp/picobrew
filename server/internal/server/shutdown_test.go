package server

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/cookiejar"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

func TestHTTPShutdownEndsEventsAndDrainsAcceptedWrite(t *testing.T) {
	s, err := New(Config{DataDir: newTestDataDir(t), Password: ownerPassword, Logger: slog.New(slog.NewTextHandler(io.Discard, nil))})
	if err != nil {
		t.Fatal(err)
	}
	entered, release := make(chan struct{}), make(chan struct{})
	var releaseOnce sync.Once
	h := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodPost && r.URL.Path == "/api/v1/recipes" {
			close(entered)
			select {
			case <-release:
			case <-r.Context().Done():
				http.Error(w, "accepted request was cancelled", http.StatusServiceUnavailable)
				return
			}
		}
		s.ServeHTTP(w, r)
	}))
	h.Config.RegisterOnShutdown(s.BeginShutdown)
	h.Start()
	t.Cleanup(func() {
		releaseOnce.Do(func() { close(release) })
		h.Close()
		if err := s.Close(); err != nil {
			t.Error(err)
		}
	})
	jar, _ := cookiejar.New(nil)
	client := &http.Client{Jar: jar, Timeout: 10 * time.Second}
	csrf := signIn(t, client, h.URL)

	events, err := client.Get(h.URL + "/api/v1/events")
	if err != nil {
		t.Fatal(err)
	}
	defer events.Body.Close()
	reader := bufio.NewReader(events.Body)
	if line, err := reader.ReadString('\n'); err != nil || line != "event: fleet\n" {
		t.Fatalf("live event stream did not start: %q (%v)", line, err)
	}
	eventsEnded := make(chan error, 1)
	go func() { _, err := io.Copy(io.Discard, reader); eventsEnded <- err }()

	body, err := json.Marshal(validRecipe())
	if err != nil {
		t.Fatal(err)
	}
	req, err := http.NewRequest(http.MethodPost, h.URL+"/api/v1/recipes", bytes.NewReader(body))
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-CSRF-Token", csrf)
	type writeResult struct {
		status int
		body   []byte
		err    error
	}
	written := make(chan writeResult, 1)
	go func() {
		response, err := client.Do(req)
		if err != nil {
			written <- writeResult{err: err}
			return
		}
		defer response.Body.Close()
		body, err := io.ReadAll(response.Body)
		written <- writeResult{status: response.StatusCode, body: body, err: err}
	}()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	select {
	case <-entered:
	case <-ctx.Done():
		t.Fatal("recipe request was not accepted")
	}
	stopped := make(chan error, 1)
	go func() { stopped <- h.Config.Shutdown(ctx) }()
	select {
	case err := <-eventsEnded:
		if err != nil {
			t.Fatalf("event stream was aborted instead of drained: %v", err)
		}
	case <-ctx.Done():
		t.Fatal("open event stream held HTTP shutdown until its deadline")
	}
	select {
	case err := <-stopped:
		t.Fatalf("HTTP shutdown returned before the accepted write finished: %v", err)
	default:
	}
	releaseOnce.Do(func() { close(release) })
	var result writeResult
	select {
	case result = <-written:
	case <-ctx.Done():
		t.Fatal("accepted write did not finish during shutdown")
	}
	if result.err != nil || result.status != http.StatusCreated {
		t.Fatalf("accepted write failed during shutdown: %d %s (%v)", result.status, result.body, result.err)
	}
	select {
	case err := <-stopped:
		if err != nil {
			t.Fatalf("HTTP shutdown did not drain: %v", err)
		}
	case <-ctx.Done():
		t.Fatal("HTTP shutdown did not finish after draining requests")
	}
	var created, saved domain.Recipe
	if err := json.Unmarshal(result.body, &created); err != nil {
		t.Fatal(err)
	}
	if _, err := s.db.Get("recipe", created.ID, &saved); err != nil || saved.Name != validRecipe().Name {
		t.Fatalf("last accepted write was not retained before store close: %+v (%v)", saved, err)
	}
}
