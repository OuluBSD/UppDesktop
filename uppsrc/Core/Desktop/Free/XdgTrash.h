// XdgTrash.h
//
// Trash handling per the FreeDesktop Trash Specification.

#ifndef _Core_Desktop_Free_XdgTrash_h_
#define _Core_Desktop_Free_XdgTrash_h_

#include <Core/Core.h>
#include <Core/Desktop/DesktopCommon.h>

namespace Upp {

// A trash directory set (files/ and info/).
struct TrashDir : Moveable<TrashDir> {
	String root;       // absolute path to the trash root (e.g. ~/.local/share/Trash)
	String files;      // root/files
	String info;       // root/info
	bool   is_home     = false; // true for $HOME/.local/share/Trash
};

// Configuration for trash operations.
struct TrashConfig : Moveable<TrashConfig> {
	Vector<String> top_dirs; // XDG_DATA_DIRS for system trashes + home
	String       home_dir;   // explicit home for testing
};

// A trashed file entry.
struct TrashEntry : Moveable<TrashEntry> {
	String original_path;  // absolute path before trashing
	String trash_file;     // path in trash/files
	String trash_info;     // path in trash/info
	String deletion_date;  // ISO 8601 date from .trashinfo
	
	bool   valid = false;
};

// Result of a trash operation.
struct TrashResult : Moveable<TrashResult> {
	DesktopStatus status = DESKTOP_OK;
	String detail;
	Vector<String> failed_files;
};

// Find all trash directories (home + per-volume).
Vector<TrashDir> FindTrashDirs(const TrashConfig& cfg);

// List all entries in a trash directory.
Vector<TrashEntry> ListTrash(const TrashDir& trash);

// Generate a unique trash file name (handles collisions).
String GenerateTrashName(const TrashDir& trash, const String& original_name);

// Create a .trashinfo file content.
String CreateTrashInfo(const String& original_path, const String& deletion_date);

// Move a file to trash (same filesystem = rename, cross-fs = copy+delete).
TrashResult MoveToTrash(const TrashConfig& cfg, const Vector<String>& files);

// Restore a file from trash to its original location.
TrashResult RestoreFromTrash(const TrashConfig& cfg, const Vector<String>& trash_info_files);

// Permanently delete a file from trash (empty specific entries).
TrashResult DeleteFromTrash(const TrashConfig& cfg, const Vector<String>& trash_info_files);

// Empty all trash directories the user has access to.
TrashResult EmptyTrash(const TrashConfig& cfg);

// Get the trash info for a specific original path (for "is this in trash?" queries).
TrashEntry FindTrashEntry(const TrashConfig& cfg, const String& original_path);

// Parse a .trashinfo file.
bool ParseTrashInfo(const String& file, TrashEntry& out);

} // Upp

#endif // _Core_Desktop_Free_XdgTrash_h_