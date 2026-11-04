// DesktopBackend.h
//
// The `IDesktopBackend` contract. Every platform backend in Core/Desktop
// implements this interface; the common package selects one and exposes it.
//
// The interface is intentionally small and stateless: a backend is constructed
// once and asked questions. State that depends on the host (the active icon
// theme, the user's default applications) is read fresh on each call so a
// caller that changes it sees the change, and so the headless tests can drive
// the same code path with injectable roots.
//
// Nothing here returns Image. Icon answers are identity + resolution metadata;
// materialization is Draw/Desktop's job.

#ifndef _Core_Desktop_DesktopBackend_h_
#define _Core_Desktop_DesktopBackend_h_

#include <Core/Core.h>
#include "DesktopCommon.h"

namespace Upp {

struct IDesktopBackend {
	// Identity and diagnostics. A backend reports its name and, where it has
	// something to say, the roots it searched and what it fell back to.
	virtual const char* Name() const = 0;
	virtual bool        IsSupported() const = 0;
	virtual String      Diagnostics() const = 0;

	// XDG base directories and the user directories declared in
	// ~/.config/user-dirs.dirs. Returns OK with an empty value when the
	// directory is genuinely absent; UNSUPPORTED when the backend does not
	// implement this on the platform.
	virtual String GetUserDirectory(const String& xdg_var,
	                               const String& fallback,
	                               DesktopStatus& status) const = 0;

	// MIME detection. `path` is the file path (extension is enough for the
	// glob tables; content sniffing is optional and backend-dependent).
	virtual DesktopMimeResult DetectMime(const String& path) const = 0;
	virtual DesktopMimeResult GetMimeDescription(const String& mime) const = 0;

	// Icon lookup. `mime_or_name` is a MIME type or an icon name; `size` and
	// `scale` request a specific rendering. The backend resolves the theme
	// file or reports UNSUPPORTED / NOT_FOUND.
	virtual DesktopIconResult LookupIcon(const String& mime_or_name,
	                                     int size, int scale) const = 0;

	// Application icon identity and desktop-entry metadata. `name` is a
	// desktop file basename or a desktop entry Icon= value.
	virtual DesktopIconResult LookupAppIcon(const String& name,
	                                        int size, int scale) const = 0;
	virtual DesktopApp      GetDesktopApp(const String& desktop_file) const = 0;

	// Default application and OpenWith candidates for a MIME type or extension.
	virtual Vector<DesktopDefaultApp> GetDefaultApps(const String& mime_or_ext) const = 0;

	// Trash. The backend reports whether it can move to trash, restore and
	// empty; the trash directory is returned only when the capability is true,
	// so a caller cannot act on a path the backend never promised.
	virtual DesktopTrashInfo GetTrashInfo() const = 0;
	virtual DesktopStatus MoveToTrash(const String& path, String& new_path) const = 0;
	virtual DesktopStatus RestoreTrash(const String& trash_path,
	                                   String& original_path) const = 0;
	virtual DesktopStatus EmptyTrash(String& error) const = 0;

protected:
	~IDesktopBackend() {}
};

// The selected backend for this process. Constructed once, lazily, by the
// common package; backends register themselves at static-init time.
IDesktopBackend* GetDesktopBackend();

// The icon theme the running desktop integration prefers (for example the
// theme GTK or KDE is actually showing). Backends read it fresh on each
// lookup; an empty value means "no preference" and a backend falls back to
// its own default theme. A GUI layer that knows the theme sets it once,
// because a backend cannot read window-system settings on its own.
void   SetPreferredIconTheme(const String& theme_name);
String GetPreferredIconTheme();

// Register a backend implementation. Called by platform backends at static-init time.
void RegisterDesktopBackend(IDesktopBackend* backend);

} // Upp

#endif // _Core_Desktop_DesktopBackend_h_