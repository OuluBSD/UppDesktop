// XdgDesktopEntry.h
//
// .desktop file parsing per the FreeDesktop Desktop Entry Specification.

#ifndef _Core_Desktop_Free_XdgDesktopEntry_h_
#define _Core_Desktop_Free_XdgDesktopEntry_h_

#include <Core/Core.h>
#include <Core/Desktop/DesktopCommon.h>

namespace Upp {

// A parsed desktop entry.
struct DesktopEntry : Moveable<DesktopEntry> {
	String file;              // path to the .desktop file
	String type;              // Application, Link, Directory
	String version;           // Version= value
	String name;              // Name= (localized)
	String generic_name;      // GenericName=
	String comment;           // Comment=
	String icon;              // Icon=
	bool   hidden             = false;
	bool   no_display         = false;
	Vector<String> only_show_in;
	Vector<String> not_show_in;
	String exec;              // Exec= (raw, with field codes)
	String path;              // Path=
	bool   terminal           = false;
	Vector<String> mime_types;
	Vector<String> categories;
	Vector<String> keywords;
	bool   startup_notify     = false;
	String startup_wm_class;
	String url;               // URL= (for Link type)
	Vector<String> actions;   // Actions= identifiers
	
	// Parsed action groups (Desktop Action <id>)
	struct Action : Moveable<Action> {
		String id;
		String name;
		String icon;
		String exec;
	};
	Vector<Action> action_groups;
	
	// Raw key-value pairs not recognized (for round-trip/extensions) - not used in current implementation
};

// Configuration for desktop entry lookup.
struct DesktopEntryConfig : Moveable<DesktopEntryConfig> {
	Vector<String> data_dirs;     // XDG_DATA_DIRS + custom
	String       current_desktop; // XDG_CURRENT_DESKTOP value
};

// Result of finding desktop entries.
struct DesktopEntryResult : Moveable<DesktopEntryResult> {
	Vector<DesktopEntry> entries;
	DesktopStatus status = DESKTOP_OK;
};

// Load a single .desktop file.
bool LoadDesktopEntry(const String& file, DesktopEntry& out);

// Load all Application-type desktop entries from the configured directories.
DesktopEntryResult LoadDesktopEntries(const DesktopEntryConfig& cfg);

// Find the desktop entry for a given desktop file ID (e.g. "org.example.App").
bool FindDesktopEntry(const DesktopEntryConfig& cfg, const String& desktop_id, DesktopEntry& out);

// Get the default application for a MIME type (per mimeapps.list).
String GetDefaultApp(const DesktopEntryConfig& cfg, const String& mime);

// Check if a desktop entry should be shown in the current desktop environment.
bool ShouldShowEntry(const DesktopEntry& entry, const String& current_desktop);

// Get the command line for launching an entry, with field code expansion.
String ExpandExec(const DesktopEntry& entry, const Vector<String>& files, const Vector<String>& urls);

} // Upp

#endif // _Core_Desktop_Free_XdgDesktopEntry_h_