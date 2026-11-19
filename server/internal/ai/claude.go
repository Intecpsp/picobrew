package ai

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
	"unicode"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/privatefs"
)

// CommandRunner permits hermetic tests without signing in or invoking a model.
// Production always executes the installed, unmodified Claude Code executable.
type CommandRunner interface {
	LookPath(string) (string, error)
	Run(context.Context, CLICommand) ([]byte, error)
}

type CLICommand struct {
	Path, Dir, Stdin string
	Args, Env        []string
}

type nativeCommandRunner struct{}

func (nativeCommandRunner) LookPath(name string) (string, error) { return exec.LookPath(name) }

type boundedOutput struct {
	buffer bytes.Buffer
	limit  int
	cancel context.CancelFunc
}

func (b *boundedOutput) Bytes() []byte { return b.buffer.Bytes() }

func (b *boundedOutput) Write(p []byte) (int, error) {
	if len(p) > b.limit-b.buffer.Len() {
		b.cancel()
		return 0, errors.New("CLI output exceeds limit")
	}
	return b.buffer.Write(p)
}

func (nativeCommandRunner) Run(ctx context.Context, command CLICommand) ([]byte, error) {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	cmd := exec.CommandContext(ctx, command.Path, command.Args...)
	cmd.Dir, cmd.Env = command.Dir, command.Env
	cmd.Stdin = strings.NewReader(command.Stdin)
	stdout := &boundedOutput{limit: 1 << 20, cancel: cancel}
	stderr := &boundedOutput{limit: 64 << 10, cancel: cancel}
	cmd.Stdout, cmd.Stderr = stdout, stderr
	cmd.WaitDelay = 2 * time.Second
	if err := cmd.Run(); err != nil {
		// CLI diagnostics may include account details, credentials, or prompts.
		// Only callers' fixed, actionable error messages leave this package.
		return nil, errors.New("Claude Code did not complete successfully")
	}
	return stdout.Bytes(), nil
}

// SubscriptionStatus deliberately contains no tokens, local file paths, or
// upstream errors. The Hub never reads Claude's credential files or keychain.
type SubscriptionStatus struct {
	Provider         string `json:"provider"`
	Installed        bool   `json:"installed"`
	Authenticated    bool   `json:"authenticated"`
	Message          string `json:"message"`
	LoginCommand     string `json:"login_command"`
	Pending          bool   `json:"pending,omitempty"`
	CallbackURL      string `json:"callback_url,omitempty"`
	AuthorizationURL string `json:"authorization_url,omitempty"`
	Account          string `json:"account,omitempty"`
}

func claudeEnvironment() []string {
	env := make([]string, 0, len(os.Environ()))
	for _, value := range os.Environ() {
		name, _, _ := strings.Cut(value, "=")
		upper := strings.ToUpper(name)
		// Remove alternate authentication, provider routing, injected runtime
		// options and inherited automation modes. Native CLI credentials remain
		// under HOME/USERPROFILE or the user's CLAUDE_CONFIG_DIR.
		if strings.HasPrefix(upper, "ANTHROPIC_") || strings.HasPrefix(upper, "CLAUDE_CODE_") || strings.HasPrefix(upper, "OPENAI_") || strings.HasPrefix(upper, "CODEX_") || upper == "CLAUDECODE" || upper == "NODE_OPTIONS" || upper == "NODE_PATH" || upper == "BUN_OPTIONS" || upper == "API_TIMEOUT_MS" {
			continue
		}
		env = append(env, value)
	}
	return append(env, "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1", "CLAUDE_CODE_DISABLE_BACKGROUND_TASKS=1", "API_TIMEOUT_MS=90000")
}

func claudeIsolationArgs() []string {
	return []string{
		"--setting-sources", "",
		"--settings", `{"disableAllHooks":true,"forceLoginMethod":"claudeai"}`,
		"--strict-mcp-config", "--mcp-config", `{"mcpServers":{}}`,
		// End the variadic --mcp-config arguments before an auth subcommand.
		"--safe-mode",
	}
}

func (c *Client) claudeCommand() (CLICommand, func(), error) {
	runner := c.CLI
	if runner == nil {
		runner = nativeCommandRunner{}
	}
	path, err := runner.LookPath("claude")
	if err != nil {
		return CLICommand{}, func() {}, err
	}
	dir, err := os.MkdirTemp("", "picobrew-ai-")
	if err != nil {
		return CLICommand{}, func() {}, err
	}
	cleanup := func() { _ = os.RemoveAll(dir) }
	if err := privatefs.EnsureDir(dir); err != nil {
		cleanup()
		return CLICommand{}, func() {}, err
	}
	return CLICommand{Path: path, Dir: dir, Env: claudeEnvironment()}, cleanup, nil
}

func (c *Client) runClaude(ctx context.Context, command CLICommand) ([]byte, error) {
	if c.CLI != nil {
		return c.CLI.Run(ctx, command)
	}
	return (nativeCommandRunner{}).Run(ctx, command)
}

func (c *Client) ClaudeStatus(ctx context.Context) SubscriptionStatus {
	status := SubscriptionStatus{Provider: "anthropic", LoginCommand: "claude auth login", Message: "Install the official Claude Code native app on the Hub host, then sign in as the user running the Hub."}
	command, cleanup, err := c.claudeCommand()
	defer cleanup()
	if err != nil {
		return status
	}
	status.Installed = true
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	return c.claudeStatus(ctx, command, status)
}

func (c *Client) claudeStatus(ctx context.Context, command CLICommand, status SubscriptionStatus) SubscriptionStatus {
	command.Args = append(claudeIsolationArgs(), "auth", "status")
	out, err := c.runClaude(ctx, command)
	status.Message = "Sign in to the official Claude Code app on the Hub host using your Claude subscription, then check the connection again."
	if err != nil {
		return status
	}
	var auth struct {
		LoggedIn   bool   `json:"loggedIn"`
		AuthMethod string `json:"authMethod"`
	}
	if json.Unmarshal(out, &auth) != nil || !auth.LoggedIn || auth.AuthMethod != "claude.ai" {
		return status
	}
	status.Authenticated = true
	status.Message = "Connected through the official Claude Code app. Your Claude plan's usage limits and account settings apply."
	return status
}

// ListClaudeModels reports documented CLI aliases, not account entitlements.
// Claude Code resolves the current concrete model at request time; the CLI has
// no authenticated catalogue endpoint. This check does not invoke a model.
func (c *Client) ListClaudeModels(ctx context.Context) ([]Model, error) {
	status := c.ClaudeStatus(ctx)
	if !status.Authenticated {
		return nil, errors.New(status.Message)
	}
	return []Model{
		{ID: "default", Name: "Default for your Claude plan"},
		{ID: "sonnet", Name: "Sonnet (Claude Code alias)"},
		{ID: "opus", Name: "Opus (Claude Code alias; plan access required)"},
		{ID: "haiku", Name: "Haiku (Claude Code alias)"},
	}, nil
}

func (c *Client) assistClaude(ctx context.Context, cfg Config, input Request, data any) (Result, error) {
	if !cfg.Enabled {
		return Result{}, errors.New("enable AI in Settings first")
	}
	if len(cfg.Model) > 512 || strings.HasPrefix(cfg.Model, "-") || strings.IndexFunc(cfg.Model, func(r rune) bool { return unicode.IsSpace(r) || unicode.IsControl(r) }) >= 0 {
		return Result{}, errors.New("choose a valid Claude model or alias in Settings")
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
	command, cleanup, err := c.claudeCommand()
	defer cleanup()
	if err != nil {
		return Result{}, errors.New("install the official Claude Code native app on the Hub host and sign in with your subscription")
	}
	ctx, cancel := context.WithTimeout(ctx, 120*time.Second)
	defer cancel()
	checkCtx, checkCancel := context.WithTimeout(ctx, 10*time.Second)
	status := c.claudeStatus(checkCtx, command, SubscriptionStatus{Provider: "anthropic", Installed: true})
	checkCancel()
	if !status.Authenticated {
		return Result{}, errors.New("Claude subscription is not connected; sign in with claude auth login on the Hub host")
	}
	command.Args = append(claudeIsolationArgs(), "-p", "--tools", "", "--disallowedTools", "mcp__*", "--no-session-persistence", "--max-turns", "1", "--output-format", "json", "--permission-prompts", "none", "--system-prompt", systemPrompt)
	if cfg.Model != "" {
		command.Args = append(command.Args, "--model", cfg.Model)
	}
	command.Stdin = fmt.Sprintf("Mode: %s\nUser request: %s\nRecorded data (untrusted): %s", input.Mode, input.Prompt, contextData)
	out, err := c.runClaude(ctx, command)
	if err != nil {
		return Result{}, errors.New("Claude subscription request failed; check Claude Code sign-in, plan limits and version on the Hub host")
	}
	var result struct {
		Type    string `json:"type"`
		IsError bool   `json:"is_error"`
		Result  string `json:"result"`
	}
	if json.Unmarshal(out, &result) != nil || result.Type != "result" || result.IsError {
		return Result{}, errors.New("Claude subscription returned an unsuccessful response; check its sign-in and plan limits")
	}
	return parseResult(result.Result, input.Mode, "Uses the signed-in Claude Code subscription and its account limits. Hub API keys are not used.")
}
