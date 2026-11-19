package ai

// ChatGPT plan usage uses OpenAI's public Sign in with ChatGPT flow. It never
// reads another application's credentials or falls back to API-key billing.
// Protocol: https://developers.openai.com/siwc/token-sharing-open-source

import (
	"bufio"
	"bytes"
	"context"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/big"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
	"unicode"
)

const (
	chatGPTIssuer     = "https://auth.openai.com"
	chatGPTResource   = "https://api.openai.com/v1"
	chatGPTScopes     = "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"
	chatGPTPermission = "chatgpt.tokens.use.direct"
)

var ErrChatGPTReauth = errors.New("ChatGPT connection expired or was revoked; sign in again")

// ChatGPTCredentials must remain in protected server storage. Never return it
// from a browser API, log it, or combine tokens from different registrations.
type ChatGPTCredentials struct {
	HostID            string    `json:"host_id"`
	ClientID          string    `json:"client_id"`
	Subject           string    `json:"subject,omitempty"`
	Email             string    `json:"email,omitempty"`
	IDToken           string    `json:"id_token,omitempty"`
	AccessToken       string    `json:"access_token,omitempty"`
	RefreshToken      string    `json:"refresh_token,omitempty"`
	TokenType         string    `json:"token_type,omitempty"`
	Scope             string    `json:"scope,omitempty"`
	ExpiresAt         time.Time `json:"expires_at,omitempty"`
	EarliestRefreshAt time.Time `json:"earliest_refresh_at,omitempty"`
}

func (v ChatGPTCredentials) PlanEnabled() bool {
	return v.ClientID != "" && v.ClientID != "dynamic_agent_client" && v.Subject != "" &&
		(v.AccessToken != "" || v.RefreshToken != "") && hasScope(v.Scope, chatGPTPermission)
}

func (v ChatGPTCredentials) NeedsRefresh() bool {
	now := time.Now()
	if v.AccessToken == "" || !v.ExpiresAt.After(now) {
		return true
	}
	// A server-provided earliest refresh time can be later than our preferred
	// renewal window. Continue using the still-valid token until then.
	return !v.ExpiresAt.After(now.Add(time.Minute)) && !v.EarliestRefreshAt.After(now)
}

// ChatGPTLogin is secret pending state; expose only AuthorizationURL and
// CallbackURL to the browser. Persist the host before Begin and the issued
// client ID after ValidateCallback, before Exchange, including failed exchanges.
type ChatGPTLogin struct {
	State            string    `json:"state"`
	Nonce            string    `json:"nonce"`
	Verifier         string    `json:"verifier"`
	CallbackURL      string    `json:"callback_url"`
	ClientID         string    `json:"client_id"`
	HostID           string    `json:"host_id"`
	Subject          string    `json:"subject,omitempty"`
	Email            string    `json:"email,omitempty"`
	IDToken          string    `json:"id_token,omitempty"`
	ExpiresAt        time.Time `json:"expires_at"`
	AuthorizationURL string    `json:"authorization_url"`
}

type ChatGPTClient struct {
	HTTP                                                                   *http.Client
	AuthorizeURL, TokenURL, JWKSURL, DiscoveryURL, ModelsURL, ResponsesURL string
}

func NewChatGPT() *ChatGPTClient {
	return &ChatGPTClient{
		HTTP:         &http.Client{Timeout: 120 * time.Second},
		AuthorizeURL: chatGPTIssuer + "/api/accounts/authorize",
		TokenURL:     chatGPTIssuer + "/api/accounts/oauth/token",
		JWKSURL:      chatGPTIssuer + "/.well-known/jwks.json",
		DiscoveryURL: chatGPTIssuer + "/.well-known/openid-configuration",
		ModelsURL:    chatGPTResource + "/models",
		ResponsesURL: chatGPTResource + "/responses",
	}
}

func NewChatGPTHostID() (string, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("urn:uuid:%x-%x-%x-%x-%x", b[:4], b[4:6], b[6:8], b[8:10], b[10:]), nil
}

func randomOAuthValue() (string, error) {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(b), nil
}

func (c *ChatGPTClient) Begin(saved ChatGPTCredentials, callbackURL string) (ChatGPTLogin, error) {
	var login ChatGPTLogin
	u, err := url.Parse(callbackURL)
	if err != nil || u.Scheme != "http" || u.Hostname() != "127.0.0.1" || u.Port() == "" || u.Path != "/auth/callback" || u.RawQuery != "" || u.Fragment != "" || u.User != nil {
		return login, errors.New("ChatGPT requires an HTTP loopback callback at 127.0.0.1 with path /auth/callback")
	}
	port, err := strconv.Atoi(u.Port())
	if err != nil || port < 1 || port > 65535 {
		return login, errors.New("invalid ChatGPT callback port")
	}
	if saved.HostID == "" || saved.ClientID == "dynamic_agent_client" {
		return login, errors.New("persist a stable ChatGPT host registration before signing in")
	}
	login = ChatGPTLogin{CallbackURL: callbackURL, ClientID: saved.ClientID, HostID: saved.HostID, Subject: saved.Subject, Email: saved.Email, IDToken: saved.IDToken, ExpiresAt: time.Now().Add(10 * time.Minute)}
	if login.State, err = randomOAuthValue(); err != nil {
		return ChatGPTLogin{}, err
	}
	if login.Nonce, err = randomOAuthValue(); err != nil {
		return ChatGPTLogin{}, err
	}
	if login.Verifier, err = randomOAuthValue(); err != nil {
		return ChatGPTLogin{}, err
	}
	authorize, err := url.Parse(c.AuthorizeURL)
	if err != nil || authorize.Host == "" {
		return ChatGPTLogin{}, errors.New("invalid ChatGPT authorization endpoint")
	}
	q := url.Values{"response_type": {"code"}, "redirect_uri": {callbackURL}, "scope": {chatGPTScopes}, "resource": {chatGPTResource}, "state": {login.State}, "nonce": {login.Nonce}, "ext_agent_host_id": {saved.HostID}, "code_challenge_method": {"S256"}}
	challenge := sha256.Sum256([]byte(login.Verifier))
	q.Set("code_challenge", base64.RawURLEncoding.EncodeToString(challenge[:]))
	if saved.ClientID == "" {
		q.Set("client_id", "dynamic_agent_client")
		q.Set("agent_name_hint", "PicoBrew Hub")
	} else {
		q.Set("client_id", saved.ClientID)
		if saved.IDToken != "" {
			q.Set("id_token_hint", saved.IDToken)
		}
		if saved.Email != "" {
			q.Set("login_hint", saved.Email)
		}
		if saved.Subject != "" && !hasScope(saved.Scope, chatGPTPermission) {
			q.Set("prompt", "consent")
		}
	}
	authorize.RawQuery = q.Encode()
	login.AuthorizationURL = authorize.String()
	return login, nil
}

func (c *ChatGPTClient) ValidateCallback(login ChatGPTLogin, query url.Values) (clientID, code string, err error) {
	if !login.ExpiresAt.After(time.Now()) || login.State == "" || len(query["state"]) != 1 || subtle.ConstantTimeCompare([]byte(query.Get("state")), []byte(login.State)) != 1 {
		return "", "", errors.New("ChatGPT sign-in state is invalid or expired; start a new sign-in")
	}
	for _, key := range []string{"code", "error", "client_id"} {
		if len(query[key]) > 1 {
			return "", "", errors.New("ChatGPT returned an ambiguous callback")
		}
	}
	if query.Get("error") != "" {
		return "", "", errors.New("ChatGPT sign-in was declined or failed; start a new sign-in when ready")
	}
	clientID = query.Get("client_id")
	if login.ClientID != "" {
		if clientID != "" && clientID != login.ClientID {
			return "", "", errors.New("ChatGPT callback changed the selected account registration")
		}
		clientID = login.ClientID
	}
	code = query.Get("code")
	if !safeOAuthValue(clientID, 1024) || clientID == "dynamic_agent_client" || !safeOAuthValue(code, 16384) {
		return "", "", errors.New("ChatGPT registration is incomplete; start a new sign-in")
	}
	return clientID, code, nil
}

func safeOAuthValue(value string, max int) bool {
	return value != "" && len(value) <= max && strings.IndexFunc(value, func(r rune) bool { return unicode.IsControl(r) || unicode.IsSpace(r) }) < 0
}
func hasScope(scopes, want string) bool {
	for _, s := range strings.Fields(scopes) {
		if s == want {
			return true
		}
	}
	return false
}

type chatGPTTokenResponse struct {
	AccessToken       string          `json:"access_token"`
	RefreshToken      string          `json:"refresh_token"`
	IDToken           string          `json:"id_token"`
	TokenType         string          `json:"token_type"`
	ExpiresIn         int64           `json:"expires_in"`
	Scope             *string         `json:"scope"`
	EarliestRefreshAt json.RawMessage `json:"earliest_refresh_at"`
}

func (c *ChatGPTClient) Exchange(ctx context.Context, login ChatGPTLogin, clientID, code string) (ChatGPTCredentials, error) {
	if !login.ExpiresAt.After(time.Now()) || !safeOAuthValue(clientID, 1024) || clientID == "dynamic_agent_client" || !safeOAuthValue(code, 16384) || (login.ClientID != "" && clientID != login.ClientID) {
		return ChatGPTCredentials{}, errors.New("ChatGPT authorization is invalid or expired")
	}
	form := url.Values{"grant_type": {"authorization_code"}, "client_id": {clientID}, "code": {code}, "code_verifier": {login.Verifier}, "redirect_uri": {login.CallbackURL}, "resource": {chatGPTResource}}
	wire, err := c.token(ctx, form)
	if err != nil {
		return ChatGPTCredentials{}, err
	}
	claims, err := c.verifyIDToken(ctx, wire.IDToken, clientID, login.Nonce)
	if err != nil {
		return ChatGPTCredentials{}, err
	}
	if login.Subject != "" && claims.Subject != login.Subject {
		return ChatGPTCredentials{}, errors.New("ChatGPT returned a different account; its credentials were not saved")
	}
	if wire.Scope == nil {
		return ChatGPTCredentials{}, errors.New("ChatGPT token response omitted granted scopes")
	}
	creds := ChatGPTCredentials{HostID: login.HostID, ClientID: clientID, Subject: claims.Subject, Email: claims.Email}
	return applyChatGPTTokens(creds, wire), nil
}

func applyChatGPTTokens(saved ChatGPTCredentials, wire chatGPTTokenResponse) ChatGPTCredentials {
	saved.AccessToken, saved.RefreshToken, saved.TokenType = wire.AccessToken, wire.RefreshToken, wire.TokenType
	if wire.IDToken != "" {
		saved.IDToken = wire.IDToken
	}
	if wire.Scope != nil {
		saved.Scope = *wire.Scope
	}
	saved.ExpiresAt = time.Now().Add(time.Duration(wire.ExpiresIn) * time.Second)
	saved.EarliestRefreshAt = time.Time{}
	if len(wire.EarliestRefreshAt) > 0 {
		var n int64
		if json.Unmarshal(wire.EarliestRefreshAt, &n) == nil {
			saved.EarliestRefreshAt = time.Unix(n, 0)
		} else {
			var s string
			if json.Unmarshal(wire.EarliestRefreshAt, &s) == nil {
				saved.EarliestRefreshAt, _ = time.Parse(time.RFC3339, s)
			}
		}
	}
	return saved
}

func (c *ChatGPTClient) token(ctx context.Context, form url.Values) (chatGPTTokenResponse, error) {
	var wire chatGPTTokenResponse
	ctx, cancel := context.WithTimeout(ctx, 30*time.Second)
	defer cancel()
	r, err := http.NewRequestWithContext(ctx, http.MethodPost, c.TokenURL, strings.NewReader(form.Encode()))
	if err != nil {
		return wire, errors.New("could not create the ChatGPT token request")
	}
	r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	body, err := c.readJSON(r, 1<<20)
	if err != nil {
		return wire, err
	}
	if json.Unmarshal(body, &wire) != nil || !safeOAuthValue(wire.AccessToken, 65536) || !safeOAuthValue(wire.RefreshToken, 65536) || !strings.EqualFold(wire.TokenType, "Bearer") || wire.ExpiresIn <= 0 || wire.ExpiresIn > 86400 {
		return wire, errors.New("ChatGPT returned an invalid token response")
	}
	return wire, nil
}

func (c *ChatGPTClient) Refresh(ctx context.Context, saved ChatGPTCredentials) (ChatGPTCredentials, error) {
	if saved.ClientID == "" || saved.ClientID == "dynamic_agent_client" || saved.Subject == "" || !safeOAuthValue(saved.RefreshToken, 65536) {
		return saved, ErrChatGPTReauth
	}
	if saved.EarliestRefreshAt.After(time.Now()) {
		return saved, errors.New("ChatGPT token cannot be refreshed yet; try again shortly")
	}
	wire, err := c.token(ctx, url.Values{"grant_type": {"refresh_token"}, "client_id": {saved.ClientID}, "refresh_token": {saved.RefreshToken}, "resource": {chatGPTResource}})
	if err != nil {
		var providerError *ChatGPTError
		if errors.As(err, &providerError) && providerError.Code == "invalid_grant" {
			providerError.Reauthentication = true
		}
		return saved, err
	}
	if wire.IDToken != "" {
		claims, err := c.verifyIDToken(ctx, wire.IDToken, saved.ClientID, "")
		if err != nil {
			return saved, err
		}
		if claims.Subject != saved.Subject {
			return saved, errors.New("ChatGPT refresh returned a different account")
		}
		saved.Email = claims.Email
	}
	return applyChatGPTTokens(saved, wire), nil
}

func (c *ChatGPTClient) httpClient() *http.Client {
	var client http.Client
	if c.HTTP != nil {
		client = *c.HTTP
	} else {
		client.Timeout = 120 * time.Second
	}
	client.CheckRedirect = func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }
	return &client
}

func (c *ChatGPTClient) readJSON(r *http.Request, limit int64) ([]byte, error) {
	resp, err := c.httpClient().Do(r)
	if err != nil {
		return nil, errors.New("ChatGPT could not be reached; check the connection and try again")
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(io.LimitReader(resp.Body, limit+1))
	if err != nil || int64(len(body)) > limit {
		return nil, errors.New("ChatGPT response was incomplete or exceeded the size limit")
	}
	if resp.StatusCode != http.StatusOK {
		return nil, chatGPTResponseError(resp.StatusCode, resp.Header.Get("x-request-id"), body)
	}
	return body, nil
}

// Only fixed error text and bounded machine-readable identifiers leave the
// package. Provider bodies can contain credentials, prompts or account details.
type ChatGPTError struct {
	Status           int
	Code, RequestID  string
	Reauthentication bool
}

func (e *ChatGPTError) Error() string {
	if e.Reauthentication {
		return ErrChatGPTReauth.Error()
	}
	switch e.Code {
	case "subscription_sharing_usage_limit_exceeded":
		return "ChatGPT plan usage limit reached; review this app in ChatGPT Settings → Usage"
	case "subscription_sharing_user_not_eligible":
		return "ChatGPT plan usage is unavailable for this account or workspace"
	case "subscription_sharing_unsupported_capability":
		return "ChatGPT plan does not support the selected model or request capability"
	case "subscription_sharing_usage_unavailable", "subscription_sharing_user_unavailable":
		return "ChatGPT plan usage is temporarily unavailable; try again later"
	case "invalid_grant":
		return "ChatGPT authorization code expired or was already used; start a fresh sign-in"
	}
	if e.Status != 0 {
		return fmt.Sprintf("ChatGPT returned HTTP %d; check sign-in, plan permissions and usage limits", e.Status)
	}
	return "ChatGPT did not complete the request; check sign-in, plan permissions and usage limits"
}
func (e *ChatGPTError) Unwrap() error {
	if e.Reauthentication {
		return ErrChatGPTReauth
	}
	return nil
}

func chatGPTResponseError(status int, requestID string, body []byte) error {
	var raw struct {
		Error json.RawMessage `json:"error"`
	}
	_ = json.Unmarshal(body, &raw)
	var code string
	if json.Unmarshal(raw.Error, &code) != nil {
		var v struct {
			Code string `json:"code"`
		}
		_ = json.Unmarshal(raw.Error, &v)
		code = v.Code
	}
	if !safeOAuthValue(code, 128) {
		code = ""
	}
	if !safeOAuthValue(requestID, 128) {
		requestID = ""
	}
	e := &ChatGPTError{Status: status, Code: code, RequestID: requestID}
	switch code {
	case "invalid_refresh_token", "token_expired", "refresh_token_expired", "refresh_token_invalidated", "refresh_token_reused":
		e.Reauthentication = true
	}
	return e
}

type chatGPTClaims struct {
	Issuer          string          `json:"iss"`
	Subject         string          `json:"sub"`
	Audience        json.RawMessage `json:"aud"`
	AuthorizedParty string          `json:"azp"`
	Expires         int64           `json:"exp"`
	IssuedAt        int64           `json:"iat"`
	NotBefore       int64           `json:"nbf"`
	Nonce           string          `json:"nonce"`
	Email           string          `json:"email"`
}

func (c *ChatGPTClient) verifyIDToken(ctx context.Context, token, clientID, nonce string) (chatGPTClaims, error) {
	var claims chatGPTClaims
	invalid := errors.New("ChatGPT identity token failed verification")
	parts := strings.Split(token, ".")
	if len(parts) != 3 || len(token) > 65536 {
		return claims, invalid
	}
	headerJSON, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return claims, invalid
	}
	var header struct{ Alg, Kid string }
	if json.Unmarshal(headerJSON, &header) != nil || header.Alg != "RS256" || !safeOAuthValue(header.Kid, 256) {
		return claims, invalid
	}
	payload, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return claims, invalid
	}
	sig, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return claims, invalid
	}
	ctx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	r, err := http.NewRequestWithContext(ctx, http.MethodGet, c.JWKSURL, nil)
	if err != nil {
		return claims, invalid
	}
	keyBody, err := c.readJSON(r, 1<<20)
	if err != nil {
		return claims, err
	}
	var jwks struct {
		Keys []struct{ Kty, Kid, Use, Alg, N, E string }
	}
	if json.Unmarshal(keyBody, &jwks) != nil || len(jwks.Keys) > 100 {
		return claims, invalid
	}
	verified := false
	hash := sha256.Sum256([]byte(parts[0] + "." + parts[1]))
	for _, key := range jwks.Keys {
		if key.Kid != header.Kid || key.Kty != "RSA" || (key.Use != "" && key.Use != "sig") || (key.Alg != "" && key.Alg != "RS256") {
			continue
		}
		n, e1 := base64.RawURLEncoding.DecodeString(key.N)
		eb, e2 := base64.RawURLEncoding.DecodeString(key.E)
		if e1 != nil || e2 != nil || len(n) < 256 || len(n) > 1024 || len(eb) == 0 || len(eb) > 4 {
			continue
		}
		e := 0
		for _, b := range eb {
			e = e*256 + int(b)
		}
		if e < 3 || e%2 == 0 {
			continue
		}
		pub := &rsa.PublicKey{N: new(big.Int).SetBytes(n), E: e}
		if rsa.VerifyPKCS1v15(pub, crypto.SHA256, hash[:], sig) == nil {
			verified = true
			break
		}
	}
	if !verified || json.Unmarshal(payload, &claims) != nil {
		return chatGPTClaims{}, invalid
	}
	var audiences []string
	var single string
	if json.Unmarshal(claims.Audience, &single) == nil {
		audiences = []string{single}
	} else if json.Unmarshal(claims.Audience, &audiences) != nil {
		return chatGPTClaims{}, invalid
	}
	found := false
	for _, aud := range audiences {
		if aud == clientID {
			found = true
		}
	}
	now := time.Now().Unix()
	if !found || claims.Issuer != chatGPTIssuer || !safeOAuthValue(claims.Subject, 1024) || claims.Expires <= now || claims.IssuedAt > now+60 || claims.NotBefore > now+60 || (len(audiences) > 1 && claims.AuthorizedParty != clientID) || (claims.AuthorizedParty != "" && claims.AuthorizedParty != clientID) || (nonce != "" && subtle.ConstantTimeCompare([]byte(claims.Nonce), []byte(nonce)) != 1) {
		return chatGPTClaims{}, invalid
	}
	if len(claims.Email) > 320 || strings.IndexFunc(claims.Email, unicode.IsControl) >= 0 {
		claims.Email = ""
	}
	return claims, nil
}

func (c *ChatGPTClient) Models(ctx context.Context, creds ChatGPTCredentials) ([]Model, error) {
	if !creds.PlanEnabled() || !safeOAuthValue(creds.AccessToken, 65536) {
		return nil, errors.New("connect ChatGPT and allow plan usage before loading models")
	}
	ctx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	r, err := http.NewRequestWithContext(ctx, http.MethodGet, c.ModelsURL, nil)
	if err != nil {
		return nil, errors.New("invalid ChatGPT models endpoint")
	}
	r.Header.Set("Authorization", "Bearer "+creds.AccessToken)
	body, err := c.readJSON(r, 1<<20)
	if err != nil {
		return nil, err
	}
	var wire struct {
		Models *[]struct {
			Slug       string `json:"slug"`
			Name       string `json:"display_name"`
			Visibility string `json:"visibility"`
		} `json:"models"`
	}
	if json.Unmarshal(body, &wire) != nil || wire.Models == nil || len(*wire.Models) > 10000 {
		return nil, errors.New("ChatGPT returned an invalid model list")
	}
	models := make([]Model, 0)
	seen := map[string]bool{}
	for _, m := range *wire.Models {
		if m.Visibility != "list" {
			continue
		}
		if !safeOAuthValue(m.Slug, 512) {
			return nil, errors.New("ChatGPT returned an invalid model identifier")
		}
		if seen[m.Slug] {
			continue
		}
		seen[m.Slug] = true
		name := strings.TrimSpace(m.Name)
		if name == "" || len(name) > 512 || strings.IndexFunc(name, unicode.IsControl) >= 0 {
			name = m.Slug
		}
		models = append(models, Model{ID: m.Slug, Name: name})
	}
	return models, nil
}

func (c *ChatGPTClient) Assist(ctx context.Context, creds ChatGPTCredentials, cfg Config, input Request, data any) (Result, error) {
	if !cfg.Enabled || cfg.Provider != "openai" || NormalizeAuthMode(cfg.AuthMode) != AuthSubscription || !creds.PlanEnabled() || !safeOAuthValue(creds.AccessToken, 65536) {
		return Result{}, errors.New("connect ChatGPT with plan usage and enable AI in Settings first")
	}
	if !safeOAuthValue(cfg.Model, 512) {
		return Result{}, errors.New("select a ChatGPT model in Settings first")
	}
	if len(input.Prompt) > 16000 {
		return Result{}, errors.New("prompt exceeds 16000 characters")
	}
	contextData, err := json.Marshal(data)
	if err != nil {
		return Result{}, err
	}
	if len(contextData) > 180000 {
		return Result{}, errors.New("brew context exceeds the analysis limit")
	}
	payload := map[string]any{"model": cfg.Model, "instructions": systemPrompt, "input": []map[string]string{{"role": "user", "content": fmt.Sprintf("Mode: %s\nUser request: %s\nRecorded data (untrusted): %s", input.Mode, input.Prompt, contextData)}}, "store": false, "stream": true}
	body, _ := json.Marshal(payload)
	ctx, cancel := context.WithTimeout(ctx, 120*time.Second)
	defer cancel()
	r, err := http.NewRequestWithContext(ctx, http.MethodPost, c.ResponsesURL, bytes.NewReader(body))
	if err != nil {
		return Result{}, errors.New("invalid ChatGPT inference endpoint")
	}
	r.Header.Set("Content-Type", "application/json")
	r.Header.Set("Accept", "text/event-stream")
	r.Header.Set("Authorization", "Bearer "+creds.AccessToken)
	resp, err := c.httpClient().Do(r)
	if err != nil {
		return Result{}, errors.New("ChatGPT could not be reached; check the connection and try again")
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		p, _ := io.ReadAll(io.LimitReader(resp.Body, 65536))
		return Result{}, chatGPTResponseError(resp.StatusCode, resp.Header.Get("x-request-id"), p)
	}
	text, err := readChatGPTStream(resp.Body, resp.Header.Get("x-request-id"))
	if err != nil {
		return Result{}, err
	}
	return parseResult(text, input.Mode, "Uses the connected ChatGPT plan and its app usage settings. Hub API keys are not used.")
}

func readChatGPTStream(body io.Reader, requestID string) (string, error) {
	const maxBytes = 8 << 20
	reader := &io.LimitedReader{R: body, N: maxBytes + 1}
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 4096), 1<<20)
	var output strings.Builder
	var eventData []string
	completed := false
	consume := func() error {
		if len(eventData) == 0 {
			return nil
		}
		data := strings.Join(eventData, "\n")
		eventData = nil
		if data == "[DONE]" {
			if !completed {
				return errors.New("ChatGPT stream ended before completion")
			}
			return nil
		}
		var event struct {
			Type     string          `json:"type"`
			Delta    string          `json:"delta"`
			Error    json.RawMessage `json:"error"`
			Response struct {
				Status string          `json:"status"`
				Error  json.RawMessage `json:"error"`
				Output []struct {
					Type    string                        `json:"type"`
					Content []struct{ Type, Text string } `json:"content"`
				} `json:"output"`
			} `json:"response"`
		}
		if json.Unmarshal([]byte(data), &event) != nil {
			return errors.New("ChatGPT returned an invalid response stream")
		}
		switch event.Type {
		case "response.output_text.delta":
			if completed {
				return errors.New("ChatGPT sent output after completion")
			}
			if output.Len()+len(event.Delta) > 1<<20 {
				return errors.New("ChatGPT output exceeded the size limit")
			}
			output.WriteString(event.Delta)
		case "response.completed":
			if event.Response.Status != "" && event.Response.Status != "completed" {
				return errors.New("ChatGPT response did not complete")
			}
			completed = true
			if output.Len() == 0 {
				for _, item := range event.Response.Output {
					if item.Type == "message" {
						for _, block := range item.Content {
							if block.Type == "output_text" {
								if output.Len()+len(block.Text) > 1<<20 {
									return errors.New("ChatGPT output exceeded the size limit")
								}
								output.WriteString(block.Text)
							}
						}
					}
				}
			}
		case "response.failed", "response.incomplete", "error":
			errorBody := event.Error
			if len(event.Response.Error) > 0 {
				errorBody = event.Response.Error
			}
			p, _ := json.Marshal(map[string]json.RawMessage{"error": errorBody})
			return chatGPTResponseError(0, requestID, p)
		}
		return nil
	}
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			if err := consume(); err != nil {
				return "", err
			}
			if completed {
				return output.String(), nil
			}
			continue
		}
		if strings.HasPrefix(line, "data:") {
			eventData = append(eventData, strings.TrimPrefix(strings.TrimPrefix(line, "data:"), " "))
		}
	}
	if scanner.Err() != nil || reader.N <= 0 {
		return "", errors.New("ChatGPT response stream was interrupted or exceeded the size limit")
	}
	if err := consume(); err != nil {
		return "", err
	}
	if !completed {
		return "", errors.New("ChatGPT stream ended before response.completed; no partial result was accepted")
	}
	return output.String(), nil
}

func (c *ChatGPTClient) Revoke(ctx context.Context, saved ChatGPTCredentials) error {
	if saved.RefreshToken == "" {
		return nil
	}
	ctx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	r, err := http.NewRequestWithContext(ctx, http.MethodGet, c.DiscoveryURL, nil)
	if err != nil {
		return errors.New("invalid ChatGPT discovery endpoint")
	}
	body, err := c.readJSON(r, 1<<20)
	if err != nil {
		return err
	}
	var config struct {
		Issuer             string `json:"issuer"`
		RevocationEndpoint string `json:"revocation_endpoint"`
	}
	if json.Unmarshal(body, &config) != nil || config.Issuer != chatGPTIssuer {
		return errors.New("ChatGPT discovery response was invalid")
	}
	u, err := url.Parse(config.RevocationEndpoint)
	if err != nil || u.Scheme != "https" || u.Host != "auth.openai.com" || u.User != nil || u.Fragment != "" {
		return errors.New("ChatGPT revocation endpoint was invalid")
	}
	r, err = http.NewRequestWithContext(ctx, http.MethodPost, u.String(), strings.NewReader(url.Values{"token": {saved.RefreshToken}, "token_type_hint": {"refresh_token"}, "client_id": {saved.ClientID}}.Encode()))
	if err != nil {
		return errors.New("could not create ChatGPT revocation request")
	}
	r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	_, err = c.readJSON(r, 65536)
	return err
}
