// DesktopFree.h
//
// FreeDesktop backend umbrella header: wraps XdgDirs, XdgIconTheme, XdgMime,
// XdgDesktopEntry, and XdgTrash.

#ifndef _Core_Desktop_Free_DesktopFree_h_
#define _Core_Desktop_Free_DesktopFree_h_

#include <Core/Core.h>

#include <Core/Desktop/Free/XdgDirs.h>
#include <Core/Desktop/Free/XdgIconTheme.h>
#include <Core/Desktop/Free/XdgMime.h>
#include <Core/Desktop/Free/XdgDesktopEntry.h>
#include <Core/Desktop/Free/XdgTrash.h>

namespace Upp {

// Configuration bundle for the FreeDesktop backend.
struct FreeDesktopConfig : Moveable<FreeDesktopConfig> {
	XdgRoots xdg_roots;
	IconLookupConfig icon_config;
	MimeLookupConfig mime_config;
	DesktopEntryConfig desktop_config;
	TrashConfig trash_config;
	
	static FreeDesktopConfig FromEnv();
};

// Construct the backend and register it as the process desktop backend.
// Static-archive linking pulls a package's object file in only when some
// referenced symbol asks for it, so a backend that registered at static-init
// time only would never activate in a binary that links nothing else of it;
// callers that expect backend answers call this once, up front.
void RegisterFreeDesktopBackend();

} // Upp

#endif // _Core_Desktop_Free_DesktopFree_h_