// DesktopCommon.h
//
// GUI-free desktop service types shared by every Core/Desktop backend and by
// the callers that consume them.

#ifndef _Core_Desktop_DesktopCommon_h_
#define _Core_Desktop_DesktopCommon_h_

#include <Core/Core.h>

namespace Upp {

// A result is either answered or refused. A refused result carries a status and
// a detail string; an answered one carries the data and status OK. This is
// what separates "no icon theme is installed" (OK, empty file path) from "icon
// lookup is not supported on this platform" (UNSUPPORTED) -- the two read the
// same to a caller that only looks at the file path.
enum DesktopStatus : int {
	DESKTOP_OK           = 0,
	DESKTOP_UNSUPPORTED  = 1,
	DESKTOP_NOT_FOUND    = 2,
	DESKTOP_MALFORMED    = 3,
	DESKTOP_ERROR        = 4,
};

struct DesktopMimeResult : Moveable<DesktopMimeResult> {
	String mime;
	String description;
	String detail;
	DesktopStatus status = DESKTOP_OK;

	DesktopMimeResult() {}
	DesktopMimeResult(DesktopStatus s, const char* d = NULL)
		: status(s), detail(d ? String(d) : String::GetVoid()) {}

	bool IsOk()       const { return status == DESKTOP_OK; }
	bool IsSupported() const { return status != DESKTOP_UNSUPPORTED; }
};

struct DesktopIconResult : Moveable<DesktopIconResult> {
	// Identity and resolution metadata. The caller materializes this into an
	// Image through Draw/Desktop; a headless client can print it.
	String mime_or_name;
	String theme;
	String file_path;      // resolved theme file, or empty
	int    size            = 0;
	int    scale           = 1;
	String fallback_reason;
	DesktopStatus status   = DESKTOP_OK;

	DesktopIconResult() {}
	DesktopIconResult(DesktopStatus s, const char* d = NULL)
		: status(s), fallback_reason(d ? String(d) : String::GetVoid()) {}

	bool HasFile() const { return !file_path.IsEmpty(); }
};

// A desktop entry as the backend resolved it. The backend does not interpret
// OnlyShowIn/NotShowIn into a boolean for the caller; it returns the raw
// fields and the resolved visibility so callers can match their own rules.
struct DesktopApp : Moveable<DesktopApp> {
	String name;          // localized name, or the file's basename
	String icon;          // Icon= value, or empty
	String exec;          // Exec= value, or empty
	String mime;          // MimeType= value, or empty
	String file;          // the .desktop file path
	bool   visible        = true;
	bool   has_display    = false;
	DesktopStatus status  = DESKTOP_OK;
};

struct DesktopDefaultApp : Moveable<DesktopDefaultApp> {
	String desktop_file;
	String mime;
	DesktopStatus status = DESKTOP_OK;
};

struct DesktopTrashInfo : Moveable<DesktopTrashInfo> {
	String trash_dir;     // absolute path of the trash root (files/..)
	String info_dir;      // absolute path of the info/ directory
	DesktopStatus status  = DESKTOP_OK;
	bool   can_move       = false;
	bool   can_restore    = false;
	bool   can_empty      = false;
};

} // Upp

#endif // _Core_Desktop_DesktopCommon.h_