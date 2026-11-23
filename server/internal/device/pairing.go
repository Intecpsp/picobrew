package device

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"net/http"
)

// PairingClient only exposes the two public pairing operations. All appliance
// control continues to use the authenticated adapter.
type PairingClient struct{ transport *pico }

func NewPairingClient(baseURL string) (*PairingClient, error) {
	p, err := newPicoTransport(baseURL, false)
	if err != nil {
		return nil, err
	}
	return &PairingClient{p}, nil
}

func PairingNonce() string {
	var b [32]byte
	if _, err := rand.Read(b[:]); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b[:])
}

func pairingFailure(err error) error {
	var remote *Error
	if errors.As(err, &remote) {
		switch remote.StatusCode {
		case 401, 403, 404, 405:
			return &Error{StatusCode: 409, Code: "pairing_unavailable", Message: "Pairing is unavailable or expired. Start again, or use the key file from USB setup for older firmware."}
		case 409, 423, 429:
			return &Error{StatusCode: 409, Code: "pairing_busy", Message: "Another pairing request is active. Wait for it to expire and try again."}
		}
	}
	return &Error{StatusCode: 502, Code: "pairing_failed", Message: "The Hub could not confirm pairing with this machine"}
}

func (c *PairingClient) Request(ctx context.Context, nonce string) error {
	if !apiKey.MatchString(nonce) {
		return invalid("Invalid pairing nonce")
	}
	var reply struct{ State, Code string }
	err := c.transport.request(ctx, http.MethodPost, "/api/v1/pairing/request", map[string]string{"nonce": nonce}, &reply, 202)
	if err != nil || reply.State != "pending" || reply.Code != nonce[:8] {
		return pairingFailure(err)
	}
	return nil
}

// Claim returns an empty key while the brewer is waiting for its owner.
func (c *PairingClient) Claim(ctx context.Context, nonce string) (string, error) {
	var reply struct {
		State string `json:"state"`
		Key   string `json:"api_key"`
	}
	err := c.transport.request(ctx, http.MethodPost, "/api/v1/pairing/claim", map[string]string{"nonce": nonce}, &reply, 200, 202)
	if err != nil {
		return "", pairingFailure(err)
	}
	if reply.State == "pending" && reply.Key == "" {
		return "", nil
	}
	if !apiKey.MatchString(reply.Key) {
		return "", pairingFailure(nil)
	}
	return reply.Key, nil
}

// WebsiteGrant authorizes one browser tab to claim the key directly from the
// brewer. Only the disposable nonce travels in the URL fragment.
func (p *pico) WebsiteGrant(ctx context.Context) (string, error) {
	if err := p.lock(ctx); err != nil {
		return "", err
	}
	defer p.unlock()
	nonce := PairingNonce()
	err := p.request(ctx, http.MethodPost, "/api/v1/pairing/grant", map[string]string{"nonce": nonce}, nil, 202)
	if err != nil {
		return "", pairingFailure(err)
	}
	u := *p.base
	u.Path = "/"
	u.Fragment = "pair=" + nonce
	return u.String(), nil
}
