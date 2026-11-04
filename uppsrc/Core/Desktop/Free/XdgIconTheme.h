// XdgIconTheme.h
//
// Icon theme resolution per the FreeDesktop Icon Theme Specification. The
// backend is given explicit roots so tests never read the host's active
// theme; production uses the standard hicolor path and the environment.

#ifndef _Core_Desktop_Free_XdgIconTheme_h_
#define _Core_Desktop_Free_XdgIconTheme_h_

#include <Core/Core.h>

namespace Upp {

// A directory entry from an index.theme file.
struct IconThemeDir : Moveable<IconThemeDir> {
	String name;       // section name
	String type;       // Fixed, Scalable, Threshold
	String context;    // any
	int    size        = 0;
	int    min_size    = 0;
	int    max_size    = 0;
	int    threshold   = 2;
	String directory;  // subdirectory inside the theme
};

// A resolved icon theme.
struct IconTheme : Moveable<IconTheme> {
	String name;
	String path;            // absolute theme directory
	String inherits;        // comma-separated inherited theme names
	String default_context; // e.g. "Actions"
	int    threshold        = 2;
	bool   hidden           = false;
	Vector<IconThemeDir> dirs;
};

// The search configuration. `theme_roots` are the directories searched for
// themes (the hicolor fallback path is appended automatically); `theme` is
// the requested theme name, with an empty value meaning "use the configured
// default".
struct IconLookupConfig : Moveable<IconLookupConfig> {
	String theme;
	String fallback_theme = "hicolor";
	String user_theme_dir;  // $XDG_DATA_DIRS-less user override, usually empty
	Vector<String> theme_roots; // searched in order, in addition to the defaults
};

// The result of looking an icon up: the resolved file and the diagnostics a
// caller can print. `found` is false when no file was resolved; that is a
// legitimate answer (the theme has no such icon), distinct from the lookup
// being unsupported.
struct IconLookup : Moveable<IconLookup> {
	String file;         // resolved absolute path, or empty
	String theme;        // the theme that answered
	String context;      // the context that answered
	String directory;    // the subdirectory that answered
	int    size          = 0;
	int    scale         = 1;
	String fallback;     // why the answer is what it is
	bool   found         = false;
};

// Load a theme by name from the configured roots. Returns an empty theme when
// the theme does not exist; the caller distinguishes that from a failure by
// checking `theme.name`.
IconTheme LoadIconTheme(const IconLookupConfig& cfg, const String& name);

// Resolve `icon_name` in `size`/`scale` using `cfg`. Walks inheritance and
// falls back to the fallback theme, then to the hicolor directory directly.
IconLookup LookupIcon(const IconLookupConfig& cfg, const String& icon_name,
                      int size, int scale, const String& context = String());

} // Upp

#endif // _Core_Desktop_Free_XdgIconTheme_h_