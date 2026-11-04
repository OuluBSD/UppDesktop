// XdgIconTheme.cpp
//
// Icon theme resolution per the FreeDesktop Icon Theme Specification.

#include <Core/Desktop/Free/XdgIconTheme.h>
#include <Core/Desktop/Free/XdgDirs.h>

namespace Upp {

namespace {

// Parse a comma-separated list, trimming spaces.
Vector<String> ParseInherits(const String& s)
{
	Vector<String> out;
	int start = 0;
	while(start < s.GetCount()) {
		while(start < s.GetCount() && (s[start] == ' ' || s[start] == '\t'))
			start++;
		if(start >= s.GetCount())
			break;
		int end = start;
		while(end < s.GetCount() && s[end] != ',')
			end++;
		String part = s.Mid(start, end - start);
		while(part.GetCount() && (part[part.GetCount()-1] == ' ' || part[part.GetCount()-1] == '\t'))
			part.Trim(part.GetCount() - 1);
		if(part.GetCount())
			out.Add(part);
		start = end + 1;
	}
	return out;
}

// Parse a semicolon-separated list of directories from index.theme.
Vector<String> ParseDirectories(const String& s)
{
	Vector<String> out;
	int start = 0;
	while(start < s.GetCount()) {
		while(start < s.GetCount() && (s[start] == ' ' || s[start] == '\t'))
			start++;
		if(start >= s.GetCount())
			break;
		int end = start;
		while(end < s.GetCount() && s[end] != ';')
			end++;
		String part = s.Mid(start, end - start);
		while(part.GetCount() && (part[part.GetCount()-1] == ' ' || part[part.GetCount()-1] == '\t'))
			part.Trim(part.GetCount() - 1);
		if(part.GetCount())
			out.Add(part);
		start = end + 1;
	}
	return out;
}

// Read a single key=value from an INI-like section.
String ReadIniValue(const String& text, const String& section, const String& key)
{
	String sec = "[" + section + "]";
	int secPos = text.Find(sec);
	if(secPos < 0)
		return Null;
	int lineStart = text.ReverseFind('\n', secPos);
	if(lineStart < 0)
		lineStart = 0;
	else
		lineStart++;
	int secEnd = text.Find('\n', secPos);
	if(secEnd < 0)
		secEnd = text.GetCount();
	// Search from section start to next section or end
	String sectionText = text.Mid(secPos);
	int nextSec = sectionText.Find("\n[");
	if(nextSec >= 0)
		sectionText = sectionText.Mid(0, nextSec);
	
	String findKey = key + "=";
	int keyPos = sectionText.Find(findKey);
	if(keyPos < 0)
		return Null;
	keyPos += findKey.GetCount();
	int valEnd = sectionText.Find('\n', keyPos);
	if(valEnd < 0)
		valEnd = sectionText.GetCount();
	String val = sectionText.Mid(keyPos, valEnd - keyPos);
	return val;
}

// Parse an integer with default.
int ParseIntDef(const String& s, int def)
{
	if(s.IsEmpty())
		return def;
	return atoi(s);
}

// Resolve an icon directory path inside a theme.
String ResolveIconDir(const String& theme_path, const String& dir_name)
{
	return AppendFileName(theme_path, dir_name);
}

// Check if a file exists and is a regular file.
bool IsIconFile(const String& path)
{
	return FileExists(path) && !DirectoryExists(path);
}

// Score a directory entry for a requested size/scale.
// Returns -1 if not a match, otherwise a score (lower is better).
int ScoreDir(const IconThemeDir& dir, int size, int scale)
{
	if(dir.type == "Fixed") {
		if(dir.size == size)
			return 0;
		return -1;
	}
	if(dir.type == "Scalable") {
		if(dir.min_size > 0 && size < dir.min_size)
			return -1;
		if(dir.max_size > 0 && size > dir.max_size)
			return -1;
		return abs(size - dir.size);
	}
	if(dir.type == "Threshold") {
		if(dir.size == 0)
			return -1;
		int diff = abs(size - dir.size);
		if(diff <= dir.threshold)
			return diff;
		return -1;
	}
	return -1;
}

// Find an icon file in a theme directory.
String FindIconInDir(const String& theme_path, const String& dir_name, const String& icon_name, const String& context)
{
	String dir_path = AppendFileName(theme_path, dir_name);
	if(!DirectoryExists(dir_path))
		return Null;
	
	// Try context subdirectory first
	if(context.GetCount()) {
		String ctx_dir = AppendFileName(dir_path, context);
		if(DirectoryExists(ctx_dir)) {
			FindFile ff(AppendFileName(ctx_dir, icon_name + ".*"));
			while(ff) {
				if(IsIconFile(ff.GetPath()))
					return ff.GetPath();
				ff.Next();
			}
		}
	}
	
	// Then the directory itself
	FindFile ff(AppendFileName(dir_path, icon_name + ".*"));
	while(ff) {
		if(IsIconFile(ff.GetPath()))
			return ff.GetPath();
		ff.Next();
	}
	return Null;
}

// Load a single theme from its index.theme file.
IconTheme LoadIconThemeFromFile(const String& theme_path, const String& name)
{
	IconTheme theme;
	theme.name = name;
	theme.path = theme_path;
	
	String index_file = AppendFileName(theme_path, "index.theme");
	if(!FileExists(index_file))
		return theme;
	
	String text = LoadFile(index_file);
	if(text.IsEmpty())
		return theme;
	
	theme.inherits = ReadIniValue(text, "Icon Theme", "Inherits");
	theme.default_context = ReadIniValue(text, "Icon Theme", "Directories");
	theme.threshold = ParseIntDef(ReadIniValue(text, "Icon Theme", "Threshold"), 2);
	String hidden = ReadIniValue(text, "Icon Theme", "Hidden");
	if(hidden == "true" || hidden == "1")
		theme.hidden = true;
	
	Vector<String> dir_names = ParseDirectories(theme.default_context);
	for(const String& dname : dir_names) {
		String section = dname;
		IconThemeDir dir;
		dir.name = dname;
		dir.directory = dname;
		dir.type = ReadIniValue(text, section, "Type");
		if(dir.type.IsEmpty())
			dir.type = "Threshold";
		dir.context = ReadIniValue(text, section, "Context");
		dir.size = ParseIntDef(ReadIniValue(text, section, "Size"), 0);
		dir.min_size = ParseIntDef(ReadIniValue(text, section, "MinSize"), 0);
		dir.max_size = ParseIntDef(ReadIniValue(text, section, "MaxSize"), 0);
		dir.threshold = ParseIntDef(ReadIniValue(text, section, "Threshold"), 2);
		theme.dirs.Add(dir);
	}
	return theme;
}

// Build the search path for themes from config.
Vector<String> GetThemeRoots(const IconLookupConfig& cfg)
{
	Vector<String> roots;
	
	if(cfg.user_theme_dir.GetCount()) {
		roots.Add(cfg.user_theme_dir);
	}
	
	for(const String& r : cfg.theme_roots) {
		if(r.GetCount())
			roots.Add(r);
	}
	
	// Standard XDG data dirs
	XdgRoots xdg = XdgRoots::FromEnv();
	Vector<String> data_dirs = Split(xdg.data_dirs, ':');
	for(const String& d : data_dirs) {
		if(d.GetCount())
			roots.Add(AppendFileName(d, "icons"));
	}
	
	// Always append hicolor fallback
	roots.Add("/usr/share/icons");
	
	return roots;
}

// Find a theme directory by name.
String FindThemePath(const Vector<String>& roots, const String& name)
{
	for(const String& root : roots) {
		String path = AppendFileName(root, name);
		if(DirectoryExists(path) && FileExists(AppendFileName(path, "index.theme")))
			return path;
	}
	return Null;
}

// Recursively load a theme with inheritance.
IconTheme LoadIconThemeRecursive(const Vector<String>& roots, const String& name, Index<String>& loaded)
{
	if(loaded.Find(name) >= 0)
		return IconTheme(); // circular inheritance guard
	loaded.Add(name);
	
	String path = FindThemePath(roots, name);
	if(path.IsEmpty())
		return IconTheme();
	
	IconTheme theme = LoadIconThemeFromFile(path, name);
	if(theme.name.IsEmpty())
		return IconTheme();
	
	// Load inherited themes
	Vector<String> inherits = ParseInherits(theme.inherits);
	for(const String& iname : inherits) {
		IconTheme parent = LoadIconThemeRecursive(roots, iname, loaded);
		if(parent.name.GetCount()) {
			// Prepend parent directories so child overrides
			for(int i = parent.dirs.GetCount() - 1; i >= 0; i--)
				theme.dirs.Insert(0, parent.dirs[i]);
		}
	}
	return theme;
}

} // anonymous

IconTheme LoadIconTheme(const IconLookupConfig& cfg, const String& name)
{
	Vector<String> roots = GetThemeRoots(cfg);
	Index<String> loaded;
	return LoadIconThemeRecursive(roots, name, loaded);
}

IconLookup LookupIcon(const IconLookupConfig& cfg, const String& icon_name,
                      int size, int scale, const String& context)
{
	IconLookup result;
	
	if(size <= 0)
		size = 48;
	if(scale <= 0)
		scale = 1;
	
	Vector<String> roots = GetThemeRoots(cfg);
	
	// Try requested theme first, then fallback, then hicolor
	Vector<String> themes;
	if(cfg.theme.GetCount())
		themes.Add(cfg.theme);
	if(cfg.fallback_theme.GetCount() && cfg.fallback_theme != cfg.theme)
		themes.Add(cfg.fallback_theme);
	themes.Add("hicolor");
	
	for(const String& theme_name : themes) {
		Index<String> loaded;
		IconTheme theme = LoadIconThemeRecursive(roots, theme_name, loaded);
		if(theme.name.IsEmpty() || theme.hidden)
			continue;
		
		// Score all directories
		struct ScoredDir { int score; int idx; };
		Vector<ScoredDir> scored;
		for(int i = 0; i < theme.dirs.GetCount(); i++) {
			int sc = ScoreDir(theme.dirs[i], size, scale);
			if(sc >= 0)
				scored.Add({sc, i});
		}
		
		if(scored.GetCount() == 0)
			continue;
		
		// Sort by score
		std::sort(scored.begin(), scored.end(), [](const ScoredDir& a, const ScoredDir& b) {
			return a.score < b.score;
		});
		
		// Search in scored order
		for(const auto& sd : scored) {
			const IconThemeDir& dir = theme.dirs[sd.idx];
			String file = FindIconInDir(theme.path, dir.directory, icon_name, context);
			if(file.GetCount()) {
				result.file = file;
				result.theme = theme.name;
				result.context = dir.context;
				result.directory = dir.directory;
				result.size = dir.size > 0 ? dir.size : size;
				result.scale = scale;
				result.fallback = theme_name == cfg.theme ? "requested theme" :
				                  theme_name == cfg.fallback_theme ? "fallback theme" : "hicolor fallback";
				result.found = true;
				return result;
			}
		}
	}
	
	// Not found
	result.fallback = "not found in any theme";
	result.found = false;
	return result;
}

} // Upp