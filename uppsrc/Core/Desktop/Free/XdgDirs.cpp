// XdgDirs.cpp
//
// XDG Base Directory and user-dirs.dirs resolution.

#include <Core/Desktop/Free/XdgDirs.h>
#include <Core/Desktop/DesktopCommon.h>

namespace Upp {

namespace {

// The standard XDG user directories, in the order the specification lists
// them. The label is what a file manager shows; the variable is what
// user-dirs.dirs writes.
struct VarDef { const char* var; const char* label; const char* fallback; };
static const VarDef kVars[] = {
	{ "XDG_DESKTOP_DIR",   "Desktop",   "Desktop"   },
	{ "XDG_DOWNLOAD_DIR",  "Downloads", "Downloads" },
	{ "XDG_DOCUMENTS_DIR", "Documents", "Documents" },
	{ "XDG_MUSIC_DIR",     "Music",     "Music"     },
	{ "XDG_PICTURES_DIR",  "Images",    "Pictures"  },
	{ "XDG_VIDEOS_DIR",    "Videos",    "Videos"    },
};

String StripTrailingSlashes(String s)
{
	while(s.GetCount() > 1 && (s[s.GetCount() - 1] == '/' || s[s.GetCount() - 1] == '\\'))
		s.Trim(s.GetCount() - 1);
	return s;
}

// Expand a $HOME-style reference in a user-dirs.dirs value. Only $HOME and
// ${HOME} are recognized; other $VAR references are left untouched, which is
// what the spec implies for a file that is not sourced.
String ExpandHomeValue(const String& v, const String& home)
{
	String r;
	const char* s = v;
	while(*s) {
		if(*s == '$') {
			if(s[1] == '{') {
				const char* e = s + 2;
				while(*e && *e != '}')
					e++;
				String id(s + 2, int(e - s - 2));
				if(id == "HOME") {
					r.Cat(home);
					s = (*e == '}') ? e + 1 : e;
					continue;
				}
				r.Cat('$');
				r.Cat('{');
				r.Cat(id);
				s = (*e == '}') ? e + 1 : e;
				continue;
			}
			if(s[1] == 'H') {
				const char* e = s + 1;
				while(*e && (IsAlpha(*e) || IsDigit(*e) || *e == '_'))
					e++;
				String id(s + 1, int(e - s - 1));
				if(id == "HOME") {
					r.Cat(home);
					s = e;
					continue;
				}
				r.Cat('$');
				r.Cat(id);
				s = e;
				continue;
			}
			r.Cat('$');
			s++;
			continue;
		}
		r.Cat(*s++);
	}
	return r;
}

// Read the first quoted value on a line beginning with `var`.
String ReadQuotedValue(const String& text, const String& var)
{
	int at = text.Find(var);
	if(at < 0)
		return Null;
	int open = text.Find('"', at);
	if(open < 0)
		return Null;
	int close = text.Find('"', open + 1);
	if(close < 0)
		return Null;
	return text.Mid(open + 1, close - open - 1);
}

} // anonymous

XdgRoots XdgRoots::FromEnv(const String& home_in)
{
	XdgRoots r;
	String home = home_in;
	if(home.IsEmpty())
		home = GetHomeDirectory();
	if(home.IsEmpty())
		home = GetEnv("HOME");
	r.home = home;

	String v;
	v = GetEnv("XDG_CONFIG_HOME");
	r.config_home = v.GetCount() ? v : AppendFileName(home, ".config");
	v = GetEnv("XDG_CONFIG_DIRS");
	r.config_dirs = v.GetCount() ? v : "/etc/xdg";
	v = GetEnv("XDG_DATA_HOME");
	r.data_home = v.GetCount() ? v : AppendFileName(home, ".local/share");
	v = GetEnv("XDG_DATA_DIRS");
	r.data_dirs = v.GetCount() ? v : "/usr/local/share:/usr/share";
	v = GetEnv("XDG_CACHE_HOME");
	r.cache_home = v.GetCount() ? v : AppendFileName(home, ".cache");
	v = GetEnv("XDG_STATE_HOME");
	r.state_home = v.GetCount() ? v : AppendFileName(home, ".local/state");
	return r;
}

Vector<XdgUserDir> LoadUserDirs(const XdgRoots& roots, bool check_existence)
{
	String file = AppendFileName(roots.config_home, "user-dirs.dirs");
	String text;
	if(FileExists(file))
		text = LoadFile(file);
	if(text.IsEmpty())
		return {};

	Index<String> seen;
	Vector<XdgUserDir> out;
	for(const VarDef& d : kVars) {
		String value = ReadQuotedValue(text, d.var);
		if(value.IsEmpty())
			continue;
		value = ExpandHomeValue(value, roots.home);
		value = StripTrailingSlashes(value);
		if(value.IsEmpty() || value == roots.home)
			continue;
		if(check_existence && !DirectoryExists(value))
			continue;
		if(seen.Find(value) >= 0)
			continue;
		seen.Add(value);
		XdgUserDir u;
		u.var = d.var;
		u.label = d.label;
		u.path = value;
		out.Add(u);
	}
	return out;
}

String ResolveUserDir(const XdgRoots& roots, const String& var, const String& fallback)
{
	String file = AppendFileName(roots.config_home, "user-dirs.dirs");
	String text;
	if(FileExists(file))
		text = LoadFile(file);
	if(!text.IsEmpty()) {
		String value = ReadQuotedValue(text, var);
		if(!value.IsEmpty()) {
			value = ExpandHomeValue(value, roots.home);
			value = StripTrailingSlashes(value);
			if(!value.IsEmpty())
				return NormalizePath(value);
		}
	}
	if(fallback.IsEmpty())
		return Null;
	return NormalizePath(AppendFileName(roots.home, fallback));
}

namespace {

// Legacy expansion: after '$', leading whitespace is skipped, then an
// identifier run [A-Za-z0-9_] expands through the process environment (an
// unset variable expands to nothing); anything else after '$' is copied as is.
// This mirrors the old reader's hand-rolled CParser loop exactly, so the
// Core/App wrappers keep their previous results when they delegate here.
String ExpandLegacyEnvValue(const String& v)
{
	String r;
	const char* s = v;
	while(*s) {
		if(*s == '$') {
			const char* p = s + 1;
			while(IsSpace(*p))
				p++;
			if(IsAlNum(*p) || *p == '_') {
				const char* id0 = p;
				while(IsAlNum(*p) || *p == '_')
					p++;
				r.Cat(GetEnv(String(id0, int(p - id0))));
				s = p;
				continue;
			}
			r.Cat('$');
			s++;
			continue;
		}
		r.Cat(*s++);
	}
	return r;
}

// One settings file, last occurrence of a key wins, and only keys before the
// first [group] header count - the legacy reader reads the default group.
VectorMap<String, String> ReadSettingsLastWins(const String& file)
{
	VectorMap<String, String> kv;
	FileIn in(file);
	bool in_default_group = true;
	while(!in.IsEof()) {
		String ln = in.GetLine();
		if(*ln == '[') {
			in_default_group = false;
			continue;
		}
		int eq = ln.Find('=');
		if(eq <= 0 || !in_default_group)
			continue;
		kv.GetAdd(TrimBoth(ln.Mid(0, eq))) = TrimBoth(ln.Mid(eq + 1));
	}
	return kv;
}

// One leading and one trailing quote, stripped independently, exactly like
// the old reader did before expanding.
String StripQuotesLegacy(String v)
{
	if(v.GetCount() && *v == '"')
		v = v.Mid(1);
	if(v.GetCount() && *v.Last() == '"')
		v.Trim(v.GetCount() - 1);
	return v;
}

} // anonymous

String ResolveShellFolder(const XdgRoots& roots, const String& key,
                          const String& fallback_key, DesktopStatus& status)
{
	status = DESKTOP_OK;
	// The chain is fixed and single-file: the first file that exists is the
	// only one read, and both keys are asked of it.
	String chain[3];
	chain[0] = AppendFileName(roots.config_home, "user-dirs.dirs");
	chain[1] = AppendFileName(roots.config_dirs, "user-dirs.defaults");
	chain[2] = AppendFileName(roots.config_dirs, "user-dirs.dirs");
	for(int i = 0; i < 3; i++) {
		if(!FileExists(chain[i]))
			continue;
		VectorMap<String, String> kv = ReadSettingsLastWins(chain[i]);
		// The legacy reader expands before it falls back to the second key,
		// so a value like $UNSET still triggers the fallback.
		String v = ExpandLegacyEnvValue(StripQuotesLegacy(kv.Get(key, Null)));
		if(v.IsEmpty() && !fallback_key.IsEmpty())
			v = ExpandLegacyEnvValue(StripQuotesLegacy(kv.Get(fallback_key, Null)));
		if(!v.IsEmpty())
			return v;
		break;   // one file only: the chain does not continue through the keys
	}
	status = DESKTOP_NOT_FOUND;
	return String();
}

} // Upp