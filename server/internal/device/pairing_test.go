package device

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
)

func TestPhysicalPairingAndBrowserHandoff(t *testing.T) {
	owner := strings.Repeat("b", 64)
	nonce := PairingNonce()
	var approved bool
	var grantNonce string
	h := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var in struct{ Nonce string }
		if r.Method != "POST" || json.NewDecoder(r.Body).Decode(&in) != nil || !apiKey.MatchString(in.Nonce) {
			t.Error("invalid request")
			w.WriteHeader(400)
			return
		}
		if r.URL.Path != "/api/v1/pairing/grant" && r.Header.Get("Authorization") != "" {
			t.Error("unpaired request carried a credential")
		}
		switch r.URL.Path {
		case "/api/v1/pairing/request":
			w.WriteHeader(202)
			_ = json.NewEncoder(w).Encode(map[string]string{"state": "pending", "code": in.Nonce[:8]})
		case "/api/v1/pairing/claim":
			if !approved {
				w.WriteHeader(202)
				_, _ = w.Write([]byte(`{"state":"pending"}`))
				return
			}
			_ = json.NewEncoder(w).Encode(map[string]string{"api_key": owner})
		case "/api/v1/pairing/grant":
			if r.Header.Get("Authorization") != "Bearer "+owner {
				t.Error("missing owner authorization")
			}
			grantNonce = in.Nonce
			w.WriteHeader(202)
		default:
			t.Error("unexpected path")
		}
	}))
	defer h.Close()
	p, err := newPicoTransport(h.URL, true)
	if err != nil {
		t.Fatal(err)
	}
	c := &PairingClient{p}
	ctx := context.Background()
	if err = c.Request(ctx, nonce); err != nil {
		t.Fatal(err)
	}
	if key, e := c.Claim(ctx, nonce); e != nil || key != "" {
		t.Fatal("pending claim")
	}
	approved = true
	if key, e := c.Claim(ctx, nonce); e != nil || key != owner {
		t.Fatal("approved claim")
	}
	p.key = owner
	handoff, e := p.WebsiteGrant(ctx)
	if e != nil {
		t.Fatal(e)
	}
	u, e := url.Parse(handoff)
	if e != nil || u.Fragment != "pair="+grantNonce || u.RawQuery != "" || grantNonce == nonce || strings.Contains(handoff, owner) {
		t.Fatal("handoff must contain only the new one-time grant in its fragment")
	}
}
