// XdgDesktopEntry.cpp
//
// .desktop file parsing per the FreeDesktop Desktop Entry Specification.

#include <Core/Desktop/Free/XdgDesktopEntry.h>
#include <Core/Desktop/Free/XdgDirs.h>

namespace Upp {

namespace {

struct KeyValue : Moveable<KeyValue> {
	String key;
	String value;
	
	KeyValue() {}
	KeyValue(const String& k, const String& v) : key(k), value(v) {}
};

// Simple string-to-string map using linear search (good enough for .desktop files)
class DesktopStringMap {
	Vector<KeyValue> data;
public:
	void Put(const String& key, const String& val) {
		for(auto& kv : data) {
			if(kv.key == key) {
				kv.value = val;
				return;
			}
		}
		data.Add(KeyValue(key, val));
	}
	
	String Get(const String& key, const String& def = String()) const {
		for(const auto& kv : data) {
			if(kv.key == key)
				return kv.value;
		}
		return def;
	}
	
	bool Has(const String& key) const {
		for(const auto& kv : data) {
			if(kv.key == key)
				return true;
		}
		return false;
	}
	
	void Iterate(Function<void(const String&, const String&)> fn) const {
		for(const auto& kv : data)
			fn(kv.key, kv.value);
	}
	
	int GetCount() const { return data.GetCount(); }
};

// Get a localized value from a map of locale->value.
String GetLocalizedValue(const DesktopStringMap& map, const String& base_key, const String& locale)
{
	// Try exact locale match
	String key = base_key + "[" + locale + "]";
	if(map.Has(key))
		return map.Get(key);
	
	// Try language_COUNTRY
	int at = locale.Find('_');
	if(at > 0) {
		key = base_key + "[" + locale.Mid(0, at) + "]";
		if(map.Has(key))
			return map.Get(key);
	}
	
	// Try language@modifier
	at = locale.Find('@');
	if(at > 0) {
		key = base_key + "[" + locale.Mid(0, at) + "]";
		if(map.Has(key))
			return map.Get(key);
	}
	
	// Try language only
	if(at > 0) {
		key = base_key + "[" + locale.Mid(0, at) + "]";
		if(map.Has(key))
			return map.Get(key);
	}
	
	// Default
	if(map.Has(base_key))
		return map.Get(base_key);
	
	return Null;
}

// Unescape a desktop entry value (\s, \n, \t, \r, \\).
String UnescapeValue(const String& s)
{
	String out;
	for(int i = 0; i < s.GetCount(); i++) {
		if(s[i] == '\\' && i + 1 < s.GetCount()) {
			char c = s[i+1];
			if(c == 's') out.Cat(' ');
			else if(c == 'n') out.Cat('\n');
			else if(c == 't') out.Cat('\t');
			else if(c == 'r') out.Cat('\r');
			else if(c == '\\') out.Cat('\\');
			else out.Cat(c);
			i++;
		}
		else {
			out.Cat(s[i]);
		}
	}
	return out;
}

// Parse a semicolon-separated list with escaping.
Vector<String> ParseSemicolonList(const String& s)
{
	Vector<String> out;
	String cur;
	for(int i = 0; i < s.GetCount(); i++) {
		if(s[i] == '\\' && i + 1 < s.GetCount() && s[i+1] == ';') {
			cur.Cat(';');
			i++;
		}
		else if(s[i] == ';') {
			if(cur.GetCount())
				out.Add(cur);
			cur.Clear();
		}
		else {
			cur.Cat(s[i]);
		}
	}
	if(cur.GetCount())
		out.Add(cur);
	return out;
}

} // anonymous

// Parse a single .desktop file.
bool LoadDesktopEntry(const String& file, DesktopEntry& out)
{
	if(!FileExists(file))
		return false;
	
	String text = LoadFile(file);
	if(text.IsEmpty())
		return false;
	
	out.file = file;
	
	DesktopStringMap values;
	String current_section;
	
	for(const String& line_raw : Split(text, '\n')) {
		String line = TrimBoth(line_raw);
		
		if(line.GetCount() == 0 || line[0] == '#')
			continue;
		
		if(line[0] == '[') {
			int end = line.Find(']');
			if(end > 1) {
				current_section = line.Mid(1, end - 1);
			}
			continue;
		}
		
		if(current_section.IsEmpty())
			continue;
		
		int eq = line.Find('=');
		if(eq < 0)
			continue;
		
		String key = line.Mid(0, eq);
		String val = line.Mid(eq + 1);
		
		values.Put(current_section + "/" + key, val);
	}
	
	// Must have Desktop Entry section
	if(!values.Has("Desktop Entry/Type"))
		return false;
	
	auto get_val = [&](const String& key) -> String {
		return values.Get(key);
	};
	
	out.type = values.Get("Desktop Entry/Type");
	out.version = values.Get("Desktop Entry/Version");
	out.name = UnescapeValue(GetLocalizedValue(values, "Desktop Entry/Name", GetEnv("LC_MESSAGES")));
	out.generic_name = UnescapeValue(GetLocalizedValue(values, "Desktop Entry/GenericName", GetEnv("LC_MESSAGES")));
	out.comment = UnescapeValue(GetLocalizedValue(values, "Desktop Entry/Comment", GetEnv("LC_MESSAGES")));
	out.icon = values.Get("Desktop Entry/Icon");
	out.hidden = values.Get("Desktop Entry/Hidden") == "true";
	out.no_display = values.Get("Desktop Entry/NoDisplay") == "true";
	out.only_show_in = ParseSemicolonList(values.Get("Desktop Entry/OnlyShowIn"));
	out.not_show_in = ParseSemicolonList(values.Get("Desktop Entry/NotShowIn"));
	out.exec = values.Get("Desktop Entry/Exec");
	out.path = values.Get("Desktop Entry/Path");
	out.terminal = values.Get("Desktop Entry/Terminal") == "true";
	out.mime_types = ParseSemicolonList(values.Get("Desktop Entry/MimeType"));
	out.categories = ParseSemicolonList(values.Get("Desktop Entry/Categories"));
	out.keywords = ParseSemicolonList(values.Get("Desktop Entry/Keywords"));
	out.startup_notify = values.Get("Desktop Entry/StartupNotify") == "true";
	out.startup_wm_class = values.Get("Desktop Entry/StartupWMClass");
	out.url = values.Get("Desktop Entry/URL");
	out.actions = ParseSemicolonList(values.Get("Desktop Entry/Actions"));
	
	// Parse action groups
	for(const String& aid : out.actions) {
		String section = "Desktop Action " + aid;
		DesktopEntry::Action action;
		action.id = aid;
		action.name = UnescapeValue(GetLocalizedValue(values, section + "/Name", GetEnv("LC_MESSAGES")));
		action.icon = values.Get(section + "/Icon");
		action.exec = values.Get(section + "/Exec");
		if(action.name.GetCount())
			out.action_groups.Add(action);
	}
	
	return true;
}

// Find all .desktop files in applications/ directories.
Vector<String> FindDesktopFiles(const Vector<String>& data_dirs)
{
	Vector<String> files;
	for(const String& dir : data_dirs) {
		String app_dir = AppendFileName(dir, "applications");
		if(!DirectoryExists(app_dir))
			continue;
		
		FindFile ff(AppendFileName(app_dir, "*.desktop"));
		while(ff) {
			files.Add(ff.GetPath());
			ff.Next();
		}
	}
	return files;
}

DesktopEntryResult LoadDesktopEntries(const DesktopEntryConfig& cfg)
{
	DesktopEntryResult result;
	
	Vector<String> data_dirs;
	if(cfg.data_dirs.GetCount() > 0)
		data_dirs <<= pick(cfg.data_dirs);
	else {
		XdgRoots xdg = XdgRoots::FromEnv();
		data_dirs = Split(xdg.data_dirs, ':');
	}
	
	Vector<String> files = FindDesktopFiles(data_dirs);
	for(const String& f : files) {
		DesktopEntry entry;
		if(LoadDesktopEntry(f, entry)) {
			if(entry.type == "Application" && !entry.hidden && !entry.no_display) {
				if(ShouldShowEntry(entry, cfg.current_desktop))
					result.entries.Add(pick(entry));
			}
		}
	}
	
	return result;
}

bool FindDesktopEntry(const DesktopEntryConfig& cfg, const String& desktop_id, DesktopEntry& out)
{
	Vector<String> data_dirs;
	if(cfg.data_dirs.GetCount() > 0)
		data_dirs <<= pick(cfg.data_dirs);
	else {
		XdgRoots xdg = XdgRoots::FromEnv();
		data_dirs = Split(xdg.data_dirs, ':');
	}
	
	for(const String& dir : data_dirs) {
		String file = AppendFileName(dir, "applications/" + desktop_id);
		if(FileExists(file)) {
			return LoadDesktopEntry(file, out);
		}
	}
	return false;
}

bool ShouldShowEntry(const DesktopEntry& entry, const String& current_desktop)
{
	if(current_desktop.IsEmpty())
		return true;
	
	Vector<String> desktops = Split(current_desktop, ':');
	
	// OnlyShowIn takes precedence
	if(entry.only_show_in.GetCount()) {
		for(const String& d : desktops) {
			bool found = false;
			for(const String& osi : entry.only_show_in) {
				if(osi == d) { found = true; break; }
			}
			if(found)
				return true;
		}
		return false;
	}
	
	// NotShowIn excludes
	for(const String& d : desktops) {
		for(const String& nsi : entry.not_show_in) {
			if(nsi == d)
				return false;
		}
	}
	
	return true;
}

String ExpandExec(const DesktopEntry& entry, const Vector<String>& files, const Vector<String>& urls)
{
	String cmd = entry.exec;
	if(cmd.IsEmpty())
		return Null;
	
	// Build replacements
	String f_single = files.GetCount() ? files[0] : String();
	String u_single = urls.GetCount() ? urls[0] : String();
	String f_multi = Join(files, " ");
	String u_multi = Join(urls, " ");
	
	// Process field codes
	String out;
	for(int i = 0; i < cmd.GetCount(); i++) {
		if(cmd[i] == '%' && i + 1 < cmd.GetCount()) {
			char code = cmd[i+1];
			switch(code) {
				case 'f': out.Cat(f_single); break;
				case 'F': out.Cat(f_multi); break;
				case 'u': out.Cat(u_single); break;
				case 'U': out.Cat(u_multi); break;
				case 'i': 
					if(entry.icon.GetCount()) {
						out.Cat(" --icon ");
						out.Cat(entry.icon);
					}
					break;
				case 'c': out.Cat(entry.name); break;
				case 'k': out.Cat(entry.file); break;
				case '%': out.Cat('%'); break;
				default: out.Cat('%'); out.Cat(code); break;
			}
			i++;
		}
		else {
			out.Cat(cmd[i]);
		}
	}
	return out;
}

// Simple mimeapps.list parsing for default apps.
String GetDefaultApp(const DesktopEntryConfig& cfg, const String& mime)
{
	Vector<String> data_dirs;
	if(cfg.data_dirs.GetCount() > 0)
		data_dirs <<= pick(cfg.data_dirs);
	else {
		XdgRoots xdg = XdgRoots::FromEnv();
		data_dirs = Split(xdg.data_dirs, ':');
	}
	
	for(const String& dir : data_dirs) {
		String file = AppendFileName(dir, "applications/mimeapps.list");
		if(!FileExists(file))
			continue;
		
		String text = LoadFile(file);
		for(const String& line : Split(text, '\n')) {
			String l = TrimBoth(line);
			if(l.GetCount() == 0 || l[0] == '#')
				continue;
			
			int eq = l.Find('=');
			if(eq < 0)
				continue;
			
			String key = l.Mid(0, eq);
			String val = l.Mid(eq + 1);
			if(key == mime) {
				Vector<String> apps = ParseSemicolonList(val);
				if(apps.GetCount())
					return apps[0];
			}
		}
	}
	return Null;
}

} // Upp