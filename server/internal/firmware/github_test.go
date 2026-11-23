package firmware

import (
	"bytes"
	"context"
	"errors"
	"io"
	"net/http"
	"net/netip"
	"strings"
	"testing"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/firmware/firmwaretest"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func (fn roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return fn(r) }

func githubResponse(status int, data []byte) *http.Response {
	return &http.Response{StatusCode: status, Header: make(http.Header), Body: io.NopCloser(bytes.NewReader(data)), ContentLength: int64(len(data))}
}

func TestRenamedFirmwareRepository(t *testing.T) {
	for _, input := range []string{"jeffreydwalter/picobrew-pico-pro-firmware", "https://github.com/JeffreyDWalter/picobrew-pico-pro-firmware/"} {
		settings, err := NormalizeSettings(Settings{Repository: input, Ref: "master"})
		if err != nil || settings.Repository != "jeffreydwalter/picobrew" || settings.Ref != "master" {
			t.Fatalf("old source %q: %+v, %v", input, settings, err)
		}
	}
}

func TestRepositoryAndRefValidation(t *testing.T) {
	for _, input := range []string{"owner/repo", "Owner/Repo", "https://github.com/Owner/Repo", " https://github.com/owner/repo/ "} {
		got, err := NormalizeRepository(input)
		if err != nil || got != "owner/repo" {
			t.Fatalf("%q => %q, %v", input, got, err)
		}
	}
	for _, input := range []string{
		"", "../repo", "-owner/repo", "owner/../repo", "owner/repo/extra", "owner\\repo",
		"http://github.com/owner/repo", "https://evil.example/owner/repo", "https://github.com.evil/owner/repo",
		"https://github.com:443/owner/repo", "https://x@github.com/owner/repo", "https://github.com/owner/repo?x=1",
		"https://github.com/owner/repo?", "https://github.com/owner/repo#fragment", "https://github.com/owner%2frepo",
		"owner/repo\nother", "own--er/repo",
	} {
		if got, err := NormalizeRepository(input); err == nil {
			t.Errorf("accepted %q as %q", input, got)
		}
	}
	for _, ref := range []string{"", "master", "feature/packages", "v1.2.0", strings.Repeat("a", 40)} {
		if _, err := NormalizeSettings(Settings{Repository: "owner/repo", Ref: ref}); err != nil {
			t.Errorf("valid ref %q: %v", ref, err)
		}
	}
	for _, ref := range []string{"../secret", "x..y", "a\nb", "refs/heads/../x", "a//b", "x.lock", strings.Repeat("x", 201), "https://evil"} {
		if _, err := NormalizeSettings(Settings{Repository: "owner/repo", Ref: ref}); err == nil {
			t.Errorf("invalid ref accepted: %q", ref)
		}
	}
}

func TestGitHubCatalogPinsOneCommitAndImportUsesSameValidator(t *testing.T) {
	const commit = "0123456789abcdef0123456789abcdef01234567"
	var requests []string
	sets := map[string]map[string][]byte{"pico-s": firmwaretest.Files("pico-s"), "pico-pro": firmwaretest.Files("pico-pro")}
	client := &GitHub{HTTP: &http.Client{Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
		requests = append(requests, r.URL.String())
		if r.Method != "GET" || r.Header.Get("Authorization") != "" || r.Header.Get("Cookie") != "" {
			t.Fatal("unexpected method or credentials")
		}
		switch r.URL.String() {
		case "https://api.github.com/repos/owner/repo":
			return githubResponse(200, []byte(`{"default_branch":"new-default","private":false}`)), nil
		case "https://api.github.com/repos/owner/repo/commits/new-default":
			return githubResponse(200, []byte(`{"sha":"`+commit+`"}`)), nil
		}
		for model, files := range sets {
			for name, data := range files {
				if r.URL.String() == rawPath("owner/repo", commit, model, name) {
					return githubResponse(200, data), nil
				}
			}
		}
		if strings.Contains(r.URL.Path, "/firmware/pico-c/") {
			return githubResponse(http.StatusNotFound, nil), nil
		}
		t.Fatalf("unexpected URL %s", r.URL)
		return nil, errors.New("unexpected request")
	})}}
	catalog, err := client.Catalog(context.Background(), Settings{Repository: "owner/repo"})
	if err != nil || catalog.Commit != commit || catalog.Ref != "" || len(catalog.Packages) != 2 {
		t.Fatalf("catalog: %+v, %v", catalog, err)
	}
	if len(requests) != 6 {
		t.Fatalf("unexpected catalog requests: %v", requests)
	}
	archive, candidate, err := client.Import(context.Background(), "owner/repo", commit, "pico-pro")
	if err != nil || candidate.Model != "pico-pro" {
		t.Fatalf("import: %+v, %v", candidate, err)
	}
	validated, err := ValidateArchive(archive)
	if err != nil || validated != candidate || len(requests) != 14 {
		t.Fatalf("import was not fully validated: %+v, %v; requests=%d", validated, err, len(requests))
	}
	sets["pico-pro"]["pico-pro-standalone.bin"][20] ^= 1
	if _, _, err := client.Import(context.Background(), "owner/repo", commit, "pico-pro"); err == nil {
		t.Fatal("GitHub corruption bypassed the upload validator")
	}
}

func TestGitHubBoundedErrorsAndNoRedirects(t *testing.T) {
	for name, response := range map[string]*http.Response{
		"not found":  githubResponse(404, []byte("private server diagnostics")),
		"rate limit": githubResponse(403, nil),
		"redirect":   {StatusCode: 302, Header: http.Header{"Location": []string{"http://127.0.0.1/private"}}, Body: io.NopCloser(strings.NewReader(""))},
		"large body": githubResponse(200, bytes.Repeat([]byte("x"), 100)),
	} {
		t.Run(name, func(t *testing.T) {
			calls := 0
			g := NewGitHub()
			defer g.Close()
			g.HTTP.Transport = roundTripFunc(func(*http.Request) (*http.Response, error) {
				calls++
				return response, nil
			})
			_, err := g.get(context.Background(), "https://api.github.com/repos/owner/repo", 16)
			if err == nil || calls != 1 || strings.Contains(err.Error(), "private server diagnostics") {
				t.Fatalf("unbounded or unsafe response: calls=%d error=%v", calls, err)
			}
		})
	}
	t.Run("stream exceeds bound", func(t *testing.T) {
		g := &GitHub{HTTP: &http.Client{Transport: roundTripFunc(func(*http.Request) (*http.Response, error) {
			response := githubResponse(200, bytes.Repeat([]byte("x"), 100))
			response.ContentLength = -1
			return response, nil
		})}}
		if _, err := g.get(context.Background(), "https://api.github.com/repos/owner/repo", 16); err == nil {
			t.Fatal("chunked oversized response accepted")
		}
	})
	t.Run("foreign targets never requested", func(t *testing.T) {
		g := &GitHub{HTTP: &http.Client{Transport: roundTripFunc(func(*http.Request) (*http.Response, error) {
			t.Fatal("unapproved target was requested")
			return nil, nil
		})}}
		for _, target := range []string{"http://api.github.com/repos/o/r", "https://evil.example/file", "https://user@api.github.com/repos/o/r", "https://api.github.com:8443/file", "https://api.github.com/file?url=evil"} {
			if _, err := g.get(context.Background(), target, 16); err == nil {
				t.Fatalf("unapproved target accepted: %s", target)
			}
		}
	})
	t.Run("cancelled request", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		cancel()
		g := &GitHub{HTTP: &http.Client{Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) { return nil, r.Context().Err() })}}
		if _, err := g.Catalog(ctx, Settings{Repository: "owner/repo"}); !errors.Is(err, context.Canceled) {
			t.Fatalf("cancellation not preserved: %v", err)
		}
	})
}

func TestGitHubRejectsPrivateOrReservedAddresses(t *testing.T) {
	for _, address := range []string{"127.0.0.1", "10.0.0.1", "172.16.0.1", "192.168.1.1", "169.254.169.254", "100.64.0.1", "0.0.0.0", "198.18.0.1", "192.0.2.1", "224.0.0.1", "::1", "fc00::1", "fe80::1", "::ffff:127.0.0.1", "64:ff9b::7f00:1", "2001:db8::1", "2002:7f00:1::1"} {
		if publicAddress(netip.MustParseAddr(address)) {
			t.Errorf("nonpublic address allowed: %s", address)
		}
	}
	for _, address := range []string{"140.82.112.3", "185.199.108.133", "2606:50c0:8000::154"} {
		if !publicAddress(netip.MustParseAddr(address)) {
			t.Errorf("public address refused: %s", address)
		}
	}
	g := NewGitHub()
	defer g.Close()
	transport := g.HTTP.Transport.(*http.Transport)
	if transport.Proxy != nil || !transport.DisableCompression || transport.MaxResponseHeaderBytes <= 0 || g.HTTP.Timeout <= 0 {
		t.Fatal("GitHub transport lost proxy/size/time protections")
	}
}
