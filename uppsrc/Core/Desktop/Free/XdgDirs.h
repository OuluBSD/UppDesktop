// XdgDirs.h
//
// XDG Base Directory and user-dirs.dirs resolution. The backend is given
// explicit roots so the tests never read the host's active configuration;
// production uses the real environment and home directory.

#ifndef _Core_Desktop_Free_XdgDirs_h_
#define _Core_Desktop_Free_XdgDirs_h_

#include <Core/Core.h>
#include <Core/Desktop/DesktopCommon.h>

namespace Upp {

// A resolved set of roots. All paths are absolute; an empty value means the
// variable was unset or the directory does not exist.
struct XdgRoots : Moveable<XdgRoots> {
	String config_home;   // XDG_CONFIG_HOME, default $HOME/.config
	String config_dirs;   // XDG_CONFIG_DIRS, default /etc/xdg
	String data_home;     // XDG_DATA_HOME, default $HOME/.local/share
	String data_dirs;     // XDG_DATA_DIRS, default /usr/local/share:/usr/share
	String cache_home;    // XDG_CACHE_HOME, default $HOME/.cache
	String state_home;    // XDG_STATE_HOME, default $HOME/.local/state
	String home;          // the home directory used to expand $HOME

	// Defaults from the environment, or from an explicit home when the test
	// wants to pin it.
	static XdgRoots FromEnv(const String& home = String());
};

// A user directory declared in ~/.config/user-dirs.dirs. The variable name is
// the key the file writes (XDG_DESKTOP_DIR etc.); the label is the short
// human name used for the sidebar.
struct XdgUserDir : Moveable<XdgUserDir> {
	String var;
	String label;
	String path;   // resolved, absolute, trailing separator stripped
};

// Parse ~/.config/user-dirs.dirs. The file is a shell fragment; only the two
// leading characters of the variable name are used and $HOME is expanded by
// hand because the file is not sourced.
//
// `roots` selects where to look. Returns the directories that exist and that
// resolve to distinct paths; a variable pointing at home itself is dropped,
// and three variables aimed at one directory yield one entry. With
// `check_existence` false, the caller takes over the existence question -
// this is the form for a client that carries its own filesystem view (a
// mount manager, a VFS), where a host-side existence check would answer the
// wrong filesystem.
Vector<XdgUserDir> LoadUserDirs(const XdgRoots& roots,
                                bool check_existence = true);

// Resolve one XDG variable to an absolute path. `fallback` is used when the
// variable is not declared in user-dirs.dirs; the standard fallbacks are the
// XDG Base Directory defaults.
String ResolveUserDir(const XdgRoots& roots, const String& var,
                      const String& fallback);

// The legacy shell-folder chain, byte-compatible with the reader that used to
// live in Core/App's GetShellFolder. It differs from ResolveUserDir because
// the legacy reader did not source the file:
//
//   - it reads ONE file from a fixed chain - <config_home>/user-dirs.dirs,
//     then <config_dirs>/user-dirs.defaults, then <config_dirs>/user-dirs.dirs
//     and asks two keys of it (`key`, then `fallback_key` when empty), so a
//     system that only ships user-dirs.defaults is still answered;
//   - lines are split at the first '=' and both sides trimmed; the LAST
//     occurrence of a key wins;
//   - one leading and one trailing quote are stripped independently;
//   - $IDENTIFIER expands through the process environment and disappears
//     when the variable is unset, not only $HOME.
//
// Returns the expanded absolute value and DESKTOP_OK; DESKTOP_NOT_FOUND with
// an empty value when no chain file exists or both keys are absent.
String ResolveShellFolder(const XdgRoots& roots, const String& key,
                          const String& fallback_key, DesktopStatus& status);

} // Upp

#endif // _Core_Desktop_Free_XdgDirs_h_