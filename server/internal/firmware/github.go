package firmware

import (
	"context"
	"errors"
	"io"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"regexp"
	"strings"
	"time"
)

var (
	ownerPattern = regexp.MustCompile(`^[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?$`)
	repoPattern  = regexp.MustCompile(`^[A-Za-z0-9_.-]{1,100}$`)
)

func NormalizeRepository(value string) (string, error) {
	value = strings.TrimSpace(value)
	if strings.HasPrefix(value, "https://") {
		u, err := url.Parse(value)
		if err != nil || u.Scheme != "https" || u.Host != "github.com" || u.User != nil || u.ForceQuery ||
			u.RawQuery != "" || u.Fragment != "" || u.RawPath != "" {
			return "", &Error{Code: "firmware_repository", Message: "Use a public GitHub owner/repository or its exact https://github.com/owner/repository URL"}
		}
		value = strings.TrimPrefix(strings.TrimSuffix(u.Path, "/"), "/")
	}
	parts := strings.Split(value, "/")
	if len(parts) != 2 || !ownerPattern.MatchString(parts[0]) || strings.Contains(parts[0], "--") ||
		!repoPattern.MatchString(parts[1]) || parts[1] == "." || parts[1] == ".." {
		return "", &Error{Code: "firmware_repository", Message: "Use a public GitHub owner/repository"}
	}
	return strings.ToLower(value), nil
}

func NormalizeSettings(in Settings) (Settings, error) {
	var err error
	in.Repository, err = NormalizeRepository(in.Repository)
	if err != nil {
		return Settings{}, err
	}
	in.Ref = strings.TrimSpace(in.Ref)
	if !validRef(in.Ref) {
		return Settings{}, &Error{Code: "firmware_ref", Message: "Use a valid GitHub branch, tag or full commit, or leave the ref empty for the default branch"}
	}
	return in, nil
}

func validRef(ref string) bool {
	if ref == "" {
		return true
	}
	if len(ref) > 200 || strings.ContainsAny(ref, " ~^:?*[\\\x7f") ||
		strings.Contains(ref, "..") || strings.Contains(ref, "@{") || strings.Contains(ref, "//") ||
		strings.HasPrefix(ref, "/") || strings.HasSuffix(ref, "/") || strings.HasSuffix(ref, ".") {
		return false
	}
	for _, ch := range ref {
		if ch < 33 || ch > 126 {
			return false
		}
	}
	for _, part := range strings.Split(ref, "/") {
		if strings.HasPrefix(part, ".") || strings.HasSuffix(part, ".lock") {
			return false
		}
	}
	return true
}

// GitHub reads only fixed public API/raw-content URLs. HTTP is injectable for
// tests; application configuration cannot change its host or transport.
type GitHub struct{ HTTP *http.Client }

func NewGitHub() *GitHub {
	dialer := &net.Dialer{Timeout: 5 * time.Second}
	transport := http.DefaultTransport.(*http.Transport).Clone()
	transport.Proxy = nil
	transport.DisableCompression = true
	transport.TLSHandshakeTimeout = 5 * time.Second
	transport.ResponseHeaderTimeout = 10 * time.Second
	transport.MaxResponseHeaderBytes = 32 << 10
	transport.MaxConnsPerHost = 2
	transport.MaxIdleConnsPerHost = 2
	transport.IdleConnTimeout = 30 * time.Second
	transport.DialContext = func(ctx context.Context, network, address string) (net.Conn, error) {
		host, port, err := net.SplitHostPort(address)
		if err != nil || port != "443" || (host != "api.github.com" && host != "raw.githubusercontent.com") {
			return nil, errors.New("unapproved firmware download host")
		}
		addresses, err := net.DefaultResolver.LookupNetIP(ctx, "ip", host)
		if err != nil || len(addresses) == 0 {
			return nil, errors.New("GitHub lookup failed")
		}
		// Reject the whole answer if any address is local/reserved, rather than
		// letting a fallback dial contact a private endpoint after a public one.
		for _, ip := range addresses {
			if !publicAddress(ip) {
				return nil, errors.New("GitHub resolved to a nonpublic address")
			}
		}
		for _, ip := range addresses {
			conn, err := dialer.DialContext(ctx, "tcp", net.JoinHostPort(ip.String(), port))
			if err == nil {
				return conn, nil
			}
			if ctx.Err() != nil {
				return nil, ctx.Err()
			}
		}
		return nil, errors.New("GitHub connection failed")
	}
	return &GitHub{HTTP: &http.Client{
		Transport: transport,
		Timeout:   15 * time.Second,
		CheckRedirect: func(*http.Request, []*http.Request) error {
			// Commit-pinned raw files and canonical API paths require no
			// redirects. A renamed repository can be saved under its new name.
			return http.ErrUseLastResponse
		},
	}}
}

var reservedPrefixes = []netip.Prefix{
	netip.MustParsePrefix("0.0.0.0/8"),
	netip.MustParsePrefix("100.64.0.0/10"),
	netip.MustParsePrefix("192.0.0.0/24"),
	netip.MustParsePrefix("192.0.2.0/24"),
	netip.MustParsePrefix("192.88.99.0/24"),
	netip.MustParsePrefix("198.18.0.0/15"),
	netip.MustParsePrefix("198.51.100.0/24"),
	netip.MustParsePrefix("203.0.113.0/24"),
	netip.MustParsePrefix("240.0.0.0/4"),
	netip.MustParsePrefix("2001::/23"),
	netip.MustParsePrefix("2001:db8::/32"),
	netip.MustParsePrefix("2002::/16"),
}

func publicAddress(ip netip.Addr) bool {
	ip = ip.Unmap()
	if !ip.IsValid() || !ip.IsGlobalUnicast() || ip.IsPrivate() || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
		return false
	}
	if ip.Is6() && !netip.MustParsePrefix("2000::/3").Contains(ip) {
		return false
	}
	for _, prefix := range reservedPrefixes {
		if prefix.Contains(ip) {
			return false
		}
	}
	return true
}

func (g *GitHub) Close() {
	if g != nil && g.HTTP != nil {
		g.HTTP.CloseIdleConnections()
	}
}

func (g *GitHub) get(ctx context.Context, endpoint string, limit int) ([]byte, error) {
	unavailable := func() error {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		return &Error{Code: "firmware_github_unavailable", Message: "GitHub could not be reached or returned an incomplete response. Try again."}
	}
	u, err := url.Parse(endpoint)
	if err != nil || u.Scheme != "https" || u.User != nil || u.RawQuery != "" || u.Fragment != "" ||
		(u.Host != "api.github.com" && u.Host != "raw.githubusercontent.com") {
		return nil, &Error{Code: "firmware_github_unavailable", Message: "The firmware download address is not permitted"}
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint, nil)
	if err != nil || g == nil || g.HTTP == nil {
		return nil, unavailable()
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("X-GitHub-Api-Version", "2022-11-28")
	req.Header.Set("User-Agent", "PicoBrew-Hub")
	resp, err := g.HTTP.Do(req)
	if err != nil {
		return nil, unavailable()
	}
	defer resp.Body.Close()
	switch resp.StatusCode {
	case http.StatusNotFound:
		return nil, &Error{Code: "firmware_github_not_found", Message: "The public repository, ref or firmware files were not found on GitHub"}
	case http.StatusForbidden, http.StatusTooManyRequests:
		return nil, &Error{Code: "firmware_github_rate_limit", Message: "GitHub refused this public request or its rate limit was reached. Try again later."}
	case http.StatusOK:
	default:
		return nil, unavailable()
	}
	if resp.ContentLength > int64(limit) {
		return nil, &Error{Code: "firmware_github_size", Message: "A GitHub firmware response exceeds its size limit"}
	}
	data, err := io.ReadAll(io.LimitReader(resp.Body, int64(limit)+1))
	if err != nil {
		return nil, unavailable()
	}
	if len(data) > limit {
		return nil, &Error{Code: "firmware_github_size", Message: "A GitHub firmware response exceeds its size limit"}
	}
	return data, nil
}

func (g *GitHub) apiJSON(ctx context.Context, suffix string, dst any) error {
	data, err := g.get(ctx, "https://api.github.com/repos/"+suffix, 2<<20)
	if err != nil {
		return err
	}
	if decodeJSON(data, dst, false) != nil {
		return &Error{Code: "firmware_github_unavailable", Message: "GitHub returned invalid repository metadata"}
	}
	return nil
}

func (g *GitHub) resolve(ctx context.Context, settings Settings) (string, error) {
	ref := settings.Ref
	if ref == "" {
		var repo struct {
			DefaultBranch string `json:"default_branch"`
			Private       bool   `json:"private"`
		}
		if err := g.apiJSON(ctx, settings.Repository, &repo); err != nil {
			return "", err
		}
		if repo.Private || repo.DefaultBranch == "" || !validRef(repo.DefaultBranch) {
			return "", &Error{Code: "firmware_github_not_found", Message: "Choose a public GitHub repository with a valid default branch"}
		}
		ref = repo.DefaultBranch
	}
	var commit struct {
		SHA string `json:"sha"`
	}
	if err := g.apiJSON(ctx, settings.Repository+"/commits/"+url.PathEscape(ref), &commit); err != nil {
		return "", err
	}
	if !ValidCommit(commit.SHA) || (ValidCommit(ref) && ref != commit.SHA) {
		return "", &Error{Code: "firmware_github_unavailable", Message: "GitHub did not return a valid pinned commit"}
	}
	return commit.SHA, nil
}

func rawPath(repository, commit, model, name string, display ...string) string {
	root := "/firmware/" + modelFamily(model) + "/modified/firmware/"
	base := "https://raw.githubusercontent.com/" + repository + "/" + commit + root
	if strings.HasPrefix(name, "web/") {
		return base + name
	}
	variant := ""
	if model == "pico-c" && len(display) == 1 {
		variant = display[0] + "/"
	}
	return base + "standalone/" + model + "/" + variant + name
}

// Catalog resolves the saved ref once. Every report is then read at that same
// commit; its metadata is only a candidate until Import validates all files.
func (g *GitHub) Catalog(ctx context.Context, settings Settings) (Catalog, error) {
	settings, err := NormalizeSettings(settings)
	if err != nil {
		return Catalog{}, err
	}
	ctx, cancel := context.WithTimeout(ctx, 45*time.Second)
	defer cancel()
	commit, err := g.resolve(ctx, settings)
	if err != nil {
		return Catalog{}, err
	}
	out := Catalog{Repository: settings.Repository, Ref: settings.Ref, Commit: commit, Packages: []Candidate{}}
	for _, selection := range []struct{ model, display string }{{"pico-s", ""}, {"pico-pro", ""}, {"pico-c", "ssd1325"}, {"pico-c", "ssd1309"}} {
		model := selection.model
		data, err := g.get(ctx, rawPath(settings.Repository, commit, model, "firmware-report.json", selection.display), MaxReportBytes)
		if err != nil {
			var failure *Error
			if errors.As(err, &failure) && failure.Code == "firmware_github_not_found" {
				continue // Public forks may provide only one supported model.
			}
			return Catalog{}, err
		}
		r, err := parseReport(data)
		if err != nil {
			return Catalog{}, err
		}
		if r.Model != model || r.DisplayVariant != selection.display {
			return Catalog{}, invalid("A GitHub report declares a different model or display variant from its package path")
		}
		out.Packages = append(out.Packages, candidate(r))
	}
	return out, nil
}

// Import trusts only repository/commit/model selectors. Version, hash, paths
// and provenance are derived from the retrieved bytes and fixed repository
// layout. An upload and a GitHub import pass through the same final validator.
func (g *GitHub) Import(ctx context.Context, repository, commit, model string, display ...string) ([]byte, Candidate, error) {
	var empty Candidate
	repository, err := NormalizeRepository(repository)
	if err != nil {
		return nil, empty, err
	}
	variant := ""
	if len(display) == 1 {
		variant = display[0]
	}
	if !ValidCommit(commit) || !ValidModel(model) || len(display) > 1 || !ValidVariant(model, variant) {
		return nil, empty, &Error{Code: "firmware_github_selection", Message: "Choose a supported model, an explicit C display variant and a full 40-character GitHub commit"}
	}
	ctx, cancel := context.WithTimeout(ctx, 60*time.Second)
	defer cancel()
	files := make(map[string][]byte, 8)
	for _, name := range archiveNames(model) {
		data, err := g.get(ctx, rawPath(repository, commit, model, name, variant), fileLimit(name))
		if err != nil {
			return nil, empty, err
		}
		files[name] = data
		// Fail early on a mismatched report, before fetching larger files.
		if name == "firmware-report.json" {
			r, err := parseReport(data)
			if err != nil {
				return nil, empty, err
			}
			if r.Model != model || r.DisplayVariant != variant {
				return nil, empty, invalid("The GitHub firmware report does not match the selected model and display variant")
			}
		}
	}
	archive, err := buildArchive(files)
	if err != nil {
		return nil, empty, err
	}
	metadata, err := ValidateArchive(archive)
	if err != nil {
		return nil, empty, err
	}
	return archive, metadata, nil
}
