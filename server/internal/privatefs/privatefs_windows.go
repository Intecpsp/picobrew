package privatefs

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"unsafe"

	"golang.org/x/sys/windows"
)

func currentSID() (string, error) {
	user, err := windows.GetCurrentProcessToken().GetTokenUser()
	if err != nil {
		return "", err
	}
	return user.User.Sid.String(), nil
}

// The operating system and local administrators remain trusted on Windows;
// no ordinary other user or group may own or have access to a private object.
func trustedSID(sid, user string) bool {
	return sid == user || sid == "S-1-5-18" || sid == "S-1-5-32-544"
}

func checkACL(sd *windows.SECURITY_DESCRIPTOR, user string, directory bool) error {
	owner, _, err := sd.Owner()
	if err != nil || owner == nil || !trustedSID(owner.String(), user) {
		return errors.New("owner is not the current user, SYSTEM or Administrators")
	}
	dacl, _, err := sd.DACL()
	if err != nil || dacl == nil {
		return errors.New("a private DACL is required")
	}
	fileInheritance := false
	for i := uint32(0); i < uint32(dacl.AceCount); i++ {
		var ace *windows.ACCESS_ALLOWED_ACE
		if err := windows.GetAce(dacl, i, &ace); err != nil {
			return err
		}
		switch ace.Header.AceType {
		case windows.ACCESS_DENIED_ACE_TYPE:
			continue // A deny ACE cannot grant another user access.
		case windows.ACCESS_ALLOWED_ACE_TYPE:
			sid := (*windows.SID)(unsafe.Pointer(&ace.SidStart)).String()
			// An inherit-only CREATOR OWNER entry grants access to the owner
			// of future children, never to unrelated users of this directory.
			creatorOwner := directory && sid == "S-1-3-0" && ace.Header.AceFlags&windows.INHERIT_ONLY_ACE != 0
			if ace.Mask != 0 && !trustedSID(sid, user) && !creatorOwner {
				return fmt.Errorf("DACL grants access to another user or group (%s)", sid)
			}
			if ace.Mask != 0 && ace.Header.AceFlags&windows.OBJECT_INHERIT_ACE != 0 {
				fileInheritance = true
			}
		default:
			return fmt.Errorf("unsupported DACL entry type %d", ace.Header.AceType)
		}
	}
	if directory && !fileInheritance {
		return errors.New("DACL must give newly created files inheritable private access; apply private permissions to this folder and its files")
	}
	return nil
}

func checkHandle(h windows.Handle, path, user string, directory, private bool) error {
	var info windows.ByHandleFileInformation
	if err := windows.GetFileInformationByHandle(h, &info); err != nil {
		return err
	}
	if info.FileAttributes&windows.FILE_ATTRIBUTE_REPARSE_POINT != 0 {
		return fmt.Errorf("private path cannot contain a reparse point: %s", path)
	}
	if (info.FileAttributes&windows.FILE_ATTRIBUTE_DIRECTORY != 0) != directory {
		return fmt.Errorf("unexpected private path type: %s", path)
	}
	if private {
		sd, err := windows.GetSecurityInfo(h, windows.SE_FILE_OBJECT, windows.OWNER_SECURITY_INFORMATION|windows.DACL_SECURITY_INFORMATION)
		if err != nil {
			return fmt.Errorf("cannot verify Windows permissions for %s: %w", path, err)
		}
		if err := checkACL(sd, user, directory); err != nil {
			return fmt.Errorf("unsafe Windows permissions for %s: %w; choose a new private data directory or restrict this object's owner and Security permissions to your account (SYSTEM and Administrators are allowed)", path, err)
		}
	}
	return nil
}

func validatePath(path string) error {
	if strings.ContainsFunc(path, func(char rune) bool { return char < 32 }) {
		return errors.New("Windows private paths cannot contain NUL or control characters")
	}
	path = strings.ReplaceAll(path, "/", `\`)
	volume := filepath.VolumeName(path)
	if strings.HasPrefix(path, `\\?\`) || strings.HasPrefix(path, `\\.\`) || strings.Contains(path[len(volume):], ":") {
		return errors.New("private files require a normal filesystem path, without device prefixes or alternate streams")
	}
	for _, part := range strings.Split(path[len(volume):], `\`) {
		if part == "" || part == "." || part == ".." {
			continue
		}
		stem := strings.ToUpper(strings.TrimRight(strings.SplitN(part, ".", 2)[0], " "))
		device := stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$"
		if strings.HasPrefix(stem, "COM") || strings.HasPrefix(stem, "LPT") {
			suffix := []rune(stem[3:])
			device = device || len(suffix) == 1 && strings.ContainsRune("123456789¹²³", suffix[0])
		}
		if device || strings.HasSuffix(part, ".") || strings.HasSuffix(part, " ") || strings.ContainsAny(part, `<>"|?*`) {
			return errors.New("private paths cannot use reserved Windows names, trailing dots/spaces, or wildcard characters")
		}
	}
	return nil
}

func fullPath(path string) (string, error) {
	// filepath.Abs calls Windows GetFullPathName, which can remove a trailing
	// dot before a later check sees it. Reject ambiguous spelling first.
	if err := validatePath(path); err != nil {
		return "", err
	}
	path, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	if err := validatePath(path); err != nil {
		return "", err
	}
	return path, nil
}

func nativePath(path string) (*uint16, error) {
	if strings.HasPrefix(path, `\\`) {
		return windows.UTF16PtrFromString(`\\?\UNC\` + path[2:])
	}
	return windows.UTF16PtrFromString(`\\?\` + path)
}

func privateDescriptor(user string, directory bool) (*windows.SECURITY_DESCRIPTOR, error) {
	inherit := ""
	if directory {
		inherit = "OICI"
	}
	return windows.SecurityDescriptorFromString("O:" + user + "D:P(A;" + inherit + ";FA;;;" + user + ")")
}

// Hold all ancestor directories without delete sharing while resolving a path.
// This rejects junctions as well as symbolic links, including intermediate ones.
// Ancestors need not themselves be private; only the requested data directory is
// checked. Existing permissions are never rewritten by this package on Windows.
func directoryHandles(path, user string, create, private bool) ([]windows.Handle, error) {
	var paths []string
	for p := path; ; p = filepath.Dir(p) {
		paths = append(paths, p)
		if filepath.Dir(p) == p {
			break
		}
	}
	var handles []windows.Handle
	closeOnError := func(err error) ([]windows.Handle, error) {
		closeHandles(handles)
		return nil, err
	}
	for i := len(paths) - 1; i >= 0; i-- {
		p, err := nativePath(paths[i])
		if err != nil {
			return closeOnError(err)
		}
		access := uint32(windows.FILE_READ_ATTRIBUTES)
		if i == 0 && private {
			access |= windows.READ_CONTROL
		}
		h, err := windows.CreateFile(p, access, windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE, nil, windows.OPEN_EXISTING, windows.FILE_FLAG_BACKUP_SEMANTICS|windows.FILE_FLAG_OPEN_REPARSE_POINT, 0)
		if create && (errors.Is(err, windows.ERROR_FILE_NOT_FOUND) || errors.Is(err, windows.ERROR_PATH_NOT_FOUND)) {
			sd, e := privateDescriptor(user, true)
			if e != nil {
				return closeOnError(e)
			}
			sa := windows.SecurityAttributes{Length: uint32(unsafe.Sizeof(windows.SecurityAttributes{})), SecurityDescriptor: sd}
			err = windows.CreateDirectory(p, &sa)
			runtime.KeepAlive(sd)
			if err != nil && !errors.Is(err, windows.ERROR_ALREADY_EXISTS) {
				return closeOnError(err)
			}
			h, err = windows.CreateFile(p, access|windows.READ_CONTROL, windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE, nil, windows.OPEN_EXISTING, windows.FILE_FLAG_BACKUP_SEMANTICS|windows.FILE_FLAG_OPEN_REPARSE_POINT, 0)
			if err == nil {
				err = checkHandle(h, paths[i], user, true, true)
				if err != nil {
					windows.CloseHandle(h)
				}
			}
		}
		if err != nil {
			return closeOnError(&os.PathError{Op: "open private directory", Path: paths[i], Err: err})
		}
		handles = append(handles, h)
		if err := checkHandle(h, paths[i], user, true, private && i == 0); err != nil {
			return closeOnError(err)
		}
	}
	return handles, nil
}

func closeHandles(handles []windows.Handle) {
	for i := len(handles) - 1; i >= 0; i-- {
		windows.CloseHandle(handles[i])
	}
}

func EnsureDir(path string) error {
	path, err := fullPath(path)
	if err != nil {
		return err
	}
	user, err := currentSID()
	if err != nil {
		return err
	}
	handles, err := directoryHandles(path, user, true, true)
	closeHandles(handles)
	return err
}

func OpenFile(path string, flags int) (*os.File, error) {
	path, err := fullPath(path)
	if err != nil {
		return nil, err
	}
	user, err := currentSID()
	if err != nil {
		return nil, err
	}
	handles, err := directoryHandles(filepath.Dir(path), user, false, false)
	if err != nil {
		return nil, err
	}
	defer closeHandles(handles)
	p, err := nativePath(path)
	if err != nil {
		return nil, err
	}
	sd, err := privateDescriptor(user, false)
	if err != nil {
		return nil, err
	}
	sa := windows.SecurityAttributes{Length: uint32(unsafe.Sizeof(windows.SecurityAttributes{})), SecurityDescriptor: sd}
	access := uint32(windows.GENERIC_READ | windows.READ_CONTROL)
	if flags&(os.O_WRONLY|os.O_RDWR) != 0 {
		access |= windows.GENERIC_WRITE
	}
	disposition := uint32(windows.OPEN_EXISTING)
	if flags&os.O_CREATE != 0 {
		disposition = windows.OPEN_ALWAYS
		if flags&os.O_EXCL != 0 {
			disposition = windows.CREATE_NEW
		}
	}
	h, err := windows.CreateFile(p, access, windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE, &sa, disposition, windows.FILE_FLAG_OPEN_REPARSE_POINT, 0)
	runtime.KeepAlive(sd)
	if err != nil {
		return nil, &os.PathError{Op: "open private file", Path: path, Err: err}
	}
	if err := checkHandle(h, path, user, false, true); err != nil {
		windows.CloseHandle(h)
		return nil, err
	}
	return os.NewFile(uintptr(h), path), nil
}
