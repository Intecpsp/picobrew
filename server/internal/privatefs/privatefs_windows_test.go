package privatefs

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"golang.org/x/sys/windows"
)

func setTestDACL(t *testing.T, path, sddl string) {
	t.Helper()
	sd, err := windows.SecurityDescriptorFromString(sddl)
	if err != nil {
		t.Fatal(err)
	}
	dacl, _, err := sd.DACL()
	if err != nil {
		t.Fatal(err)
	}
	if err := windows.SetNamedSecurityInfo(path, windows.SE_FILE_OBJECT,
		windows.DACL_SECURITY_INFORMATION|windows.PROTECTED_DACL_SECURITY_INFORMATION,
		nil, nil, dacl, nil); err != nil {
		t.Fatal(err)
	}
}

func TestWindowsPrivateOwnerACLAndChildInheritance(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private")
	if err := EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	user, err := currentSID()
	if err != nil {
		t.Fatal(err)
	}
	for _, path := range []string{dir, filepath.Join(dir, "key")} {
		if path != dir {
			f, err := OpenFile(path, os.O_RDWR|os.O_CREATE|os.O_EXCL)
			if err != nil {
				t.Fatal(err)
			}
			f.Close()
		}
		sd, err := windows.GetNamedSecurityInfo(path, windows.SE_FILE_OBJECT,
			windows.OWNER_SECURITY_INFORMATION|windows.DACL_SECURITY_INFORMATION)
		if err != nil {
			t.Fatal(err)
		}
		owner, _, err := sd.Owner()
		if err != nil || owner.String() != user {
			t.Fatalf("new object must belong to process user, got %v: %v", owner, err)
		}
		if err := checkACL(sd, user, path == dir); err != nil {
			t.Fatal(err)
		}
	}
	// SQLite creates its own sidecars. They must inherit private access even
	// when created without our explicit SECURITY_ATTRIBUTES.
	sidecar := filepath.Join(dir, "hub.sqlite-wal")
	if err := os.WriteFile(sidecar, []byte("synthetic WAL"), 0666); err != nil {
		t.Fatal(err)
	}
	if _, err := ReadFile(sidecar, 32); err != nil {
		t.Fatalf("private ACL was not inherited: %v", err)
	}
}

func TestWindowsRejectsUnsafeExistingACLWithoutChangingIt(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private")
	if err := EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	user, _ := currentSID()
	path := filepath.Join(dir, "key")
	f, err := OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_RDWR)
	if err != nil {
		t.Fatal(err)
	}
	f.Write([]byte("secret"))
	f.Close()
	setTestDACL(t, path, "D:P(A;;FA;;;"+user+")(A;;GR;;;WD)")
	if data, err := ReadFile(path, 32); err == nil || len(data) != 0 {
		t.Fatalf("read unsafe file: %q, %v", data, err)
	}
	setTestDACL(t, dir, "D:P(A;;FA;;;"+user+")(A;;GR;;;WD)")
	before, err := windows.GetNamedSecurityInfo(dir, windows.SE_FILE_OBJECT, windows.DACL_SECURITY_INFORMATION)
	if err != nil {
		t.Fatal(err)
	}
	if err := EnsureDir(dir); err == nil {
		t.Fatal("accepted a directory accessible to Everyone")
	}
	after, err := windows.GetNamedSecurityInfo(dir, windows.SE_FILE_OBJECT, windows.DACL_SECURITY_INFORMATION)
	if err != nil || before.String() != after.String() {
		t.Fatalf("existing DACL was modified: %v", err)
	}
}

func TestWindowsRejectsForeignOwnerAndNullDACL(t *testing.T) {
	user, _ := currentSID()
	for _, sddl := range []string{
		"O:WDD:P(A;;FA;;;" + user + ")",
		"O:" + user + "D:NO_ACCESS_CONTROL",
	} {
		sd, err := windows.SecurityDescriptorFromString(sddl)
		if err != nil {
			t.Fatal(err)
		}
		if err := checkACL(sd, user, false); err == nil {
			t.Fatalf("accepted unsafe native security descriptor %q", sddl)
		}
	}
}

func TestWindowsDirectoryMustProtectNewChildFiles(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "private")
	if err := EnsureDir(dir); err != nil {
		t.Fatal(err)
	}
	user, _ := currentSID()
	// Private on the directory itself, but no inheritable ACE for SQLite WAL.
	setTestDACL(t, dir, "D:P(A;;FA;;;"+user+")")
	if err := EnsureDir(dir); err == nil {
		t.Fatal("accepted a directory whose children get the token default DACL")
	}
}

func TestWindowsRejectsPathAliasesBeforeFileAccess(t *testing.T) {
	for _, component := range []string{"data.", "data ", "CON", "nul.txt", "COM1", "LPT²", "CONOUT$", "a*b", "a?b", "file:stream", "key\x00ignored", "control\x1f"} {
		if _, err := fullPath(filepath.Join(t.TempDir(), component, "hub.sqlite")); err == nil {
			t.Fatalf("accepted Windows path alias %q", component)
		}
	}
}

func TestWindowsRejectsDirectoryJunctionWithoutSymlinkPrivilege(t *testing.T) {
	base := t.TempDir()
	target := filepath.Join(base, "target")
	if err := EnsureDir(target); err != nil {
		t.Fatal(err)
	}
	junction := filepath.Join(base, "junction")
	// Directory junctions do not require Developer Mode or symlink privilege.
	if out, err := exec.Command("cmd", "/d", "/c", "mklink", "/J", junction, target).CombinedOutput(); err != nil {
		t.Fatalf("create test junction: %v: %s", err, out)
	}
	defer os.Remove(junction)
	if err := EnsureDir(junction); err == nil {
		t.Fatal("accepted a junction as the data directory")
	}
	if f, err := OpenFile(filepath.Join(junction, "key"), os.O_RDWR|os.O_CREATE|os.O_EXCL); err == nil {
		f.Close()
		t.Fatal("created a key through an intermediate junction")
	}
	if _, err := os.Stat(filepath.Join(target, "key")); !os.IsNotExist(err) {
		t.Fatalf("junction target was touched: %v", err)
	}
}
