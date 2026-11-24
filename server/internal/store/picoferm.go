package store

import (
	"database/sql"
	"encoding/json"
	"errors"

	"github.com/jeffreydwalter/picobrew-pico-pro-firmware/server/internal/domain"
)

var ErrAccessoryModel = errors.New("store: choose a registered PicoFerm and a physical brewer")

// SetPicoFermBrewer validates both registrations and the caller's link
// revision in one transaction, including when another Hub has this DB open.
func (s *Store) SetPicoFermBrewer(id, brewerID string, revision int64) (domain.Device, error) {
	tx, err := s.db.Begin()
	if err != nil {
		return domain.Device{}, err
	}
	defer tx.Rollback()
	load := func(id string) (domain.Device, int64, error) {
		var device domain.Device
		var version int64
		var payload []byte
		err := tx.QueryRow("SELECT version,payload FROM records WHERE kind='device' AND id=?", id).Scan(&version, &payload)
		if errors.Is(err, sql.ErrNoRows) {
			err = ErrNotFound
		}
		if err == nil {
			err = json.Unmarshal(payload, &device)
		}
		return device, version, err
	}
	ferm, version, err := load(id)
	if err != nil {
		return domain.Device{}, err
	}
	if !ferm.IsPicoFerm() {
		return domain.Device{}, ErrAccessoryModel
	}
	if ferm.BrewerRevision != revision {
		return domain.Device{}, ErrConflict
	}
	if brewerID != "" {
		brewer, _, err := load(brewerID)
		if err != nil {
			return domain.Device{}, err
		}
		if !brewer.IsBrewer() {
			return domain.Device{}, ErrAccessoryModel
		}
	}
	if ferm.BrewerID != brewerID {
		ferm.BrewerID = brewerID
		ferm.BrewerRevision++
		payload, err := json.Marshal(ferm)
		if err != nil {
			return domain.Device{}, err
		}
		if _, err := putRecord(tx, "device", id, payload, version); err != nil {
			return domain.Device{}, err
		}
	}
	return ferm, tx.Commit()
}
