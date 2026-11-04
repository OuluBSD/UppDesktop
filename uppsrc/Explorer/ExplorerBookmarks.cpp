#include "ExplorerBookmarks.h"

NAMESPACE_UPP

ExplorerBookmarks::ExplorerBookmarks()
{
	String error;
	if(!Load(error))
		EWARN(ExplorerChannels::MOUNT, "cannot read bookmarks: " << error);
}

String ExplorerBookmarks::FilePath()
{
	// GetConfigFolder() is already the *app's* config folder (.../u++/Explorer/), so
	// it must not be given the app name again -- that produced .../Explorer/Explorer.
	return AppendFileName(GetConfigFolder(), "bookmarks.txt");
}

String ExplorerBookmarks::Normalize(const String& path)
{
	String p = TrimLeft(TrimRight(path));   //< String::Trim truncates; it does not strip
	if(p.IsEmpty()) return p;
	// A relative bookmark would mean something different depending on where the app
	// was started from, so resolve it against the working directory now rather than
	// storing a name that quietly reinterprets itself later.
	if(p[0] != '/' && !(p.GetCount() > 1 && p[1] == ':'))
		p = GetCurrentDirectory() + '/' + p;
	// Collapse "." and ".." textually. This is not a full canonicalizer -- it does
	// not follow symlinks -- but it is enough to make the two spellings of a folder
	// a user is likely to type collapse onto each other.
	Vector<String> stack;
	for(const String& s : Split(p, "/")) {
		if(s == "..") {
			if(stack.GetCount()) stack.Remove(stack.GetCount() - 1);
		}
		else if(!s.IsEmpty() && s != ".")
			stack.Add(s);
	}
	if(!stack.GetCount()) return "/";
	String out;
	for(const String& s : stack)
		out << '/' << s;
	return out;
}

bool ExplorerBookmarks::Has(const String& path) const
{
	String p = Normalize(path);
	for(const String& e : entries)
		if(Normalize(e) == p) return true;
	return false;
}

bool ExplorerBookmarks::Add(const String& path, String& error)
{
	String p = Normalize(path);
	if(p.IsEmpty()) {
		error = "a bookmark needs a path";
		return false;
	}
	if(Has(p)) {
		error = "'" + p + "' is already bookmarked";
		return false;
	}
	entries.Add(p);
	if(!Save(error)) {
		// Roll the in-memory list back, so a failed write does not leave a bookmark
		// the user can see in the window but that is not in the file -- and will be
		// gone next run.
		entries.Remove(entries.GetCount() - 1);
		return false;
	}
	EINFO(ExplorerChannels::OPS, "bookmark added " << p);
	return true;
}

bool ExplorerBookmarks::Remove(const String& path, String& error)
{
	String p = Normalize(path);
	for(int i = 0; i < entries.GetCount(); i++)
		if(Normalize(entries[i]) == p) {
			entries.Remove(i);
			if(!Save(error)) return false;
			EINFO(ExplorerChannels::OPS, "bookmark removed " << p);
			return true;
		}
	error = "'" + p + "' is not bookmarked";
	return false;
}

bool ExplorerBookmarks::Load(String& error)
{
	entries.Clear();
	// FileIn/FileOut, not FileStream: the plain FileStream is the abstract base and
	// has no Open(const char*) to call.
	FileIn f;
	if(!f.Open(FilePath()))
		return true;   //< no file yet is an empty list, not a failure
	while(!f.IsEof()) {
		String line = TrimLeft(TrimRight(f.GetLine()));
		if(line.IsEmpty()) continue;
		if(line[0] == '#') continue;   //< comments, so the file is hand-editable
		entries.Add(Normalize(line));
	}
	return true;
}

bool ExplorerBookmarks::Save(String& error)
{
	String path = FilePath();
	RealizeDirectory(GetFileFolder(path));
	FileOut f;
	if(!f.Open(path)) {
		error = "cannot write '" + path + "'";
		return false;
	}
	f << "# Explorer bookmarks, one directory per line. Lines starting with # are ignored.\n";
	for(const String& e : entries)
		f << e << "\n";
	return true;
}

String ExplorerBookmarks::Describe() const
{
	if(!entries.GetCount()) return "no bookmarks";
	String s;
	for(const String& e : entries)
		s << "  " << e << "\n";
	return s;
}

ExplorerBookmarks& GetExplorerBookmarks()
{
	static ExplorerBookmarks bookmarks;
	return bookmarks;
}

END_UPP_NAMESPACE
