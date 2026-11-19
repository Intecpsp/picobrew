package server

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/ai"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

const chatGPTSecretID = "ai:subscription:openai"
const chatGPTCallbackURL = "http://127.0.0.1:1455/auth/callback"

// Registration, pending PKCE state, and tokens share the encrypted vault record.
// A restarted Hub can finish a pending authorization or reuse its issued client
// registration. These values never enter ordinary records, backups, or logs.
type chatGPTState struct {
	Credentials ai.ChatGPTCredentials `json:"credentials"`
	Pending     *ai.ChatGPTLogin      `json:"pending,omitempty"`
}

func (s *Server) loadChatGPTState() (chatGPTState, error) {
	var state chatGPTState
	raw, err := s.readSecret(chatGPTSecretID)
	if errors.Is(err, store.ErrNotFound) {
		return state, nil
	}
	if err != nil {
		return state, err
	}
	if json.Unmarshal([]byte(raw), &state) != nil {
		return state, errors.New("stored ChatGPT connection could not be read")
	}
	return state, nil
}

func (s *Server) saveChatGPTState(state chatGPTState) error {
	raw, err := json.Marshal(state)
	if err != nil {
		return errors.New("ChatGPT connection could not be saved")
	}
	return s.saveSecret(chatGPTSecretID, string(raw))
}

func chatGPTStatus(state chatGPTState) ai.SubscriptionStatus {
	status := ai.SubscriptionStatus{Provider: "openai", Installed: true, CallbackURL: chatGPTCallbackURL,
		Message: "Connect your ChatGPT account in the browser, then paste the full callback address here."}
	if state.Credentials.PlanEnabled() {
		status.Authenticated = true
		status.Account = state.Credentials.Email
		status.Message = "Connected to ChatGPT. Your subscription limits apply; Hub API keys are not used."
	}
	if !status.Authenticated && (state.Credentials.AccessToken != "" || state.Credentials.RefreshToken != "") {
		status.Message = "Signed in, but ChatGPT plan usage was not authorized. Sign in again and allow plan usage to use your subscription."
	}
	if state.Pending != nil && time.Now().Before(state.Pending.ExpiresAt) {
		status.Pending = true
		status.CallbackURL = state.Pending.CallbackURL
		status.Message = "Complete ChatGPT sign-in, then paste the full callback address into Settings."
	}
	return status
}

func (s *Server) subscriptionStatus(ctx context.Context, provider string) (ai.SubscriptionStatus, error) {
	if provider == "anthropic" {
		return s.aiClient.ClaudeStatus(ctx), nil
	}
	s.subscriptionMu.Lock()
	defer s.subscriptionMu.Unlock()
	state, err := s.loadChatGPTState()
	if err != nil {
		return ai.SubscriptionStatus{}, err
	}
	return chatGPTStatus(state), nil
}

func (s *Server) aiSubscription(w http.ResponseWriter, r *http.Request) {
	provider := r.URL.Query().Get("provider")
	if !validAIProvider(provider) {
		s.fail(w, 400, "provider", "Choose OpenAI or Anthropic")
		return
	}
	if r.Method == http.MethodDelete {
		if provider != "openai" {
			s.fail(w, 400, "subscription_managed_externally", "Sign out of Claude Code on the Hub host to disconnect its subscription")
			return
		}
		// Always take these locks in config -> subscription order, matching
		// settings validation. Clearing local tokens cannot select API billing.
		s.configMu.Lock()
		defer s.configMu.Unlock()
		s.subscriptionMu.Lock()
		defer s.subscriptionMu.Unlock()
		state, err := s.loadChatGPTState()
		if err != nil {
			s.internal(w, err)
			return
		}
		revokeCtx, cancel := context.WithTimeout(r.Context(), 5*time.Second)
		revokeErr := s.chatGPT.Revoke(revokeCtx, state.Credentials)
		cancel()
		state = chatGPTState{Credentials: chatGPTRegistration(state.Credentials)}
		if err := s.saveChatGPTState(state); err != nil {
			s.internal(w, err)
			return
		}
		var cfg ai.Config
		if _, err := s.db.Get("config", "ai", &cfg); err != nil && !errors.Is(err, store.ErrNotFound) {
			s.internal(w, err)
			return
		} else if err == nil && cfg.Provider == "openai" && ai.NormalizeAuthMode(cfg.AuthMode) == ai.AuthSubscription {
			cfg.Enabled, cfg.MonitorEnabled, cfg.KeyConfigured = false, false, false
			if _, err := s.db.Put("config", "ai", cfg, -1); err != nil {
				s.internal(w, err)
				return
			}
		}
		status := chatGPTStatus(state)
		status.Message = "Disconnected locally. Your ChatGPT app registration is retained for the next sign-in."
		if revokeErr != nil {
			status.Message = "Disconnected locally. Remote token revocation could not be confirmed; you can remove this connection in your ChatGPT account settings."
		}
		s.stopChatGPTCallbackLocked()
		s.json(w, 200, status)
		return
	}
	status, err := s.subscriptionStatus(r.Context(), provider)
	if err != nil {
		s.internal(w, err)
		return
	}
	s.json(w, 200, status)
}

func (s *Server) aiSubscriptionLogin(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Provider    string `json:"provider"`
		RedirectURI string `json:"redirect_uri"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if in.Provider != "openai" {
		s.fail(w, 400, "provider", "ChatGPT uses browser sign-in; sign into Claude Code on the Hub host for Claude")
		return
	}
	if in.RedirectURI == "" {
		in.RedirectURI = chatGPTCallbackURL
	}
	if in.RedirectURI != chatGPTCallbackURL {
		s.fail(w, 400, "callback", "Use the displayed loopback callback address")
		return
	}
	s.subscriptionMu.Lock()
	defer s.subscriptionMu.Unlock()
	state, err := s.loadChatGPTState()
	if err != nil {
		s.internal(w, err)
		return
	}
	if state.Credentials.HostID == "" {
		state.Credentials.HostID, err = ai.NewChatGPTHostID()
		if err != nil {
			s.internal(w, err)
			return
		}
		if err = s.saveChatGPTState(state); err != nil {
			s.internal(w, err)
			return
		}
	}
	login, err := s.chatGPT.Begin(state.Credentials, in.RedirectURI)
	if err != nil {
		s.fail(w, 400, "ai_subscription", "ChatGPT sign-in could not be started")
		return
	}
	state.Pending = &login
	if err = s.saveChatGPTState(state); err != nil {
		s.internal(w, err)
		return
	}
	status := chatGPTStatus(state)
	if s.startChatGPTCallbackLocked(login.ExpiresAt) {
		status.Message = "Complete ChatGPT sign-in in the browser. On this computer it reconnects automatically; for a remote Hub, paste the full callback address here."
	} else {
		status.Message = "Complete ChatGPT sign-in, then paste the full callback address here. The local callback port is unavailable."
	}
	status.AuthorizationURL = login.AuthorizationURL
	s.json(w, 200, status)
}

func (s *Server) aiSubscriptionCallback(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Provider    string `json:"provider"`
		CallbackURL string `json:"callback_url"`
	}
	if !s.decode(w, r, &in) {
		return
	}
	if in.Provider != "openai" {
		s.fail(w, 400, "provider", "Choose OpenAI for this browser callback")
		return
	}
	if len(in.CallbackURL) > 16384 {
		s.fail(w, 400, "callback", "The callback address is too long")
		return
	}
	u, err := url.Parse(strings.TrimSpace(in.CallbackURL))
	if err != nil || u.User != nil || u.Fragment != "" || u.Scheme+"://"+u.Host+u.Path != chatGPTCallbackURL {
		s.fail(w, 400, "callback", "Paste the full callback address shown after ChatGPT sign-in")
		return
	}
	query, err := url.ParseQuery(u.RawQuery)
	if err != nil {
		s.fail(w, 400, "callback", "The callback address is invalid")
		return
	}
	status, failure := s.finishChatGPTCallback(r.Context(), query)
	if failure != nil {
		s.fail(w, failure.status, failure.code, failure.message)
		return
	}
	s.json(w, 200, status)
}

type chatGPTCallbackFailure struct {
	status        int
	code, message string
}

func (s *Server) finishChatGPTCallback(ctx context.Context, query url.Values) (ai.SubscriptionStatus, *chatGPTCallbackFailure) {
	s.subscriptionMu.Lock()
	defer s.subscriptionMu.Unlock()
	fail := func(status int, code, message string) (ai.SubscriptionStatus, *chatGPTCallbackFailure) {
		return ai.SubscriptionStatus{}, &chatGPTCallbackFailure{status: status, code: code, message: message}
	}
	state, err := s.loadChatGPTState()
	if err != nil {
		return fail(500, "internal_error", "The stored ChatGPT connection could not be read")
	}
	if state.Pending == nil {
		return fail(409, "ai_login_missing", "Start a new ChatGPT sign-in from Settings")
	}
	clientID, code, err := s.chatGPT.ValidateCallback(*state.Pending, query)
	if err != nil {
		return fail(400, "callback", "The sign-in callback is invalid or expired; start sign-in again")
	}
	// Persist a newly issued dynamic registration before exchanging its one-use
	// code. Failed exchanges and restarts must reuse the registered client.
	state.Credentials.ClientID = clientID
	login := *state.Pending
	state.Pending = nil
	if err = s.saveChatGPTState(state); err != nil {
		return fail(500, "internal_error", "The ChatGPT registration could not be saved")
	}
	credentials, err := s.chatGPT.Exchange(ctx, login, clientID, code)
	if err != nil {
		return fail(502, "ai_subscription", "ChatGPT sign-in could not be completed; start a new sign-in to retry")
	}
	state.Credentials = credentials
	if err = s.saveChatGPTState(state); err != nil {
		return fail(500, "internal_error", "The ChatGPT connection could not be saved")
	}
	// Shutdown waits for this callback response to finish if it is the current
	// loopback request. Neither a manual completion nor an automatic completion
	// leaves an unused callback listener running.
	s.stopChatGPTCallbackLocked()
	return chatGPTStatus(state), nil
}

func chatGPTRegistration(credentials ai.ChatGPTCredentials) ai.ChatGPTCredentials {
	return ai.ChatGPTCredentials{HostID: credentials.HostID, ClientID: credentials.ClientID, Subject: credentials.Subject, Email: credentials.Email}
}

func (s *Server) resumeChatGPTCallback() {
	s.subscriptionMu.Lock()
	defer s.subscriptionMu.Unlock()
	state, err := s.loadChatGPTState()
	if err == nil && state.Pending != nil && state.Pending.ExpiresAt.After(time.Now()) {
		s.startChatGPTCallbackLocked(state.Pending.ExpiresAt)
	}
}

func (s *Server) startChatGPTCallbackLocked(expiresAt time.Time) bool {
	if s.chatGPTCallback != nil {
		if s.chatGPTCallbackTimer != nil {
			s.chatGPTCallbackTimer.Stop()
		}
		s.setChatGPTCallbackTimerLocked(expiresAt)
		return true
	}
	if s.ctx.Err() != nil || !expiresAt.After(time.Now()) {
		return false
	}
	listener, err := net.Listen("tcp", "127.0.0.1:1455")
	if err != nil {
		return false
	}
	callback := &http.Server{Handler: http.HandlerFunc(s.chatGPTLoopbackCallback), ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 5 * time.Second, MaxHeaderBytes: 32 << 10,
		BaseContext: func(net.Listener) context.Context { return s.ctx }}
	s.chatGPTCallback = callback
	s.setChatGPTCallbackTimerLocked(expiresAt)
	go func() { _ = callback.Serve(listener) }()
	return true
}

func (s *Server) setChatGPTCallbackTimerLocked(expiresAt time.Time) {
	s.chatGPTCallbackGeneration++
	generation := s.chatGPTCallbackGeneration
	s.chatGPTCallbackTimer = time.AfterFunc(time.Until(expiresAt), func() {
		s.subscriptionMu.Lock()
		defer s.subscriptionMu.Unlock()
		if s.chatGPTCallbackGeneration == generation {
			s.stopChatGPTCallbackLocked()
		}
	})
}

func (s *Server) stopChatGPTCallbackLocked() {
	callback := s.chatGPTCallback
	s.chatGPTCallback = nil
	if s.chatGPTCallbackTimer != nil {
		s.chatGPTCallbackTimer.Stop()
		s.chatGPTCallbackTimer = nil
	}
	if callback != nil {
		s.wg.Add(1)
		go func() {
			defer s.wg.Done()
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			if callback.Shutdown(ctx) != nil {
				_ = callback.Close()
			}
		}()
	}
}

func (s *Server) stopChatGPTCallback() {
	s.subscriptionMu.Lock()
	defer s.subscriptionMu.Unlock()
	s.stopChatGPTCallbackLocked()
}

func (s *Server) chatGPTLoopbackCallback(w http.ResponseWriter, r *http.Request) {
	s.wg.Add(1)
	defer s.wg.Done()
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Referrer-Policy", "no-referrer")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'")
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	if r.Method != http.MethodGet || r.Host != "127.0.0.1:1455" || r.URL.Path != "/auth/callback" || len(r.URL.RawQuery) > 16384 {
		http.Error(w, "Invalid ChatGPT callback", 400)
		return
	}
	query, err := url.ParseQuery(r.URL.RawQuery)
	if err != nil {
		http.Error(w, "Invalid ChatGPT callback", 400)
		return
	}
	status, failure := s.finishChatGPTCallback(r.Context(), query)
	if failure != nil {
		http.Error(w, failure.message, failure.status)
		return
	}
	message := "ChatGPT is connected. Return to PicoBrew Hub Settings to choose a model and save. You may close this tab."
	if !status.Authenticated {
		message = "ChatGPT sign-in completed, but plan usage was not authorized. Return to PicoBrew Hub Settings and sign in again to allow plan usage."
	}
	_, _ = w.Write([]byte(message))
}

func (s *Server) chatGPTCredentials(ctx context.Context) (ai.ChatGPTCredentials, error) {
	s.subscriptionMu.Lock()
	defer s.subscriptionMu.Unlock()
	state, err := s.loadChatGPTState()
	if err != nil {
		return ai.ChatGPTCredentials{}, errors.New("the saved ChatGPT connection could not be read")
	}
	if !state.Credentials.PlanEnabled() {
		return ai.ChatGPTCredentials{}, errors.New("connect your ChatGPT subscription in Settings first")
	}
	if state.Credentials.NeedsRefresh() {
		credentials, err := s.chatGPT.Refresh(ctx, state.Credentials)
		if err != nil {
			if errors.Is(err, ai.ErrChatGPTReauth) {
				state.Credentials = chatGPTRegistration(state.Credentials)
				if saveErr := s.saveChatGPTState(state); saveErr != nil {
					return ai.ChatGPTCredentials{}, errors.New("the ChatGPT connection could not be updated")
				}
			}
			return ai.ChatGPTCredentials{}, errors.New("ChatGPT subscription renewal failed; check your connection or sign in again in Settings")
		}
		state.Credentials = credentials
		if err = s.saveChatGPTState(state); err != nil {
			return ai.ChatGPTCredentials{}, errors.New("the ChatGPT connection could not be saved")
		}
	}
	return state.Credentials, nil
}

func (s *Server) performChatGPT(ctx context.Context, cfg ai.Config, input ai.Request, data map[string]any) (ai.Result, error) {
	credentials, err := s.chatGPTCredentials(ctx)
	if err != nil {
		return ai.Result{}, err
	}
	return s.chatGPT.Assist(ctx, credentials, cfg, input, data)
}

func (s *Server) subscriptionModels(ctx context.Context, provider string) ([]ai.Model, error) {
	if provider == "anthropic" {
		return s.aiClient.ListClaudeModels(ctx)
	}
	credentials, err := s.chatGPTCredentials(ctx)
	if err != nil {
		return nil, err
	}
	return s.chatGPT.Models(ctx, credentials)
}
