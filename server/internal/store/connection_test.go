package store

import (
	"database/sql"
	"errors"
	"os"
	"path/filepath"
	"testing"

	"modernc.org/sqlite"
	sqlite3 "modernc.org/sqlite/lib"
)

func TestEveryConnectionKeepsDatabaseSettingsAndFilename(t *testing.T) {
	path := filepath.Join(t.TempDir(), "private", "hub #%.sqlite")
	s, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	// Reconnect for each query to check that these are connection settings,
	// including the options that SQLite does not persist in the database file.
	s.db.SetMaxIdleConns(0)
	for pragma, want := range map[string]string{
		"busy_timeout": "5000", "foreign_keys": "1", "journal_mode": "wal", "synchronous": "2",
	} {
		var got string
		if err := s.db.QueryRow("PRAGMA " + pragma).Scan(&got); err != nil || got != want {
			t.Errorf("%s = %q, want %q (%v)", pragma, got, want, err)
		}
	}
	var sequence int
	var name, actual string
	if err := s.db.QueryRow("PRAGMA database_list").Scan(&sequence, &name, &actual); err != nil {
		t.Fatal(err)
	}
	wantFile, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	actualFile, err := os.Stat(actual)
	if err != nil || name != "main" || !os.SameFile(wantFile, actualFile) {
		t.Fatalf("SQLite opened %q (%q), wanted %q: %v", actual, name, path, err)
	}
}

func TestTransactionsAcquireWriteLockBeforeFirstStatement(t *testing.T) {
	path := filepath.Join(t.TempDir(), "private", "transaction.sqlite")
	s, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	tx, err := s.db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	defer tx.Rollback()
	other, err := sql.Open("sqlite", path+"?_busy_timeout=1")
	if err != nil {
		t.Fatal(err)
	}
	defer other.Close()
	_, err = other.Exec("BEGIN IMMEDIATE")
	var busy *sqlite.Error
	if !errors.As(err, &busy) || busy.Code()&0xff != sqlite3.SQLITE_BUSY {
		t.Fatalf("a competing writer was not blocked before the first statement: %v", err)
	}
	if err := tx.Rollback(); err != nil {
		t.Fatal(err)
	}
	if _, err := other.Exec("BEGIN IMMEDIATE"); err != nil {
		t.Fatalf("rollback did not release the write lock: %v", err)
	}
	if _, err := other.Exec("ROLLBACK"); err != nil {
		t.Fatal(err)
	}
}
