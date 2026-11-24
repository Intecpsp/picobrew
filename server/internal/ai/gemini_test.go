package ai

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"slices"
	"strings"
	"testing"
)

func TestGeminiListsGenerationModelsAcrossPages(t *testing.T) {
	const token = "opaque/next=+&"
	calls := 0
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		if r.Method != http.MethodGet || r.URL.Path != "/v1beta/models" || r.Header.Get("x-goog-api-key") != "secret" ||
			r.Header.Get("Authorization") != "" || r.URL.Query().Has("key") || r.URL.Query().Get("pageSize") != "1000" {
			t.Errorf("unexpected Gemini discovery request")
		}
		switch calls {
		case 1:
			if r.URL.Query().Has("pageToken") {
				t.Error("initial page already has token")
			}
			io.WriteString(w, `{"models":[{"name":"models/embedding","supportedGenerationMethods":["embedContent"]},{"name":"models/flash.2","displayName":"Zeta","supportedGenerationMethods":["generateContent"]}],"nextPageToken":"opaque/next=+&"}`)
		case 2:
			if r.URL.Query().Get("pageToken") != token {
				t.Error("page token changed")
			}
			io.WriteString(w, `{"models":[{"name":"models/gemma-new","displayName":"Alpha","supportedGenerationMethods":["countTokens","generateContent"]},{"name":"models/flash.2","supportedGenerationMethods":["generateContent"]}]}`)
		default:
			t.Error("unexpected extra page")
		}
	}))
	defer api.Close()
	c := New()
	c.GeminiModelsURL = api.URL + "/v1beta/models"
	got, err := c.ListModels(t.Context(), "gemini", "secret")
	if err != nil || calls != 2 || !slices.Equal(got, []Model{{ID: "gemma-new", Name: "Alpha"}, {ID: "flash.2", Name: "Zeta"}}) {
		t.Fatalf("models=%v; calls=%d; err=%v", got, calls, err)
	}
}

func TestGeminiAssistantUsesNativeTextAndRecipeRequests(t *testing.T) {
	const draft = `{"text":"Review this draft","recipe":{"name":"Brown ale","batch_liters":9.4635}}`
	for _, mode := range []string{"recipe", "live"} {
		t.Run(mode, func(t *testing.T) {
			api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.Method != http.MethodPost || r.URL.Path != "/v1beta/models/gemini-2.5-test:generateContent" ||
					r.URL.RawQuery != "" || r.Header.Get("x-goog-api-key") != "sample-secret" ||
					r.Header.Get("Authorization") != "" || r.Header.Get("Content-Type") != "application/json" {
					t.Error("unexpected Gemini generation request")
				}
				var req struct {
					System   struct{ Parts []struct{ Text string } } `json:"systemInstruction"`
					Contents []struct {
						Role  string
						Parts []struct{ Text string }
					}
					Config struct {
						ResponseMimeType string
						MaxOutputTokens  int
					} `json:"generationConfig"`
				}
				body, _ := io.ReadAll(r.Body)
				if strings.Contains(string(body), "sample-secret") || json.Unmarshal(body, &req) != nil ||
					len(req.System.Parts) != 1 || req.System.Parts[0].Text != systemPrompt ||
					len(req.Contents) != 1 || req.Contents[0].Role != "user" || len(req.Contents[0].Parts) != 1 ||
					!strings.Contains(req.Contents[0].Parts[0].Text, "my recipe") || !strings.Contains(req.Contents[0].Parts[0].Text, "session-marker") ||
					req.Config.MaxOutputTokens == 0 || (req.Config.ResponseMimeType == "application/json") != (mode == "recipe") {
					t.Error("missing system, prompt, recipe or context separation")
				}
				text := "Brew summary."
				if mode == "recipe" {
					text = draft
				}
				// A thought summary must never become a recipe fragment or visible advice.
				parts := []any{map[string]any{"thought": true, "text": "private reasoning"}, map[string]any{"text": text[:5]}, map[string]any{"text": text[5:]}}
				json.NewEncoder(w).Encode(map[string]any{"candidates": []any{map[string]any{"finishReason": "STOP", "content": map[string]any{"parts": parts}}}})
			}))
			defer api.Close()
			c := New()
			c.GeminiModelsURL = api.URL + "/v1beta/models"
			result, err := c.Assist(t.Context(), Config{Enabled: true, Provider: "gemini", Model: "gemini-2.5-test"}, "sample-secret", Request{Mode: mode, Prompt: "my recipe"}, "session-marker")
			if err != nil {
				t.Fatal(err)
			}
			if mode == "recipe" && (result.Text != "Review this draft" || !strings.Contains(string(result.Recipe), "Brown ale")) ||
				mode == "live" && (result.Text != "Brew summary." || result.Recipe != nil) || len(result.Limitations) == 0 {
				t.Fatalf("wrong result %+v", result)
			}
		})
	}
}

func TestGeminiRejectsIncompleteRepliesAndUnsafeIdentifiers(t *testing.T) {
	for _, body := range []string{
		`{"promptFeedback":{"blockReason":"SAFETY"}}`, `{"candidates":[{"finishReason":"MAX_TOKENS","content":{"parts":[{"text":"partial"}]}}]}`,
		`{"candidates":[{"finishReason":"SAFETY","content":{"parts":[{"text":"secret reason"}]}}]}`, `{"error":"private response"}`, "<html>",
	} {
		if _, err := geminiText([]byte(body)); err == nil || strings.Contains(err.Error(), "private") || strings.Contains(err.Error(), "secret") {
			t.Fatal("invalid response accepted or error contents disclosed")
		}
	}
	c := New()
	c.HTTP = &http.Client{Transport: modelRoundTrip(func(*http.Request) (*http.Response, error) {
		t.Fatal("invalid model contacted a remote host")
		return nil, nil
	})}
	for _, id := range []string{"../keys", "models/valid", "https://other.host", "model?key=sample", "name%2fother", "x:other", "."} {
		if _, err := c.Assist(t.Context(), Config{Provider: "gemini", Enabled: true, Model: id}, "key", Request{Mode: "live"}, nil); err == nil {
			t.Errorf("accepted model %q", id)
		}
	}
}

func TestGeminiRedirectsCannotForwardCredentials(t *testing.T) {
	other := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) { t.Error("followed credentialed redirect") }))
	defer other.Close()
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, other.URL, http.StatusTemporaryRedirect)
	}))
	defer api.Close()
	c := New()
	c.HTTP = api.Client()
	c.GeminiModelsURL = api.URL
	if _, err := c.ListModels(t.Context(), "gemini", "sample-key"); err == nil {
		t.Fatal("accepted list redirect")
	}
	if _, err := c.Assist(t.Context(), Config{Provider: "gemini", Enabled: true, Model: "some-model"}, "sample-key", Request{Mode: "live"}, nil); err == nil {
		t.Fatal("accepted generation redirect")
	}
}

func TestGeminiMalformedModelCatalogFailsWithoutPartialResults(t *testing.T) {
	for _, page := range []string{
		`{"models":[{"name":"models/../../bad","supportedGenerationMethods":["generateContent"]}]}`,
		`{"models":[{"name":"models/good","supportedGenerationMethods":["generateContent"]}],"nextPageToken":"loop"}`,
		`{"models":[{"name":"models/good"}],"nextPageToken":"new\nline"}`,
		`{"models":null}`,
	} {
		c := New()
		calls := 0
		c.HTTP = &http.Client{Transport: modelRoundTrip(func(*http.Request) (*http.Response, error) {
			calls++
			return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(page))}, nil
		})}
		if got, err := c.ListModels(t.Context(), "gemini", "sample"); err == nil || got != nil || calls > 2 {
			t.Fatalf("page accepted; models=%v err=%v calls=%d", got, err, calls)
		}
	}
}
