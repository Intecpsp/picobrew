package server

import (
	"archive/zip"
	"bytes"
	"context"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"io"
	"log/slog"
	"net"
	"net/http"
	"net/http/cookiejar"
	"net/http/httptest"
	"net/url"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/ai"
)

func startSubscriptionTestHub(t *testing.T, dir string) (*Server, *httptest.Server, *http.Client, func()) {
	t.Helper()
	s, err := New(Config{DataDir: dir, Password: ownerPassword, PollInterval: time.Hour, Logger: slog.New(slog.NewTextHandler(io.Discard, nil))})
	if err != nil {
		t.Fatal(err)
	}
	h := httptest.NewServer(s)
	jar, _ := cookiejar.New(nil)
	client := &http.Client{Jar: jar, Timeout: 10 * time.Second}
	var once sync.Once
	stop := func() {
		once.Do(func() {
			h.Close()
			if err := s.Close(); err != nil {
				t.Error(err)
			}
		})
	}
	t.Cleanup(stop)
	return s, h, client, stop
}

func TestAISubscriptionAuthenticationAndCSRF(t *testing.T) {
	_, h, client := newTestServer(t)
	requests := []struct {
		method, path string
		body         any
	}{
		{"GET", "/ai/subscription?provider=openai", nil},
		{"DELETE", "/ai/subscription?provider=openai", nil},
		{"POST", "/ai/subscription/login", map[string]string{"provider": "openai"}},
		{"POST", "/ai/subscription/callback", map[string]string{"provider": "openai", "callback_url": chatGPTCallbackURL + "?code=x&state=y&client_id=z"}},
	}
	for _, req := range requests {
		status, _ := request(t, client, req.method, h.URL+"/api/v1"+req.path, "", req.body, nil)
		if status != 401 {
			t.Errorf("unauthenticated %s %s returned %d", req.method, req.path, status)
		}
	}
	csrf := signIn(t, client, h.URL)
	for _, req := range requests[1:] {
		status, _ := request(t, client, req.method, h.URL+"/api/v1"+req.path, "", req.body, nil)
		if status != 403 {
			t.Errorf("missing CSRF %s returned %d", req.path, status)
		}
		status, _ = request(t, client, req.method, h.URL+"/api/v1"+req.path, csrf, req.body, map[string]string{"Origin": "https://unrelated.example"})
		if status != 403 {
			t.Errorf("cross-origin %s returned %d", req.path, status)
		}
	}
	for _, body := range []map[string]string{
		{"provider": "anthropic"},
		{"provider": "openai", "redirect_uri": "https://unrelated.example/callback"},
		{"provider": "openai", "redirect_uri": chatGPTCallbackURL, "client_secret": "forbidden"},
	} {
		status, _ := request(t, client, "POST", h.URL+"/api/v1/ai/subscription/login", csrf, body, nil)
		if status != 400 {
			t.Errorf("invalid login accepted: %d", status)
		}
	}
	for _, callback := range []string{"https://unrelated.example/?code=x", chatGPTCallbackURL + "#code=fragment", "http://evil@127.0.0.1:1455/auth/callback?code=x"} {
		status, _ := request(t, client, "POST", h.URL+"/api/v1/ai/subscription/callback", csrf, map[string]string{"provider": "openai", "callback_url": callback}, nil)
		if status != 400 {
			t.Errorf("invalid callback accepted: %d", status)
		}
	}
}

func TestChatGPTPendingLoginAndRegistrationSurviveRestartEncrypted(t *testing.T) {
	dir := newTestDataDir(t)
	s, h, client, stop := startSubscriptionTestHub(t, dir)
	csrf := signIn(t, client, h.URL)
	status, body := request(t, client, "POST", h.URL+"/api/v1/ai/subscription/login", csrf, map[string]string{"provider": "openai"}, nil)
	var loginStatus ai.SubscriptionStatus
	if status != 200 || json.Unmarshal(body, &loginStatus) != nil || !loginStatus.Pending || loginStatus.CallbackURL != chatGPTCallbackURL || loginStatus.AuthorizationURL == "" {
		t.Fatalf("start login: %d %s", status, body)
	}
	state, err := s.loadChatGPTState()
	if err != nil || state.Pending == nil || state.Credentials.HostID == "" {
		t.Fatalf("pending login not persisted: %v", err)
	}
	original := *state.Pending
	ciphertext, err := s.db.GetSecret(chatGPTSecretID)
	if err != nil || bytes.Contains(ciphertext, []byte(original.Verifier)) || bytes.Contains(ciphertext, []byte(original.State)) {
		t.Fatal("pending proof/state not encrypted")
	}
	status, body = request(t, client, "GET", h.URL+"/api/v1/ai/subscription?provider=openai", csrf, nil, nil)
	if status != 200 || bytes.Contains(body, []byte(original.Verifier)) || bytes.Contains(body, []byte(original.Nonce)) || bytes.Contains(body, []byte(original.State)) {
		t.Fatal("status leaked pending secrets")
	}
	status, body = request(t, client, "GET", h.URL+"/api/v1/backup", csrf, nil, nil)
	if status != 200 {
		t.Fatalf("backup: %d", status)
	}
	archive, err := zip.NewReader(bytes.NewReader(body), int64(len(body)))
	if err != nil {
		t.Fatal(err)
	}
	for _, file := range archive.File {
		reader, err := file.Open()
		if err != nil {
			t.Fatal(err)
		}
		content, err := io.ReadAll(reader)
		_ = reader.Close()
		if err != nil {
			t.Fatal(err)
		}
		if bytes.Contains(content, []byte(original.Verifier)) || bytes.Contains(content, []byte(original.State)) || bytes.Contains(content, []byte(state.Credentials.HostID)) {
			t.Fatalf("backup exposed ChatGPT credentials in %s", file.Name)
		}
	}
	stop()
	s, h, client, stop = startSubscriptionTestHub(t, dir)
	csrf = signIn(t, client, h.URL)
	state, err = s.loadChatGPTState()
	if err != nil || state.Pending == nil || state.Pending.Verifier != original.Verifier || !state.Pending.ExpiresAt.Equal(original.ExpiresAt) {
		t.Fatal("restart lost pending login")
	}
	status, body = request(t, client, "GET", h.URL+"/api/v1/ai/subscription?provider=openai", csrf, nil, nil)
	if status != 200 || json.Unmarshal(body, &loginStatus) != nil || !loginStatus.Pending || loginStatus.CallbackURL != chatGPTCallbackURL {
		t.Fatalf("restart hid callback: %d %s", status, body)
	}
	var calls atomic.Int64
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		stored, err := s.loadChatGPTState()
		if err != nil || stored.Credentials.ClientID != "issued-client" || stored.Pending != nil {
			t.Error("registration was not saved before token exchange")
		}
		w.WriteHeader(400)
		_, _ = io.WriteString(w, `{"error":"invalid_grant","error_description":"private-provider-body"}`)
	}))
	defer provider.Close()
	s.chatGPT.TokenURL = provider.URL
	bad := chatGPTCallbackURL + "?" + url.Values{"state": {"wrong"}, "code": {"code-private"}, "client_id": {"issued-client"}}.Encode()
	status, _ = request(t, client, "POST", h.URL+"/api/v1/ai/subscription/callback", csrf, map[string]string{"provider": "openai", "callback_url": bad}, nil)
	if status != 400 || calls.Load() != 0 {
		t.Fatal("invalid state reached provider")
	}
	callback := chatGPTCallbackURL + "?" + url.Values{"state": {original.State}, "code": {"code-private"}, "client_id": {"issued-client"}}.Encode()
	status, body = request(t, client, "POST", h.URL+"/api/v1/ai/subscription/callback", csrf, map[string]string{"provider": "openai", "callback_url": callback}, nil)
	if status != 502 || calls.Load() != 1 || bytes.Contains(body, []byte("private-provider-body")) || bytes.Contains(body, []byte("code-private")) {
		t.Fatalf("unsafe failure: %d %s", status, body)
	}
	stop()
	s, h, client, _ = startSubscriptionTestHub(t, dir)
	csrf = signIn(t, client, h.URL)
	status, body = request(t, client, "POST", h.URL+"/api/v1/ai/subscription/login", csrf, map[string]string{"provider": "openai"}, nil)
	if status != 200 || json.Unmarshal(body, &loginStatus) != nil {
		t.Fatalf("retry: %d %s", status, body)
	}
	u, err := url.Parse(loginStatus.AuthorizationURL)
	if err != nil || u.Query().Get("client_id") != "issued-client" || u.Query().Get("ext_agent_host_id") != original.HostID {
		t.Fatal("retry abandoned dynamic registration")
	}
	// The old single-use callback cannot complete a newly started login.
	status, _ = request(t, client, "POST", h.URL+"/api/v1/ai/subscription/callback", csrf, map[string]string{"provider": "openai", "callback_url": callback}, nil)
	if status != 400 {
		t.Fatal("old callback was replayed")
	}
}

func testChatGPTCredentials() ai.ChatGPTCredentials {
	return ai.ChatGPTCredentials{HostID: "urn:uuid:01234567-89ab-4def-8123-456789abcdef", ClientID: "saved-client", Subject: "saved-subject", Email: "owner@example.test", AccessToken: "subscription-access", RefreshToken: "subscription-refresh", TokenType: "Bearer", Scope: "openid chatgpt.tokens.use.direct", ExpiresAt: time.Now().Add(time.Hour)}
}

func TestChatGPTRefreshModelsAndDisconnectKeepAPIKeysSeparate(t *testing.T) {
	s, h, client := newTestServer(t)
	csrf := signIn(t, client, h.URL)
	credentials := testChatGPTCredentials()
	credentials.ExpiresAt = time.Now().Add(-time.Minute)
	if err := s.saveChatGPTState(chatGPTState{Credentials: credentials}); err != nil {
		t.Fatal(err)
	}
	if err := s.saveSecret("ai:openai", "saved-api-key-do-not-use"); err != nil {
		t.Fatal(err)
	}
	var refreshCalls, apiCalls atomic.Int64
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/discovery" {
			w.WriteHeader(503)
			return
		}
		if r.URL.Path == "/api-key" {
			apiCalls.Add(1)
			t.Error("subscription selected API-key billing")
			w.WriteHeader(500)
			return
		}
		if r.URL.Path == "/token" {
			refreshCalls.Add(1)
			if err := r.ParseForm(); err != nil || r.Form.Get("grant_type") != "refresh_token" || r.Form.Get("client_id") != credentials.ClientID || r.Form.Get("refresh_token") != credentials.RefreshToken {
				t.Error("incorrect refresh credentials")
			}
			_, _ = io.WriteString(w, `{"access_token":"rotated-access","refresh_token":"rotated-refresh","token_type":"Bearer","expires_in":3600}`)
			return
		}
		if r.Header.Get("Authorization") != "Bearer rotated-access" || r.Header.Get("x-api-key") != "" {
			t.Error("wrong subscription credential")
		}
		if r.URL.Path == "/models" {
			_, _ = io.WriteString(w, `{"models":[{"slug":"plan-model","display_name":"Plan model","visibility":"list"}]}`)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = io.WriteString(w, "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Brew analysis\"}\n\ndata: {\"type\":\"response.completed\"}\n\n")
	}))
	defer provider.Close()
	s.chatGPT.TokenURL, s.chatGPT.ModelsURL, s.chatGPT.ResponsesURL = provider.URL+"/token", provider.URL+"/models", provider.URL+"/responses"
	s.chatGPT.DiscoveryURL = provider.URL + "/discovery"
	s.aiClient.OpenAIURL, s.aiClient.OpenAIModelsURL = provider.URL+"/api-key", provider.URL+"/api-key"
	status, body := request(t, client, "GET", h.URL+"/api/v1/ai/models?provider=openai&auth_mode=subscription", csrf, nil, nil)
	var models aiModelList
	if status != 200 || json.Unmarshal(body, &models) != nil || len(models.Models) != 1 || models.Models[0].ID != "plan-model" || models.AuthMode != ai.AuthSubscription || models.Source != "subscription" {
		t.Fatalf("models: %d %s", status, body)
	}
	stored, err := s.loadChatGPTState()
	if err != nil || stored.Credentials.RefreshToken != "rotated-refresh" || refreshCalls.Load() != 1 {
		t.Fatal("rotation not persisted")
	}
	status, _ = request(t, client, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "auth_mode": "subscription", "model": "plan-model", "enabled": true, "api_key": "rejected-key"}, nil)
	if status != 400 {
		t.Fatal("subscription accepted API key")
	}
	status, body = request(t, client, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "auth_mode": "subscription", "model": "plan-model", "enabled": true, "monitor_enabled": true}, nil)
	var cfg ai.Config
	if status != 200 || json.Unmarshal(body, &cfg) != nil || cfg.KeyConfigured || cfg.AuthMode != ai.AuthSubscription {
		t.Fatalf("save subscription: %d %s", status, body)
	}
	selected, key, err := s.aiConfiguration()
	if err != nil || key != "" || selected.AuthMode != ai.AuthSubscription {
		t.Fatal("subscription read API key")
	}
	result, err := s.performAI(context.Background(), selected, "must-never-be-used", ai.Request{Mode: "analysis", Prompt: "Analyze this brew"}, map[string]any{})
	if err != nil || result.Text != "Brew analysis" {
		t.Fatalf("subscription inference: %v %+v", err, result)
	}
	status, body = request(t, client, "DELETE", h.URL+"/api/v1/ai/subscription?provider=openai", csrf, nil, nil)
	var connection ai.SubscriptionStatus
	if status != 200 || json.Unmarshal(body, &connection) != nil || connection.Authenticated || !strings.Contains(connection.Message, "could not be confirmed") {
		t.Fatalf("disconnect: %d %s", status, body)
	}
	stored, err = s.loadChatGPTState()
	if err != nil || stored.Credentials.ClientID != credentials.ClientID || stored.Credentials.HostID != credentials.HostID || stored.Credentials.Subject != credentials.Subject || stored.Credentials.Email != credentials.Email || stored.Credentials.AccessToken != "" || stored.Credentials.RefreshToken != "" {
		t.Fatal("disconnect did not preserve only registration")
	}
	if _, err := s.db.Get("config", "ai", &cfg); err != nil || cfg.Enabled || cfg.MonitorEnabled || cfg.AuthMode != ai.AuthSubscription {
		t.Fatal("disconnect did not disable subscription settings")
	}
	if key, err := s.readSecret("ai:openai"); err != nil || key != "saved-api-key-do-not-use" || apiCalls.Load() != 0 {
		t.Fatal("subscription changed or used API credentials")
	}
}

func TestChatGPTRejectedRefreshNeverFallsBackAndRedacts(t *testing.T) {
	s, h, client := newTestServer(t)
	csrf := signIn(t, client, h.URL)
	credentials := testChatGPTCredentials()
	credentials.ExpiresAt = time.Now().Add(-time.Minute)
	if err := s.saveChatGPTState(chatGPTState{Credentials: credentials}); err != nil {
		t.Fatal(err)
	}
	if err := s.saveSecret("ai:openai", "saved-api-key"); err != nil {
		t.Fatal(err)
	}
	var apiCalls atomic.Int64
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/api" {
			apiCalls.Add(1)
		}
		w.WriteHeader(400)
		_, _ = io.WriteString(w, `{"error":"invalid_grant","error_description":"subscription-refresh private-provider-body"}`)
	}))
	defer provider.Close()
	s.chatGPT.TokenURL, s.aiClient.OpenAIModelsURL = provider.URL+"/token", provider.URL+"/api"
	status, body := request(t, client, "POST", h.URL+"/api/v1/ai/models", csrf, map[string]string{"provider": "openai", "auth_mode": "subscription"}, nil)
	if status != 502 || strings.Contains(string(body), "subscription-refresh") || strings.Contains(string(body), "private-provider-body") || apiCalls.Load() != 0 {
		t.Fatalf("unsafe refresh failure: %d %s", status, body)
	}
	stored, err := s.loadChatGPTState()
	if err != nil || stored.Credentials.ClientID != credentials.ClientID || stored.Credentials.AccessToken != "" || stored.Credentials.RefreshToken != "" {
		t.Fatal("rejected credentials retained or registration lost")
	}
	status, body = request(t, client, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "auth_mode": "subscription", "model": "model", "enabled": true}, nil)
	if status != 409 {
		t.Fatalf("enabled disconnected subscription: %d %s", status, body)
	}
	status, body = request(t, client, "PUT", h.URL+"/api/v1/ai/settings", csrf, map[string]any{"provider": "openai", "model": "model", "enabled": true}, nil)
	var cfg ai.Config
	if status != 200 || json.Unmarshal(body, &cfg) != nil || cfg.AuthMode != ai.AuthAPIKey || !cfg.KeyConfigured {
		t.Fatalf("legacy API settings lost backward compatibility: %d %s", status, body)
	}
}

func TestChatGPTLoopbackCompletesValidatedLogin(t *testing.T) {
	s, h, client := newTestServer(t)
	csrf := signIn(t, client, h.URL)
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	var login ai.ChatGPTLogin
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/jwks" {
			_ = json.NewEncoder(w).Encode(map[string]any{"keys": []any{map[string]string{"kty": "RSA", "kid": "server-test", "use": "sig", "alg": "RS256", "n": base64.RawURLEncoding.EncodeToString(key.N.Bytes()), "e": "AQAB"}}})
			return
		}
		if err := r.ParseForm(); err != nil || r.Form.Get("client_id") != "loopback-client" || r.Form.Get("code") != "private-code" || r.Form.Get("code_verifier") != login.Verifier || r.Form.Get("redirect_uri") != chatGPTCallbackURL {
			t.Error("loopback exchange did not use the pending proof")
		}
		header, _ := json.Marshal(map[string]string{"alg": "RS256", "kid": "server-test"})
		claims, _ := json.Marshal(map[string]any{"iss": "https://auth.openai.com", "aud": "loopback-client", "sub": "loopback-subject", "exp": time.Now().Add(time.Hour).Unix(), "nonce": login.Nonce, "email": "owner@example.test"})
		unsigned := base64.RawURLEncoding.EncodeToString(header) + "." + base64.RawURLEncoding.EncodeToString(claims)
		digest := sha256.Sum256([]byte(unsigned))
		signature, err := rsa.SignPKCS1v15(rand.Reader, key, crypto.SHA256, digest[:])
		if err != nil {
			t.Error(err)
			w.WriteHeader(500)
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"access_token": "private-access", "refresh_token": "private-refresh", "id_token": unsigned + "." + base64.RawURLEncoding.EncodeToString(signature), "token_type": "Bearer", "expires_in": 3600, "scope": "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"})
	}))
	defer provider.Close()
	s.chatGPT.TokenURL, s.chatGPT.JWKSURL = provider.URL+"/token", provider.URL+"/jwks"
	status, body := request(t, client, "POST", h.URL+"/api/v1/ai/subscription/login", csrf, map[string]string{"provider": "openai"}, nil)
	if status != 200 {
		t.Fatalf("start: %d %s", status, body)
	}
	s.subscriptionMu.Lock()
	listenerReady := s.chatGPTCallback != nil
	s.subscriptionMu.Unlock()
	if !listenerReady {
		t.Skip("loopback callback port is occupied; manual fallback covered separately")
	}
	stored, err := s.loadChatGPTState()
	if err != nil || stored.Pending == nil {
		t.Fatal("missing pending login")
	}
	login = *stored.Pending
	bad, _ := http.NewRequest(http.MethodGet, chatGPTCallbackURL+"?state=wrong", nil)
	bad.Host = "attacker.example"
	response, err := client.Do(bad)
	if err != nil {
		t.Fatal(err)
	}
	_ = response.Body.Close()
	if response.StatusCode != 400 {
		t.Fatalf("loopback accepted another Host: %d", response.StatusCode)
	}
	callback := chatGPTCallbackURL + "?" + url.Values{"state": {login.State}, "code": {"private-code"}, "client_id": {"loopback-client"}}.Encode()
	response, err = client.Get(callback)
	if err != nil {
		t.Fatal(err)
	}
	body, err = io.ReadAll(response.Body)
	_ = response.Body.Close()
	if err != nil || response.StatusCode != 200 || !bytes.Contains(body, []byte("ChatGPT is connected")) || bytes.Contains(body, []byte("private-")) {
		t.Fatalf("loopback response: %d %s (%v)", response.StatusCode, body, err)
	}
	status, body = request(t, client, "GET", h.URL+"/api/v1/ai/subscription?provider=openai", csrf, nil, nil)
	var connected ai.SubscriptionStatus
	if status != 200 || json.Unmarshal(body, &connected) != nil || !connected.Authenticated || connected.Pending || bytes.Contains(body, []byte("private-")) {
		t.Fatalf("connection status: %d %s", status, body)
	}
	stored, err = s.loadChatGPTState()
	if err != nil || stored.Pending != nil || stored.Credentials.AccessToken != "private-access" || stored.Credentials.Subject != "loopback-subject" {
		t.Fatal("validated callback was not saved")
	}
	ciphertext, err := s.db.GetSecret(chatGPTSecretID)
	if err != nil || bytes.Contains(ciphertext, []byte("private-access")) || bytes.Contains(ciphertext, []byte("private-refresh")) {
		t.Fatal("connected credentials were not encrypted")
	}
}

func TestChatGPTBusyLoopbackPortKeepsManualFlow(t *testing.T) {
	occupied, err := net.Listen("tcp", "127.0.0.1:1455")
	if err != nil {
		t.Skip("test requires reserving the loopback callback port")
	}
	defer occupied.Close()
	s, h, client := newTestServer(t)
	csrf := signIn(t, client, h.URL)
	status, body := request(t, client, "POST", h.URL+"/api/v1/ai/subscription/login", csrf, map[string]string{"provider": "openai"}, nil)
	var result ai.SubscriptionStatus
	if status != 200 || json.Unmarshal(body, &result) != nil || !result.Pending || result.AuthorizationURL == "" || result.CallbackURL != chatGPTCallbackURL || !strings.Contains(result.Message, "unavailable") {
		t.Fatalf("busy port blocked manual sign-in: %d %s", status, body)
	}
	s.subscriptionMu.Lock()
	listener := s.chatGPTCallback
	s.subscriptionMu.Unlock()
	if listener != nil {
		t.Fatal("claimed another process's callback port")
	}
}
