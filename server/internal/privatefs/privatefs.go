// Package privatefs creates the Hub's local secrets with operating-system
// access controls. Windows uses protected DACLs, not the Unix mode emulation.
package privatefs

import (
	"io"
	"os"
)

// ReadFile reads at most limit+1 bytes, allowing callers to reject oversized
// secrets without allocating for an arbitrary file. It rejects nonregular files.
func ReadFile(path string, limit int64) ([]byte, error) {
	f, err := OpenFile(path, os.O_RDONLY)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return io.ReadAll(io.LimitReader(f, limit+1))
}
