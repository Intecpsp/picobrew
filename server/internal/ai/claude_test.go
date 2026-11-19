package ai

import (
	"context"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"reflect"
	"strings"
	"testing"
)

type fakeClaudeRunner struct {
	missing  bool
	auth     string
	response string
	fail     bool
	commands []CLICommand
}

func (r *fakeClaudeRunner) LookPath(name string) (string, error) {
	if r.missing {
		return "", errors.New("not installed")
	}
	return "test-claude", nil
}

func (r *fakeClaudeRunner) Run(_ context.Context, command CLICommand) ([]byte, error) {
	r.commands = append(r.commands, command)
	if r.fail {
		return nil, errors.New("private-account-and-token")
	}
	if len(command.Args) >= 2 && command.Args[len(command.Args)-2] == "auth" {
		return []byte(r.auth), nil
	}
	return []byte(r.response), nil
}

func TestClaudeSubscriptionIsIsolatedFromAPIAndTools(t *testing.T) {
	t.Setenv("ANTHROPIC_API_KEY", "private-api-key")
	t.Setenv("ANTHROPIC_BASE_URL", "https://untrusted.invalid")
	t.Setenv("CLAUDE_CODE_OAUTH_TOKEN", "inherited-token")
	t.Setenv("CLAUDE_CODE_USE_BEDROCK", "1")
	t.Setenv("OPENAI_API_KEY", "other-private-key")
	t.Setenv("NODE_OPTIONS", "--require=/untrusted.js")
	t.Setenv("CLAUDE_CONFIG_DIR", t.TempDir())
	runner := &fakeClaudeRunner{
		auth:     `{"loggedIn":true,"authMethod":"claude.ai"}`,
		response: `{"type":"result","is_error":false,"result":"The observed temperature is stable."}`,
	}
	c := New()
	c.CLI = runner
	apiCalls := 0
	provider := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { apiCalls++ }))
	defer provider.Close()
	c.AnthropicURL = provider.URL
	result, err := c.Assist(context.Background(), Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true, Model: "sonnet"}, "saved-api-key", Request{Mode: "live", Prompt: "Review temperatures"}, map[string]any{"temperature": 65})
	if err != nil || !strings.Contains(result.Text, "stable") {
		t.Fatalf("subscription result: %+v %v", result, err)
	}
	if apiCalls != 0 || len(runner.commands) != 2 {
		t.Fatalf("unexpected routes: API=%d CLI=%d", apiCalls, len(runner.commands))
	}
	command := runner.commands[1]
	args := strings.Join(command.Args, "\n")
	for _, required := range []string{"--safe-mode", "--setting-sources\n", `"disableAllHooks":true`, `"forceLoginMethod":"claudeai"`, "--strict-mcp-config", `{"mcpServers":{}}`, "--tools\n\n", "--disallowedTools\nmcp__*", "--no-session-persistence", "--max-turns\n1", "--permission-prompts\nnone", "--model\nsonnet"} {
		if !strings.Contains(args, required) {
			t.Errorf("missing isolation argument %q", required)
		}
	}
	if got := runner.commands[0].Args; !reflect.DeepEqual(got[len(got)-3:], []string{"--safe-mode", "auth", "status"}) {
		t.Fatal("auth subcommand can be consumed by variadic MCP arguments")
	}
	if strings.Contains(args, "--bare") || strings.Contains(args, "Review temperatures") || strings.Contains(args, "saved-api-key") {
		t.Fatal("wrong auth mode or private prompt in command arguments")
	}
	if !strings.Contains(command.Stdin, "Review temperatures") || !strings.Contains(command.Stdin, `"temperature":65`) {
		t.Fatal("missing prompt or brew context on stdin")
	}
	env := strings.Join(command.Env, "\n")
	for _, secret := range []string{"private-api-key", "inherited-token", "untrusted.invalid", "other-private-key", "NODE_OPTIONS=", "CLAUDE_CODE_USE_BEDROCK="} {
		if strings.Contains(env, secret) {
			t.Errorf("inherited alternate authentication or runtime injection: %s", secret)
		}
	}
	if !strings.Contains(env, "CLAUDE_CONFIG_DIR=") {
		t.Fatal("lost native CLI credential directory")
	}
	if _, err := os.Stat(command.Dir); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("isolated temporary directory was not removed")
	}
}

func TestClaudeSubscriptionRejectsOtherAuthenticationWithoutAPIFallback(t *testing.T) {
	for _, auth := range []string{
		`{"loggedIn":false,"authMethod":"claude.ai"}`,
		`{"loggedIn":true,"authMethod":"api_key"}`,
		`{"loggedIn":true,"authMethod":"api_key_helper"}`,
		`{"loggedIn":true,"authMethod":"oauth_token"}`,
		`{"loggedIn":true,"authMethod":"third_party"}`,
		`not-json-private-token`,
	} {
		t.Run(auth, func(t *testing.T) {
			runner := &fakeClaudeRunner{auth: auth}
			c := New()
			c.CLI = runner
			_, err := c.Assist(context.Background(), Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true, Model: "sonnet"}, "saved-api-key", Request{}, nil)
			if err == nil || len(runner.commands) != 1 || strings.Contains(err.Error(), "private-token") {
				t.Fatalf("wrong authentication accepted or leaked: %v (%d calls)", err, len(runner.commands))
			}
		})
	}
}

func TestClaudeFailuresAndMalformedResultsAreRedacted(t *testing.T) {
	for _, response := range []string{
		`{"type":"result","is_error":true,"result":"private-provider-error"}`,
		`{"type":"error","result":"private-provider-error"}`,
		`{"type":"result","is_error":false,"result":""}`,
		`private-provider-error`,
	} {
		c := New()
		c.CLI = &fakeClaudeRunner{auth: `{"loggedIn":true,"authMethod":"claude.ai"}`, response: response}
		_, err := c.Assist(context.Background(), Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true}, "", Request{}, nil)
		if err == nil || strings.Contains(err.Error(), "private-provider-error") {
			t.Fatalf("unredacted or accepted failed response: %v", err)
		}
	}
	c := New()
	c.CLI = &fakeClaudeRunner{fail: true}
	status := c.ClaudeStatus(context.Background())
	if status.Authenticated || strings.Contains(status.Message, "private-account") {
		t.Fatalf("unredacted failed auth status: %+v", status)
	}
}

func TestClaudeModelAliasesCheckConnectionWithoutInference(t *testing.T) {
	runner := &fakeClaudeRunner{auth: `{"loggedIn":true,"authMethod":"claude.ai"}`}
	c := New()
	c.CLI = runner
	models, err := c.ListClaudeModels(context.Background())
	if err != nil || len(models) != 4 || len(runner.commands) != 1 || models[0].ID != "default" {
		t.Fatalf("models: %+v %v (%d commands)", models, err, len(runner.commands))
	}
	runner.auth = `{"loggedIn":true,"authMethod":"api_key"}`
	if _, err := c.ListClaudeModels(context.Background()); err == nil {
		t.Fatal("API key login accepted for subscription model aliases")
	}
}

func TestClaudeDisabledAndInvalidInputDoNotInvokeCLI(t *testing.T) {
	for _, tc := range []struct {
		cfg Config
		in  Request
	}{
		{Config{Provider: "anthropic", AuthMode: AuthSubscription}, Request{}},
		{Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true, Model: "--dangerous"}, Request{}},
		{Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true, Model: "sonnet\n--bad"}, Request{}},
		{Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true}, Request{Prompt: strings.Repeat("x", 16001)}},
		{Config{Provider: "openai", AuthMode: AuthSubscription, Enabled: true}, Request{}},
		{Config{Provider: "anthropic", AuthMode: "unrecognized", Enabled: true}, Request{}},
	} {
		runner := &fakeClaudeRunner{}
		c := New()
		c.CLI = runner
		_, err := c.Assist(context.Background(), tc.cfg, "saved-key", tc.in, nil)
		if err == nil || len(runner.commands) != 0 {
			t.Fatalf("invalid input invoked CLI: %+v %v", tc, err)
		}
	}
}

func TestClaudeMissingBinaryAndOutputLimit(t *testing.T) {
	c := New()
	c.CLI = &fakeClaudeRunner{missing: true}
	status := c.ClaudeStatus(context.Background())
	if status.Installed || status.Authenticated || status.LoginCommand != "claude auth login" {
		t.Fatalf("wrong missing-binary status: %+v", status)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	out := &boundedOutput{limit: 4, cancel: cancel}
	if n, err := out.Write([]byte("1234")); n != 4 || err != nil {
		t.Fatal(n, err)
	}
	if _, err := out.Write([]byte("5")); err == nil || ctx.Err() == nil || !reflect.DeepEqual(out.Bytes(), []byte("1234")) {
		t.Fatal("unbounded output or failed cancellation")
	}
}

func TestClaudeRecipeUsesExistingStructuredDraftParser(t *testing.T) {
	c := New()
	c.CLI = &fakeClaudeRunner{auth: `{"loggedIn":true,"authMethod":"claude.ai"}`, response: `{"type":"result","is_error":false,"result":"{\"text\":\"Review this draft\",\"recipe\":{\"name\":\"Pale ale\"}}"}`}
	result, err := c.Assist(context.Background(), Config{Provider: "anthropic", AuthMode: AuthSubscription, Enabled: true}, "", Request{Mode: "recipe"}, nil)
	if err != nil || result.Text != "Review this draft" || !strings.Contains(string(result.Recipe), "Pale ale") {
		t.Fatalf("recipe parse: %+v %v", result, err)
	}
}

func TestClaudeOutputLimitCannotBeBypassedByCommandPipe(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	out := &boundedOutput{limit: 4, cancel: cancel}
	// os/exec drains its pipe with io.Copy. Neither the source's WriterTo nor
	// an embedded bytes.Buffer.ReadFrom may bypass the bounded Write method.
	reader := struct{ io.Reader }{strings.NewReader(strings.Repeat("x", 100))}
	if _, err := io.Copy(out, reader); err == nil || ctx.Err() == nil || len(out.Bytes()) > 4 {
		t.Fatal("command output bypassed the size bound")
	}
}
