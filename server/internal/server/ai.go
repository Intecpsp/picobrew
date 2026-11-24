package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"sort"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/ai"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/catalog"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/recipe"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

type adviceRecord = domain.AIAdvice

func invalidProviderKey(key string) bool {
	return len(key) > 1024 || strings.IndexFunc(key, func(r rune) bool {
		return unicode.IsControl(r) || unicode.IsSpace(r)
	}) >= 0
}

func (s *Server) aiSettings(w http.ResponseWriter, r *http.Request) {
	s.configMu.Lock()
	defer s.configMu.Unlock()
	cfg := ai.Config{Provider: "openai", AuthMode: ai.AuthAPIKey}
	if _, err := s.db.Get("config", "ai", &cfg); err != nil && !errors.Is(err, store.ErrNotFound) {
		s.internal(w, err)
		return
	}
	cfg.AuthMode = ai.NormalizeAuthMode(cfg.AuthMode)
	if r.Method == http.MethodPut {
		var in struct {
			ai.Config
			APIKey string `json:"api_key"`
		}
		if !s.decode(w, r, &in) {
			return
		}
		in.Model = strings.TrimSpace(in.Model)
		in.AuthMode = ai.NormalizeAuthMode(in.AuthMode)
		if !validAIProvider(in.Provider) {
			s.fail(w, 400, "provider", "Choose OpenAI, Anthropic, or Google Gemini")
			return
		}
		if !validAIAuthMode(in.Provider, in.AuthMode) {
			s.fail(w, 400, "auth_mode", "Choose a connection method supported by this provider")
			return
		}
		if in.Provider == "gemini" && in.Model != "" && !ai.ValidGeminiModelID(in.Model) {
			s.fail(w, 400, "model", "Choose a Gemini model or enter its model ID")
			return
		}
		if len(in.Model) > 512 || invalidProviderKey(in.APIKey) {
			s.fail(w, 400, "config", "Invalid model or API key")
			return
		}
		in.KeyConfigured = false
		if in.AuthMode == ai.AuthSubscription {
			if in.APIKey != "" {
				s.fail(w, 400, "auth_mode", "API keys cannot be supplied in subscription mode")
				return
			}
			if in.Enabled {
				status, err := s.subscriptionStatus(r.Context(), in.Provider)
				if err != nil {
					s.fail(w, 502, "ai_subscription", "Could not check the subscription connection")
					return
				}
				if !status.Authenticated || in.Model == "" {
					s.fail(w, 409, "ai_subscription_missing", "Connect your subscription and choose a model before enabling AI")
					return
				}
			}
		} else {
			key := in.APIKey
			if key == "" {
				var err error
				key, err = s.readSecret("ai:" + in.Provider)
				if err != nil && !errors.Is(err, store.ErrNotFound) {
					s.internal(w, err)
					return
				}
			}
			in.KeyConfigured = key != ""
			if in.Enabled && (!in.KeyConfigured || in.Model == "") {
				s.fail(w, 400, "config", "Set an API key and model before enabling AI")
				return
			}
		}
		cfg = in.Config
		var encrypted []byte
		if in.APIKey != "" {
			encrypted = s.vault.seal("ai:"+in.Provider, in.APIKey)
		}
		if _, err := s.db.PutWithSecret("config", "ai", cfg, -1, "ai:"+in.Provider, encrypted); err != nil {
			s.internal(w, err)
			return
		}
	}
	cfg.KeyConfigured = false
	if cfg.AuthMode == ai.AuthAPIKey {
		key, err := s.readSecret("ai:" + cfg.Provider)
		if err != nil && !errors.Is(err, store.ErrNotFound) {
			s.internal(w, err)
			return
		}
		cfg.KeyConfigured = key != ""
	}
	s.json(w, 200, cfg)
}

func validAIProvider(provider string) bool {
	return provider == "gemini" || aiSubscriptionProvider(provider)
}

func aiSubscriptionProvider(provider string) bool {
	return provider == "openai" || provider == "anthropic"
}

func validAIAuthMode(provider, mode string) bool {
	return mode == ai.AuthAPIKey || (mode == ai.AuthSubscription && aiSubscriptionProvider(provider))
}

func (s *Server) aiConfiguration() (ai.Config, string, error) {
	s.configMu.Lock()
	defer s.configMu.Unlock()
	var cfg ai.Config
	if _, err := s.db.Get("config", "ai", &cfg); err != nil {
		return cfg, "", errors.New("configure AI in Settings first")
	}
	cfg.AuthMode = ai.NormalizeAuthMode(cfg.AuthMode)
	if !validAIProvider(cfg.Provider) || !validAIAuthMode(cfg.Provider, cfg.AuthMode) || cfg.Model == "" || !cfg.Enabled {
		return cfg, "", errors.New("connect an AI provider, choose a model and enable AI in Settings first")
	}
	if cfg.AuthMode == ai.AuthSubscription {
		// Never read or return an API key for subscription requests, including
		// expired or disconnected subscriptions. The subscription client fails
		// explicitly; it cannot silently change the billing method.
		return cfg, "", nil
	}
	key, err := s.readSecret("ai:" + cfg.Provider)
	if err != nil || key == "" {
		return cfg, "", errors.New("enable AI with a provider API key and model in Settings first")
	}
	return cfg, key, nil
}

func (s *Server) aiContext(input ai.Request) (map[string]any, error) {
	profiles, err := s.calibratedProfiles()
	if err != nil {
		return nil, err
	}
	data := map[string]any{"available_profiles": profiles}
	if input.CatalogID != "" {
		if input.Mode != "recipe" || input.RecipeID != "" {
			return nil, errors.New("choose one recipe source in recipe mode")
		}
		archive, err := catalog.Open()
		if err != nil {
			return nil, err
		}
		item, err := archive.Detail(input.CatalogID)
		if errors.Is(err, catalog.ErrNotFound) {
			return nil, store.ErrNotFound
		}
		if err != nil {
			return nil, err
		}
		data["catalog_reference"] = item.Summary
		data["source_warnings"] = item.Warnings
		if item.Recipe != nil {
			data["recipe"] = item.Recipe
		} else if item.NativeProgram != nil {
			data["native_program"] = item.NativeProgram
		} else {
			data["source_notes"] = item.Notes
		}
	}
	if input.RecipeID != "" {
		var v domain.Recipe
		if _, err := s.db.Get("recipe", input.RecipeID, &v); err != nil {
			return nil, err
		}
		data["recipe"] = v
	}
	if input.BrewID != "" {
		v, err := s.brewDetail(input.BrewID)
		if err != nil {
			return nil, err
		}
		// Past model output remains in the journal/export, but is not fed back
		// as if it were measured telemetry for another analysis.
		v.AIAdvice = nil
		if len(v.Samples) > 600 {
			stride := (len(v.Samples) + 598) / 599
			reduced := make([]domain.Sample, 0, 600)
			for i := 0; i < len(v.Samples)-1; i += stride {
				reduced = append(reduced, v.Samples[i])
			}
			reduced = append(reduced, v.Samples[len(v.Samples)-1])
			v.Samples = reduced
			data["sampling_note"] = "Time-series context was downsampled; full-fidelity export remains available."
		}
		data["brew"] = v
	}
	if input.DeviceID != "" && input.Mode != "analysis" {
		found := false
		for _, v := range s.fleet() {
			if v.ID == input.DeviceID {
				data["device"] = v
				found = true
				break
			}
		}
		if !found {
			return nil, store.ErrNotFound
		}
	}
	return data, nil
}

func (s *Server) performAI(ctx context.Context, cfg ai.Config, key string, input ai.Request, data map[string]any) (ai.Result, error) {
	var result ai.Result
	var err error
	if ai.NormalizeAuthMode(cfg.AuthMode) == ai.AuthSubscription && cfg.Provider == "openai" {
		result, err = s.performChatGPT(ctx, cfg, input, data)
	} else {
		result, err = s.aiClient.Assist(ctx, cfg, key, input, data)
	}
	if err != nil {
		return result, err
	}
	if len(result.Recipe) > 0 {
		profiles, _ := data["available_profiles"].([]domain.Profile)
		machine, _ := data["device"].(domain.DeviceView)
		if err := validateGeneratedRecipe(result.Recipe, machine.Model, profiles); err != nil {
			return ai.Result{}, fmt.Errorf("generated recipe failed validation: %w", err)
		}
	}
	return result, nil
}

// Only return ingredient drafts that the same compiler used by Send can turn
// into a supported brewing program. Device commands and native programs are
// outside the recipe assistant's output format.
func validateGeneratedRecipe(raw json.RawMessage, target string, profiles []domain.Profile) error {
	if value := strings.TrimSpace(string(raw)); value == "" || value[0] != '{' {
		return errors.New("expected one recipe object")
	}
	recipes, err := recipe.Import(raw, "json")
	if err != nil {
		return err
	}
	draft := recipes[0]
	if recipe.IsNative(draft) {
		return errors.New("expected an ingredient recipe")
	}
	var grist, yeast, boiled bool
	for _, item := range draft.Ingredients {
		grist = grist || item.Kind == "fermentable"
		yeast = yeast || item.Kind == "yeast"
	}
	for _, step := range draft.Steps {
		boiled = boiled || ((step.Kind == "boil" || step.Kind == "hop") && step.Minutes > 0 && step.TargetC > 0)
	}
	if !grist || !boiled || (!yeast && strings.TrimSpace(recipe.SourceYeast(draft)) == "") {
		return errors.New("include fermentables, a named yeast and a timed, heated boil")
	}
	if target == "" {
		target = draft.SourceModel
	}
	if len(profiles) == 0 {
		profiles = recipe.Profiles()
	}
	reason := "choose a supported brewing model"
	for _, profile := range profiles {
		if target != "" && !profileMatchesModel(profile.ID, target) {
			continue
		}
		plan, err := recipe.Translate(draft, profile, 0)
		if err != nil {
			return err
		}
		if plan.MachineRecipe != nil && len(plan.DeploymentBlockers) == 0 {
			return nil
		}
		if len(plan.DeploymentBlockers) > 0 {
			reason = strings.Join(plan.DeploymentBlockers, "; ")
		}
	}
	return errors.New(reason)
}

func (s *Server) recordAdvice(input ai.Request, result ai.Result, err error) {
	record := adviceRecord{ID: randomID(), CreatedAt: time.Now().UTC(), DeviceID: input.DeviceID, BrewID: input.BrewID, Mode: input.Mode, Status: "complete", Text: result.Text, Recipe: result.Recipe, Limitations: result.Limitations}
	if len(record.Text) > 65536 {
		record.Text = record.Text[:65536]
		for !utf8.ValidString(record.Text) {
			record.Text = record.Text[:len(record.Text)-1]
		}
		record.Limitations = append(append([]string(nil), record.Limitations...), "Stored advice was limited to 64 KiB.")
	}
	if err != nil {
		record.Status = "error"
		record.Error = safeMessage(err.Error())
	}
	if _, e := s.db.Put("ai-advice", record.ID, record, 0); e != nil {
		s.cfg.Logger.Error("Could not save AI advice", "error", e)
		return
	}
	// Keep bounded advice with each brew, independent of unrelated recipe
	// generation. Repeated provider errors must not evict useful brew advice.
	limit := 500
	if record.BrewID != "" {
		limit = 200
		if record.Status == "error" {
			limit = 20
		}
	}
	rows, e := s.db.List("ai-advice")
	if e != nil || len(rows) <= limit {
		return
	}
	records := make([]adviceRecord, 0, len(rows))
	for _, raw := range rows {
		var v adviceRecord
		if json.Unmarshal(raw, &v) == nil && v.BrewID == record.BrewID && (record.BrewID == "" || v.Status == record.Status) {
			records = append(records, v)
		}
	}
	sort.Slice(records, func(i, j int) bool { return records[i].CreatedAt.After(records[j].CreatedAt) })
	if len(records) <= limit {
		return
	}
	for _, v := range records[limit:] {
		_ = s.db.Delete("ai-advice", v.ID)
	}
}

func (s *Server) adviceForBrew(id string) ([]domain.AIAdvice, error) {
	rows, err := s.db.List("ai-advice")
	if err != nil {
		return nil, err
	}
	out := []domain.AIAdvice{}
	for _, raw := range rows {
		var v domain.AIAdvice
		if err := json.Unmarshal(raw, &v); err != nil {
			return nil, err
		}
		if v.BrewID == id {
			out = append(out, v)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].CreatedAt.After(out[j].CreatedAt) })
	return out, nil
}

func (s *Server) aiAssist(w http.ResponseWriter, r *http.Request) {
	var input ai.Request
	if !s.decode(w, r, &input) {
		return
	}
	if input.Mode != "recipe" && input.Mode != "live" && input.Mode != "analysis" {
		s.fail(w, 400, "mode", "Choose recipe, live, or analysis")
		return
	}
	if input.CatalogID != "" && (input.Mode != "recipe" || input.RecipeID != "") {
		s.fail(w, 400, "recipe_source", "Choose one recipe source in recipe mode")
		return
	}
	if input.Mode == "live" && (input.DeviceID == "" || input.BrewID == "") {
		s.fail(w, 400, "brew", "Choose the exact current machine and recording brew for live advice")
		return
	}
	if input.Mode == "analysis" && input.BrewID == "" {
		s.fail(w, 400, "brew", "Choose a recorded brew to analyze")
		return
	}
	if len(input.Prompt) > 16000 {
		s.fail(w, 400, "prompt", "Prompts must not exceed 16000 characters")
		return
	}
	cfg, key, err := s.aiConfiguration()
	if err != nil {
		s.fail(w, 409, "ai_not_configured", err.Error())
		return
	}
	var data map[string]any
	if input.Mode == "live" {
		s.mu.RLock()
		d := s.devices[input.DeviceID]
		s.mu.RUnlock()
		if d == nil {
			s.fail(w, 404, "device", "Machine not found")
			return
		}
		data, err = s.liveAIContext(r.Context(), d, input)
		if err != nil {
			s.deviceError(w, err)
			return
		}
	} else {
		if input.Mode == "analysis" {
			var brew domain.Brew
			if _, err = s.db.Get("brew", input.BrewID, &brew); err != nil {
				s.storeError(w, err)
				return
			}
			if input.DeviceID != "" && input.DeviceID != brew.DeviceID {
				s.fail(w, 409, "brew_mismatch", "The selected brew belongs to a different machine")
				return
			}
			input.DeviceID = brew.DeviceID
		}
		data, err = s.aiContext(input)
	}
	if err != nil {
		s.storeError(w, err)
		return
	}
	if !s.aiMu.TryLock() {
		s.fail(w, 429, "ai_busy", "An AI request is already in progress. Try again after it finishes.")
		return
	}
	defer s.aiMu.Unlock()
	result, err := s.performAI(r.Context(), cfg, key, input, data)
	s.recordAdvice(input, result, err)
	if err != nil {
		s.fail(w, 502, "ai_provider", err.Error())
		return
	}
	s.json(w, 200, result)
}

func (s *Server) aiAdvice(w http.ResponseWriter, r *http.Request) {
	mode := r.URL.Query().Get("mode")
	if mode != "" && mode != "recipe" && mode != "live" && mode != "analysis" {
		s.fail(w, 400, "mode", "Choose recipe, live, or analysis")
		return
	}
	rows, err := s.db.List("ai-advice")
	if err != nil {
		s.internal(w, err)
		return
	}
	out := make([]adviceRecord, 0, len(rows))
	for _, raw := range rows {
		var v adviceRecord
		if json.Unmarshal(raw, &v) == nil && (r.URL.Query().Get("device_id") == "" || r.URL.Query().Get("device_id") == v.DeviceID) && (r.URL.Query().Get("brew_id") == "" || r.URL.Query().Get("brew_id") == v.BrewID) && (mode == "" || mode == v.Mode) {
			out = append(out, v)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].CreatedAt.After(out[j].CreatedAt) })
	if len(out) > 500 {
		out = out[:500]
	}
	s.json(w, 200, out)
}

func (s *Server) monitorAI(ctx context.Context) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			s.monitorTick(ctx)
		}
	}
}

func (s *Server) monitorTick(ctx context.Context) {
	cfg, key, err := s.aiConfiguration()
	if err != nil {
		return
	}
	for _, v := range s.fleet() {
		if ctx.Err() != nil {
			return
		}
		if !v.Enabled || !v.Online {
			continue
		}
		s.monitorDeviceAI(ctx, v.ID, cfg, key)
	}
}

func (s *Server) monitorDeviceAI(ctx context.Context, deviceID string, cfg ai.Config, key string) {
	s.mu.RLock()
	d := s.devices[deviceID]
	s.mu.RUnlock()
	if d == nil {
		return
	}
	d.commandMu.Lock()
	record, err := s.loadAIMonitor(deviceID)
	if err != nil || (record.State != "active" && record.State != "pending") {
		d.commandMu.Unlock()
		return
	}
	observed, err := s.observeAIBrew(ctx, d)
	if err == nil {
		record, err = s.reconcileAIMonitor(record, observed)
	}
	if err != nil || record.State != "active" || time.Since(record.LastReviewAt) < 5*time.Minute {
		d.commandMu.Unlock()
		return
	}
	if !s.aiMu.TryLock() {
		d.commandMu.Unlock()
		return
	}
	defer s.aiMu.Unlock()
	record.LastReviewAt = time.Now().UTC()
	if err := s.saveAIMonitor(record); err != nil {
		d.commandMu.Unlock()
		return
	}
	input := ai.Request{Mode: "live", DeviceID: deviceID, BrewID: record.BrewID, Prompt: "Review this exact brew's recent sensor readings and phases. Identify meaningful temperature deviations, telemetry gaps or stalled progress. Give concise observations and actions the brewer can review. If data is insufficient, say so. Do not issue or imply machine commands."}
	d.commandMu.Unlock()
	data, err := s.liveAIContext(ctx, d, input)
	// Opt-out or a new start may occur while samples are downloaded. Do not
	// begin a provider request after that consent has been withdrawn/replaced.
	d.commandMu.Lock()
	current, consentErr := s.loadAIMonitor(deviceID)
	d.commandMu.Unlock()
	if consentErr != nil || current.State != "active" || current.BrewID != record.BrewID || current.DeviceUID != record.DeviceUID {
		return
	}
	var result ai.Result
	if err == nil {
		result, err = s.performAI(ctx, cfg, key, input, data)
	}
	s.recordAdvice(input, result, err)
}
