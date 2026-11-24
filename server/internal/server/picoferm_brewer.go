package server

import (
	"errors"
	"net/http"

	"github.com/go-chi/chi/v5"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/store"
)

type picoFermBrewerLink struct {
	BrewerID string `json:"brewer_id"`
	Revision int64  `json:"revision"`
}

func (s *Server) picoFermBrewer(w http.ResponseWriter, r *http.Request) {
	id := chi.URLParam(r, "id")
	if r.Method == http.MethodGet {
		var saved domain.Device
		if _, err := s.db.Get("device", id, &saved); err != nil {
			s.storeError(w, err)
		} else if !saved.IsPicoFerm() {
			s.fail(w, 400, "picoferm_required", "Choose a registered PicoFerm.")
		} else {
			s.json(w, 200, picoFermBrewerLink{saved.BrewerID, saved.BrewerRevision})
		}
		return
	}
	var input struct {
		BrewerID *string `json:"brewer_id"`
		Revision *int64  `json:"revision"`
	}
	if !s.decode(w, r, &input) {
		return
	}
	if input.BrewerID == nil || input.Revision == nil || *input.Revision < 0 {
		s.fail(w, 400, "brewer_link", "Send a brewer ID (empty to unlink) and the current link revision.")
		return
	}
	s.deviceMu.Lock()
	defer s.deviceMu.Unlock()
	d, ok := s.lookupRuntime(w, r)
	if !ok {
		return
	}
	d.mu.Lock()
	saved, err := s.db.SetPicoFermBrewer(id, *input.BrewerID, *input.Revision)
	if err == nil {
		d.view.BrewerID, d.view.BrewerRevision = saved.BrewerID, saved.BrewerRevision
	}
	d.mu.Unlock()
	if errors.Is(err, store.ErrAccessoryModel) {
		s.fail(w, 400, "brewer_required", "Choose a registered PicoFerm and a physical brewer.")
		return
	}
	if err != nil {
		s.storeError(w, err)
		return
	}
	s.json(w, 200, picoFermBrewerLink{saved.BrewerID, saved.BrewerRevision})
}

// Called after the corresponding transaction commits, with s.mu held. The
// caller may already hold the removed/reclassified machine's lock.
func (s *Server) clearBrewerLinksLocked(id string) {
	for otherID, d := range s.devices {
		if otherID == id {
			continue
		}
		d.mu.Lock()
		if d.view.BrewerID == id {
			d.view.BrewerID = ""
			d.view.BrewerRevision++
		}
		d.mu.Unlock()
	}
}
