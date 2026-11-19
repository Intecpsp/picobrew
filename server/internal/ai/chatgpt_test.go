package ai

import (
	"context"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"testing"
	"time"
)

type chatGPTRoundTrip func(*http.Request) (*http.Response, error)

func (f chatGPTRoundTrip) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func chatGPTHTTP(status int, body string) *http.Response {
	return &http.Response{StatusCode: status, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(body))}
}

func chatGPTFixture(t *testing.T) (*ChatGPTClient, ChatGPTLogin, *rsa.PrivateKey) {
	t.Helper()
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	host, err := NewChatGPTHostID()
	if err != nil {
		t.Fatal(err)
	}
	c := NewChatGPT()
	login, err := c.Begin(ChatGPTCredentials{HostID: host}, "http://127.0.0.1:1455/auth/callback")
	if err != nil {
		t.Fatal(err)
	}
	return c, login, key
}

func chatGPTJWT(t *testing.T, key *rsa.PrivateKey, login ChatGPTLogin, modify func(map[string]any)) string {
	t.Helper()
	claims := map[string]any{"iss": chatGPTIssuer, "sub": "account-one", "aud": "issued-client", "exp": time.Now().Add(time.Hour).Unix(), "iat": time.Now().Unix(), "nonce": login.Nonce, "email": "brew@example.com"}
	if modify != nil {
		modify(claims)
	}
	header, _ := json.Marshal(map[string]string{"alg": "RS256", "kid": "test-key"})
	payload, _ := json.Marshal(claims)
	unsigned := base64.RawURLEncoding.EncodeToString(header) + "." + base64.RawURLEncoding.EncodeToString(payload)
	hash := sha256.Sum256([]byte(unsigned))
	sig, err := rsa.SignPKCS1v15(rand.Reader, key, crypto.SHA256, hash[:])
	if err != nil {
		t.Fatal(err)
	}
	return unsigned + "." + base64.RawURLEncoding.EncodeToString(sig)
}

func chatGPTJWKS(key *rsa.PrivateKey) string {
	b, _ := json.Marshal(map[string]any{"keys": []map[string]string{{"kty": "RSA", "kid": "test-key", "use": "sig", "alg": "RS256", "n": base64.RawURLEncoding.EncodeToString(key.N.Bytes()), "e": "AQAB"}}})
	return string(b)
}

func chatGPTTokenJSON(token, scope string) string {
	b, _ := json.Marshal(map[string]any{"access_token": "oauth-access", "refresh_token": "oauth-refresh", "id_token": token, "token_type": "Bearer", "expires_in": 3600, "scope": scope})
	return string(b)
}

func chatGPTConnected() ChatGPTCredentials {
	return ChatGPTCredentials{HostID: "urn:uuid:00112233-4455-4677-8899-aabbccddeeff", ClientID: "issued-client", Subject: "account-one", Email: "brew@example.com", AccessToken: "oauth-access", RefreshToken: "oauth-refresh", TokenType: "Bearer", Scope: chatGPTScopes, ExpiresAt: time.Now().Add(time.Hour)}
}

func TestChatGPTAuthorizationRegistrationAndReauthorization(t *testing.T) {
	c := NewChatGPT()
	host, err := NewChatGPTHostID()
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(host, "urn:uuid:") || len(host) != 45 {
		t.Fatalf("invalid host ID %q", host)
	}
	login, err := c.Begin(ChatGPTCredentials{HostID: host}, "http://127.0.0.1:1455/auth/callback")
	if err != nil {
		t.Fatal(err)
	}
	u, _ := url.Parse(login.AuthorizationURL)
	q := u.Query()
	for k, want := range map[string]string{"client_id": "dynamic_agent_client", "agent_name_hint": "PicoBrew Hub", "ext_agent_host_id": host, "resource": chatGPTResource, "scope": chatGPTScopes, "nonce": login.Nonce, "state": login.State, "redirect_uri": login.CallbackURL, "code_challenge_method": "S256"} {
		if q.Get(k) != want {
			t.Errorf("%s=%q want %q", k, q.Get(k), want)
		}
	}
	hash := sha256.Sum256([]byte(login.Verifier))
	if q.Get("code_challenge") != base64.RawURLEncoding.EncodeToString(hash[:]) {
		t.Fatal("PKCE challenge mismatch")
	}
	saved := chatGPTConnected()
	saved.HostID = host
	saved.IDToken = "identity-hint"
	returning, err := c.Begin(saved, "http://127.0.0.1:54321/auth/callback")
	if err != nil {
		t.Fatal(err)
	}
	u, _ = url.Parse(returning.AuthorizationURL)
	q = u.Query()
	if q.Get("client_id") != saved.ClientID || q.Has("agent_name_hint") || q.Get("id_token_hint") != saved.IDToken || q.Get("login_hint") != saved.Email || q.Get("ext_agent_host_id") != host || q.Has("prompt") {
		t.Fatalf("invalid returning authorization: %v", q)
	}
	if login.State == returning.State || login.Nonce == returning.Nonce || login.Verifier == returning.Verifier {
		t.Fatal("authorization secrets reused")
	}
	saved.Scope = "openid email"
	reauthorize, err := c.Begin(saved, login.CallbackURL)
	if err != nil {
		t.Fatal(err)
	}
	u, _ = url.Parse(reauthorize.AuthorizationURL)
	if u.Query().Get("prompt") != "consent" {
		t.Fatal("explicit retry of missing plan consent did not request consent")
	}
}

func TestChatGPTRejectsUnsafeCallbackAddresses(t *testing.T) {
	c := NewChatGPT()
	for _, callback := range []string{"http://localhost:1455/auth/callback", "https://127.0.0.1:1455/auth/callback", "http://192.168.0.1:1455/auth/callback", "http://127.0.0.1/auth/callback", "http://127.0.0.1:0/auth/callback", "http://127.0.0.1:65536/auth/callback", "http://127.0.0.1:1455/callback", "http://127.0.0.1:1455/auth/callback?x=y", "http://user@127.0.0.1:1455/auth/callback"} {
		t.Run(callback, func(t *testing.T) {
			if _, err := c.Begin(ChatGPTCredentials{HostID: "saved-host"}, callback); err == nil {
				t.Fatal("accepted unsafe callback")
			}
		})
	}
	if _, err := c.Begin(ChatGPTCredentials{}, "http://127.0.0.1:1455/auth/callback"); err == nil {
		t.Fatal("missing persistent host accepted")
	}
}

func TestChatGPTCallbackValidation(t *testing.T) {
	c, login, _ := chatGPTFixture(t)
	good := url.Values{"state": {login.State}, "client_id": {"issued-client"}, "code": {"authorization-code"}}
	clientID, code, err := c.ValidateCallback(login, good)
	if err != nil || clientID != "issued-client" || code != "authorization-code" {
		t.Fatalf("valid callback failed: %s %s %v", clientID, code, err)
	}
	tests := []struct {
		name   string
		change func(url.Values, *ChatGPTLogin)
	}{
		{"state", func(q url.Values, l *ChatGPTLogin) { q.Set("state", "wrong") }},
		{"state duplicate", func(q url.Values, l *ChatGPTLogin) { q.Add("state", l.State) }},
		{"code duplicate", func(q url.Values, l *ChatGPTLogin) { q.Add("code", "second") }},
		{"declined", func(q url.Values, l *ChatGPTLogin) { q.Set("error", "access_denied") }},
		{"missing registration", func(q url.Values, l *ChatGPTLogin) { q.Del("client_id") }},
		{"entrypoint as client", func(q url.Values, l *ChatGPTLogin) { q.Set("client_id", "dynamic_agent_client") }},
		{"client replacement", func(q url.Values, l *ChatGPTLogin) { l.ClientID = "other-client" }},
		{"expired", func(q url.Values, l *ChatGPTLogin) { l.ExpiresAt = time.Now().Add(-time.Second) }},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			q := url.Values{}
			for k, v := range good {
				q[k] = append([]string(nil), v...)
			}
			pending := login
			test.change(q, &pending)
			if _, _, err := c.ValidateCallback(pending, q); err == nil {
				t.Fatal("invalid callback accepted")
			}
		})
	}
	login.ClientID = "issued-client"
	good.Del("client_id")
	if id, _, err := c.ValidateCallback(login, good); err != nil || id != login.ClientID {
		t.Fatal("returning callback did not retain registered client")
	}
}

func TestChatGPTExchangeVerifiesIdentityAndScope(t *testing.T) {
	c, login, key := chatGPTFixture(t)
	token := chatGPTJWT(t, key, login, nil)
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		switch r.URL.Path {
		case "/api/accounts/oauth/token":
			if r.Method != "POST" || r.Header.Get("Content-Type") != "application/x-www-form-urlencoded" {
				t.Fatal("invalid token transport")
			}
			_ = r.ParseForm()
			for field, want := range map[string]string{"grant_type": "authorization_code", "client_id": "issued-client", "code": "auth-code", "code_verifier": login.Verifier, "redirect_uri": login.CallbackURL, "resource": chatGPTResource} {
				if r.Form.Get(field) != want {
					t.Errorf("token field %s mismatch", field)
				}
			}
			if r.Form.Has("client_secret") {
				t.Fatal("public client supplied a secret")
			}
			return chatGPTHTTP(200, chatGPTTokenJSON(token, chatGPTScopes)), nil
		case "/.well-known/jwks.json":
			return chatGPTHTTP(200, chatGPTJWKS(key)), nil
		default:
			t.Fatalf("unexpected path %s", r.URL.Path)
			return nil, nil
		}
	})}
	creds, err := c.Exchange(context.Background(), login, "issued-client", "auth-code")
	if err != nil {
		t.Fatal(err)
	}
	if creds.HostID != login.HostID || creds.ClientID != "issued-client" || creds.Subject != "account-one" || creds.Email != "brew@example.com" || creds.IDToken != token || !creds.PlanEnabled() || creds.NeedsRefresh() {
		t.Fatalf("invalid credential result %#v", creds)
	}
	login.Subject = "different-account"
	if _, err := c.Exchange(context.Background(), login, "issued-client", "auth-code"); err == nil {
		t.Fatal("changed account identity accepted")
	}
}

func TestChatGPTRejectsInvalidSignedIdentityClaims(t *testing.T) {
	c, login, key := chatGPTFixture(t)
	changes := map[string]func(map[string]any){
		"issuer":                 func(v map[string]any) { v["iss"] = "https://attacker.example" },
		"audience":               func(v map[string]any) { v["aud"] = "another-client" },
		"nonce":                  func(v map[string]any) { v["nonce"] = "another-attempt" },
		"expired":                func(v map[string]any) { v["exp"] = time.Now().Add(-time.Second).Unix() },
		"not yet valid":          func(v map[string]any) { v["nbf"] = time.Now().Add(time.Hour).Unix() },
		"issued in future":       func(v map[string]any) { v["iat"] = time.Now().Add(time.Hour).Unix() },
		"missing subject":        func(v map[string]any) { delete(v, "sub") },
		"ambiguous audience":     func(v map[string]any) { v["aud"] = []string{"issued-client", "other-client"} },
		"wrong authorized party": func(v map[string]any) { v["azp"] = "other-client" },
	}
	for name, change := range changes {
		t.Run(name, func(t *testing.T) {
			token := chatGPTJWT(t, key, login, change)
			c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) { return chatGPTHTTP(200, chatGPTJWKS(key)), nil })}
			if _, err := c.verifyIDToken(context.Background(), token, "issued-client", login.Nonce); err == nil {
				t.Fatal("invalid identity accepted")
			}
		})
	}
	token := chatGPTJWT(t, key, login, nil)
	parts := strings.Split(token, ".")
	parts[1] = base64.RawURLEncoding.EncodeToString([]byte(`{"sub":"attacker"}`))
	if _, err := c.verifyIDToken(context.Background(), strings.Join(parts, "."), "issued-client", login.Nonce); err == nil {
		t.Fatal("tampered signature accepted")
	}
}

func TestChatGPTMissingPlanPermissionRetainsIdentityWithoutInference(t *testing.T) {
	c, login, key := chatGPTFixture(t)
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		if r.URL.Path == "/.well-known/jwks.json" {
			return chatGPTHTTP(200, chatGPTJWKS(key)), nil
		}
		return chatGPTHTTP(200, chatGPTTokenJSON(chatGPTJWT(t, key, login, nil), "openid email")), nil
	})}
	creds, err := c.Exchange(context.Background(), login, "issued-client", "code")
	if err != nil {
		t.Fatal(err)
	}
	if creds.Subject == "" || creds.ClientID == "" || creds.PlanEnabled() {
		t.Fatal("identity/plan permissions conflated")
	}
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		t.Fatal("request made without plan permission")
		return nil, nil
	})}
	if _, err := c.Models(context.Background(), creds); err == nil {
		t.Fatal("models accepted without permission")
	}
	if _, err := c.Assist(context.Background(), creds, Config{Provider: "openai", AuthMode: AuthSubscription, Enabled: true, Model: "chosen-model"}, Request{}, nil); err == nil {
		t.Fatal("inference accepted without permission")
	}
}

func TestChatGPTRefreshRotationAndTerminalErrors(t *testing.T) {
	c := NewChatGPT()
	saved := chatGPTConnected()
	saved.IDToken = "retained-identity"
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		_ = r.ParseForm()
		if r.Form.Get("grant_type") != "refresh_token" || r.Form.Get("client_id") != saved.ClientID || r.Form.Get("refresh_token") != saved.RefreshToken || r.Form.Get("resource") != chatGPTResource || r.Form.Has("scope") {
			t.Fatalf("invalid refresh: %v", r.Form)
		}
		return chatGPTHTTP(200, `{"access_token":"new-access","refresh_token":"new-refresh","token_type":"Bearer","expires_in":3600}`), nil
	})}
	next, err := c.Refresh(context.Background(), saved)
	if err != nil {
		t.Fatal(err)
	}
	if next.AccessToken != "new-access" || next.RefreshToken != "new-refresh" || next.Scope != saved.Scope || next.IDToken != saved.IDToken || next.Subject != saved.Subject || next.ClientID != saved.ClientID {
		t.Fatal("refresh rotation lost session identity")
	}
	for _, code := range []string{"invalid_grant", "invalid_refresh_token", "refresh_token_expired", "refresh_token_reused"} {
		t.Run(code, func(t *testing.T) {
			c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(*http.Request) (*http.Response, error) {
				return chatGPTHTTP(400, fmt.Sprintf(`{"error":%q,"error_description":"secret token must not escape"}`, code)), nil
			})}
			retained, err := c.Refresh(context.Background(), saved)
			if !errors.Is(err, ErrChatGPTReauth) || retained.ClientID != saved.ClientID {
				t.Fatalf("terminal refresh mishandled: %v", err)
			}
			if strings.Contains(err.Error(), "secret") {
				t.Fatal("provider body leaked")
			}
		})
	}
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(*http.Request) (*http.Response, error) {
		return chatGPTHTTP(503, `{"detail":"temporary failure secret"}`), nil
	})}
	retained, err := c.Refresh(context.Background(), saved)
	if err == nil || errors.Is(err, ErrChatGPTReauth) || retained.RefreshToken != saved.RefreshToken {
		t.Fatal("temporary failure destroyed session")
	}
}

func TestChatGPTRefreshWaitsForPermittedTimeWithoutRejectingValidToken(t *testing.T) {
	credentials := chatGPTConnected()
	credentials.ExpiresAt = time.Now().Add(40 * time.Second)
	credentials.EarliestRefreshAt = time.Now().Add(20 * time.Second)
	if credentials.NeedsRefresh() {
		t.Fatal("renewal window should not reject a still-valid token before refresh is allowed")
	}
	credentials.EarliestRefreshAt = time.Now().Add(-time.Second)
	if !credentials.NeedsRefresh() {
		t.Fatal("near-expiry token should refresh once allowed")
	}
	credentials.ExpiresAt = time.Now().Add(-time.Second)
	credentials.EarliestRefreshAt = time.Now().Add(time.Minute)
	if !credentials.NeedsRefresh() {
		t.Fatal("expired token must never be used")
	}
}

func TestChatGPTModelsUseSubscriptionCatalogOrder(t *testing.T) {
	c := NewChatGPT()
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		if r.Header.Get("Authorization") != "Bearer oauth-access" {
			t.Fatal("wrong subscription token")
		}
		return chatGPTHTTP(200, `{"models":[{"slug":"model-z","display_name":"Newest model","visibility":"list"},{"slug":"hidden-model","visibility":"hide"},{"slug":"model-a","display_name":"Other model","visibility":"list"},{"slug":"model-z","visibility":"list"}]}`), nil
	})}
	models, err := c.Models(context.Background(), chatGPTConnected())
	if err != nil {
		t.Fatal(err)
	}
	if len(models) != 2 || models[0].ID != "model-z" || models[0].Name != "Newest model" || models[1].ID != "model-a" {
		t.Fatalf("wrong model catalog %v", models)
	}
}

func TestChatGPTInferenceOnlyUsesSupportedPublicResponsesFields(t *testing.T) {
	c := NewChatGPT()
	requests := 0
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		requests++
		if r.URL.String() != chatGPTResource+"/responses" || r.Method != "POST" || r.Header.Get("Authorization") != "Bearer oauth-access" {
			t.Fatal("wrong inference route")
		}
		var body map[string]any
		if json.NewDecoder(r.Body).Decode(&body) != nil {
			t.Fatal("invalid JSON")
		}
		if body["store"] != false || body["stream"] != true || body["instructions"] != systemPrompt || body["model"] != "selected-model" {
			t.Fatalf("wrong request fields %v", body)
		}
		for _, field := range []string{"max_output_tokens", "temperature", "previous_response_id", "conversation", "background", "prompt", "tools"} {
			if _, ok := body[field]; ok {
				t.Fatalf("unsupported field %s", field)
			}
		}
		input := body["input"].([]any)
		if len(input) != 1 || input[0].(map[string]any)["role"] != "user" {
			t.Fatal("wrong input shape")
		}
		return chatGPTHTTP(200, "event: response.output_text.delta\ndata: {\"type\":\"response.output_text.delta\",\"delta\":\"Brew advice.\"}\n\ndata: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n"), nil
	})}
	result, err := c.Assist(context.Background(), chatGPTConnected(), Config{Enabled: true, Provider: "openai", AuthMode: AuthSubscription, Model: "selected-model"}, Request{Prompt: "advice"}, map[string]any{"untrusted": "brew note"})
	if err != nil || result.Text != "Brew advice." || requests != 1 {
		t.Fatalf("inference failed: %v %v", result, err)
	}
}

func TestChatGPTStreamCompletionAndFailures(t *testing.T) {
	delta := "data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n"
	for name, body := range map[string]string{
		"EOF":                      delta,
		"done without completion":  delta + "data: [DONE]\n\n",
		"incomplete":               delta + "data: {\"type\":\"response.incomplete\"}\n\n",
		"usage limit after output": delta + "data: {\"type\":\"response.failed\",\"response\":{\"error\":{\"code\":\"subscription_sharing_usage_limit_exceeded\"}}}\n\n",
		"malformed event":          delta + "data: not-json\n\n",
	} {
		t.Run(name, func(t *testing.T) {
			text, err := readChatGPTStream(strings.NewReader(body), "request-123")
			if err == nil || text != "" {
				t.Fatalf("accepted partial output %q: %v", text, err)
			}
		})
	}
	text, err := readChatGPTStream(strings.NewReader("data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"complete answer\"}]}]}}\n\n"), "")
	if err != nil || text != "complete answer" {
		t.Fatalf("completion-only output failed: %s %v", text, err)
	}
}

func TestChatGPTNeverFollowsCredentialRedirects(t *testing.T) {
	c := NewChatGPT()
	calls := 0
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		calls++
		resp := chatGPTHTTP(302, "redirect")
		resp.Header.Set("Location", "https://attacker.example/collect")
		return resp, nil
	}), CheckRedirect: func(*http.Request, []*http.Request) error { return nil }}
	if _, err := c.Models(context.Background(), chatGPTConnected()); err == nil || calls != 1 {
		t.Fatalf("redirect followed: calls=%d err=%v", calls, err)
	}
}

func TestChatGPTRevokeChecksDiscoveredOrigin(t *testing.T) {
	c := NewChatGPT()
	calls := 0
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		calls++
		if r.Method == "GET" {
			return chatGPTHTTP(200, `{"issuer":"https://auth.openai.com","revocation_endpoint":"https://auth.openai.com/api/accounts/oauth/revoke"}`), nil
		}
		_ = r.ParseForm()
		if r.URL.String() != chatGPTIssuer+"/api/accounts/oauth/revoke" || r.Form.Get("client_id") != "issued-client" || r.Form.Get("token") != "oauth-refresh" || r.Form.Get("token_type_hint") != "refresh_token" {
			t.Fatal("wrong revocation")
		}
		return chatGPTHTTP(200, ""), nil
	})}
	if err := c.Revoke(context.Background(), chatGPTConnected()); err != nil || calls != 2 {
		t.Fatalf("revocation failed %v", err)
	}
	calls = 0
	c.HTTP = &http.Client{Transport: chatGPTRoundTrip(func(r *http.Request) (*http.Response, error) {
		calls++
		return chatGPTHTTP(200, `{"issuer":"https://auth.openai.com","revocation_endpoint":"https://attacker.example/collect"}`), nil
	})}
	if err := c.Revoke(context.Background(), chatGPTConnected()); err == nil || calls != 1 {
		t.Fatalf("unsafe revocation accepted %v", err)
	}
}
