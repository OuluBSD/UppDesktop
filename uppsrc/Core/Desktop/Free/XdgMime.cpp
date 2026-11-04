// XdgMime.cpp
//
// MIME type resolution per the FreeDesktop MIME Applications Specification.

#include <Core/Desktop/Free/XdgMime.h>
#include <Core/Desktop/Free/XdgDirs.h>

namespace Upp {

namespace {

// Split a string by delimiter, keeping empty parts.
Vector<String> SplitKeep(const String& s, char delim)
{
	Vector<String> out;
	int start = 0;
	while(start <= s.GetCount()) {
		int pos = s.Find(delim, start);
		if(pos < 0)
			pos = s.GetCount();
		out.Add(s.Mid(start, pos - start));
		start = pos + 1;
	}
	return out;
}

// Parse a glob line: mimetype:glob[:flags]
bool ParseGlobLine(const String& line, MimeGlob& out)
{
	Vector<String> parts = SplitKeep(line, ':');
	if(parts.GetCount() < 2)
		return false;
	out.mime = parts[0];
	out.pattern = parts[1];
	if(parts.GetCount() >= 3)
		out.flags = parts[2];
	out.weight = 50;
	return true;
}

// Parse a glob2 line: weight:mimetype:glob[:flags]
bool ParseGlob2Line(const String& line, MimeGlob2& out)
{
	Vector<String> parts = SplitKeep(line, ':');
	if(parts.GetCount() < 3)
		return false;
	out.weight = atoi(parts[0]);
	out.mime = parts[1];
	out.pattern = parts[2];
	if(parts.GetCount() >= 4)
		out.flags = parts[3];
	return true;
}

// Parse an alias line: alias:mime
bool ParseAliasLine(const String& line, MimeAlias& out)
{
	int pos = line.Find(':');
	if(pos < 0)
		return false;
	out.alias = line.Mid(0, pos);
	out.mime = line.Mid(pos + 1);
	return true;
}

// Parse a subclass line: child:parent
bool ParseSubclassLine(const String& line, MimeSubclass& out)
{
	int pos = line.Find(':');
	if(pos < 0)
		return false;
	out.child = line.Mid(0, pos);
	out.parent = line.Mid(pos + 1);
	return true;
}

// Parse a simplified magic line: priority:mimetype:match1;match2;...
// A match is "offset:value" or "offset:value:mask". The real shared-mime-info
// magic file is a multi-line format with '>' continuation lines, ranges and
// typed values; this simplified single-line form covers the common sniffing
// case and is what the fixture exercises.
bool ParseMagicLine(const String& line, MimeMagic& out)
{
	int first = line.Find(':');
	if(first < 0)
		return false;
	int second = line.Find(':', first + 1);
	if(second < 0)
		return false;
	out.priority = atoi(line.Mid(0, first));
	out.mime = line.Mid(first + 1, second - first - 1);
	String rest = line.Mid(second + 1);
	for(const String& m : Split(rest, ';')) {
		if(m.GetCount())
			out.matches.Add(m);
	}
	return true;
}

// Load a single MIME cache file (globs, globs2, aliases, subclasses, magic).
void LoadMimeCache(const String& cache_dir,
                   Vector<MimeGlob>& globs,
                   Vector<MimeGlob2>& globs2,
                   Vector<MimeAlias>& aliases,
                   Vector<MimeSubclass>& subclasses,
                   Vector<MimeMagic>& magic)
{
	// globs
	{
		String file = AppendFileName(cache_dir, "globs");
		if(FileExists(file)) {
			String text = LoadFile(file);
			for(const String& line : Split(text, '\n')) {
				MimeGlob g;
				if(ParseGlobLine(line, g))
					globs.Add(g);
			}
		}
	}
	
	// globs2
	{
		String file = AppendFileName(cache_dir, "globs2");
		if(FileExists(file)) {
			String text = LoadFile(file);
			for(const String& line : Split(text, '\n')) {
				MimeGlob2 g;
				if(ParseGlob2Line(line, g))
					globs2.Add(g);
			}
		}
	}
	
	// aliases
	{
		String file = AppendFileName(cache_dir, "aliases");
		if(FileExists(file)) {
			String text = LoadFile(file);
			for(const String& line : Split(text, '\n')) {
				MimeAlias a;
				if(ParseAliasLine(line, a))
					aliases.Add(a);
			}
		}
	}
	
	// subclasses
	{
		String file = AppendFileName(cache_dir, "subclasses");
		if(FileExists(file)) {
			String text = LoadFile(file);
			for(const String& line : Split(text, '\n')) {
				MimeSubclass s;
				if(ParseSubclassLine(line, s))
					subclasses.Add(s);
			}
		}
	}
	
	// magic
	{
		String file = AppendFileName(cache_dir, "magic");
		if(FileExists(file)) {
			String text = LoadFile(file);
			for(const String& line : Split(text, '\n')) {
				MimeMagic m;
				if(ParseMagicLine(line, m))
					magic.Add(pick(m));
			}
		}
	}
}

// Load MIME info (descriptions, generic icons).
void LoadMimeInfo(const String& cache_dir, Vector<MimeInfo>& info)
{
	String file = AppendFileName(cache_dir, "mimeinfo.cache");
	if(!FileExists(file))
		return;
	
	String text = LoadFile(file);
	for(const String& line : Split(text, '\n')) {
		if(line.GetCount() == 0 || line[0] == '#')
			continue;
		int eq = line.Find('=');
		if(eq < 0)
			continue;
		String mime = line.Mid(0, eq);
		String rest = line.Mid(eq + 1);
		MimeInfo i;
		i.mime = mime;
		// description/icon not in cache; would come from mime/*.xml files
		info.Add(i);
	}
}

// Glob matching (simple fnmatch-style).
bool MatchGlob(const String& pattern, const String& filename)
{
	int p = 0, f = 0;
	while(p < pattern.GetCount() && f < filename.GetCount()) {
		if(pattern[p] == '*') {
			p++;
			if(p >= pattern.GetCount())
				return true;
			while(f < filename.GetCount()) {
				if(MatchGlob(pattern.Mid(p), filename.Mid(f)))
					return true;
				f++;
			}
			return false;
		}
		else if(pattern[p] == '?') {
			p++; f++;
		}
		else if(pattern[p] == '[') {
			p++;
			bool negate = false;
			if(p < pattern.GetCount() && pattern[p] == '!') {
				negate = true;
				p++;
			}
			bool match = false;
			while(p < pattern.GetCount() && pattern[p] != ']') {
				if(p + 2 < pattern.GetCount() && pattern[p+1] == '-') {
					if(filename[f] >= pattern[p] && filename[f] <= pattern[p+2])
						match = true;
					p += 3;
				}
				else {
					if(filename[f] == pattern[p])
						match = true;
					p++;
				}
			}
			if(p < pattern.GetCount() && pattern[p] == ']')
				p++;
			if(match == negate)
				return false;
			f++;
		}
		else {
			if(pattern[p] != filename[f])
				return false;
			p++; f++;
		}
	}
	while(p < pattern.GetCount() && pattern[p] == '*')
		p++;
	return p == pattern.GetCount() && f == filename.GetCount();
}

// Match a glob honoring the optional case-insensitivity flag from the spec
// ("ci" means match without regard to case; anything else is case sensitive).
bool MatchGlobFlags(const String& pattern, const String& filename, const String& flags)
{
	if(flags == "ci")
		return MatchGlob(ToLower(pattern), ToLower(filename));
	return MatchGlob(pattern, filename);
}

// Magic matching (simplified: offset:value[:mask]).
bool MatchMagic(const String& file_path, const MimeMagic& magic)
{
	if(!FileExists(file_path))
		return false;
	
	FileIn in(file_path);
	if(!in.IsOpen())
		return false;
	
	for(const String& m : magic.matches) {
		Vector<String> parts = SplitKeep(m, ':');
		if(parts.GetCount() < 2)
			continue;
		
		int offset = atoi(parts[0]);
		String value = parts[1];
		String mask = parts.GetCount() >= 3 ? parts[2] : String();
		
if(offset > 0)
			in.Seek(offset);
		
		String buf;
		buf.Cat(value);
		int read_count = value.GetCount();
		const char* buf_ptr = ~buf;
		if(in.Get((void*)buf_ptr, read_count) != read_count)
			continue;
		
		if(mask.GetCount()) {
			if(mask.GetCount() != value.GetCount())
				continue;
			for(int i = 0; i < value.GetCount(); i++) {
				if((buf[i] & mask[i]) != (value[i] & mask[i]))
					goto next_match;
			}
		}
		else {
			if(buf != value)
				continue;
		}
		return true;
	next_match:;
	}
	return false;
}

} // anonymous

void LoadMimeDatabase(const MimeLookupConfig& cfg,
                      Vector<MimeGlob>& globs,
                      Vector<MimeGlob2>& globs2,
                      Vector<MimeAlias>& aliases,
                      Vector<MimeSubclass>& subclasses,
                      Vector<MimeMagic>& magic,
                      Vector<MimeInfo>& info)
{
	Vector<String> roots;
	if(cfg.mime_roots.GetCount() > 0)
		roots <<= pick(cfg.mime_roots);
	else {
		XdgRoots xdg = XdgRoots::FromEnv();
		Vector<String> data_dirs = Split(xdg.data_dirs, ':');
		for(const String& d : data_dirs)
			if(d.GetCount())
				roots.Add(AppendFileName(d, "mime"));
	}
	
	for(const String& root : roots) {
		if(!DirectoryExists(root))
			continue;
		LoadMimeCache(root, globs, globs2, aliases, subclasses, magic);
		LoadMimeInfo(root, info);
	}
	
	// Sort by weight descending (more specific first)
	std::sort(globs.begin(), globs.end(), [](const MimeGlob& a, const MimeGlob& b) {
		return a.weight > b.weight;
	});
	std::sort(globs2.begin(), globs2.end(), [](const MimeGlob2& a, const MimeGlob2& b) {
		return a.weight > b.weight;
	});
	std::sort(magic.begin(), magic.end(), [](const MimeMagic& a, const MimeMagic& b) {
		return a.priority > b.priority;
	});
}

MimeDetectResult DetectMimeFromName(const MimeLookupConfig& cfg,
                                    const Vector<MimeGlob>& globs,
                                    const Vector<MimeGlob2>& globs2,
                                    const Vector<MimeAlias>& aliases,
                                    const String& filename)
{
	MimeDetectResult result;
	String base = GetFileName(filename);
	
	// globs2 is the modern, weighted form: try it first, most specific weight
	// first (LoadMimeDatabase already sorted by descending weight).
	for(const MimeGlob2& g : globs2) {
		if(MatchGlobFlags(g.pattern, base, g.flags)) {
			result.mime = g.mime;
			result.source = "glob2";
			result.weight = g.weight;
			result.found = true;
			return result;
		}
	}
	
	// Then the legacy globs form (all the same default weight).
	for(const MimeGlob& g : globs) {
		if(MatchGlobFlags(g.pattern, base, g.flags)) {
			result.mime = g.mime;
			result.source = "glob";
			result.weight = g.weight;
			result.found = true;
			return result;
		}
	}
	
	// No match from filename
	return result;
}

MimeDetectResult DetectMimeFromContent(const MimeLookupConfig& cfg,
                                       const Vector<MimeMagic>& magic,
                                       const String& file_path)
{
	MimeDetectResult result;
	
	for(const MimeMagic& m : magic) {
		if(MatchMagic(file_path, m)) {
			result.mime = m.mime;
			result.source = "magic";
			result.weight = m.priority;
			result.found = true;
			return result;
		}
	}
	
	return result;
}

MimeDetectResult DetectMime(const MimeLookupConfig& cfg,
                            const Vector<MimeGlob>& globs,
                            const Vector<MimeGlob2>& globs2,
                            const Vector<MimeAlias>& aliases,
                            const Vector<MimeMagic>& magic,
                            const String& file_path,
                            const String& filename_hint)
{
	MimeDetectResult result = DetectMimeFromName(cfg, globs, globs2, aliases,
	                                             filename_hint.GetCount() ? filename_hint : GetFileName(file_path));
	
	if(result.found) {
		result.mime = ResolveMimeAlias(aliases, result.mime);
		return result;
	}
	
	if(cfg.use_magic) {
		result = DetectMimeFromContent(cfg, magic, file_path);
		if(result.found) {
			result.mime = ResolveMimeAlias(aliases, result.mime);
			return result;
		}
	}
	
	// Default
	result.mime = "application/octet-stream";
	result.source = "default";
	result.weight = 0;
	result.found = true;
	return result;
}

MimeInfo GetMimeInfo(const Vector<MimeInfo>& info, const String& mime)
{
	for(const MimeInfo& i : info) {
		if(i.mime == mime)
			return i;
	}
	MimeInfo r;
	r.mime = mime;
	return r;
}

String ResolveMimeAlias(const Vector<MimeAlias>& aliases, const String& mime)
{
	Index<String> visited;
	String current = mime;
	while(true) {
		if(visited.Find(current) >= 0)
			break; // cycle
		visited.Add(current);
		bool found = false;
		for(const MimeAlias& a : aliases) {
			if(a.alias == current) {
				current = a.mime;
				found = true;
				break;
			}
		}
		if(!found)
			break;
	}
	return current;
}

bool IsMimeSubclass(const Vector<MimeSubclass>& subclasses, const String& mime, const String& parent)
{
	Vector<String> parents = GetMimeParents(subclasses, mime);
	for(const String& p : parents) {
		if(p == parent)
			return true;
	}
	return false;
}

Vector<String> GetMimeParents(const Vector<MimeSubclass>& subclasses, const String& mime)
{
	Vector<String> out;
	Index<String> visited;
	Vector<String> stack = { mime };
	
	while(stack.GetCount()) {
		String cur = stack.Pop();
		if(visited.Find(cur) >= 0)
			continue;
		visited.Add(cur);
		for(const MimeSubclass& s : subclasses) {
			if(s.child == cur) {
				out.Add(s.parent);
				stack.Add(s.parent);
			}
		}
	}
	return out;
}

} // Upp