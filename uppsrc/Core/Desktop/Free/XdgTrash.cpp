// XdgTrash.cpp
//
// Trash handling per the FreeDesktop Trash Specification.

#include <Core/Desktop/Free/XdgTrash.h>
#include <Core/Desktop/Free/XdgDirs.h>

namespace Upp {

// Parse a .trashinfo file.
bool ParseTrashInfo(const String& file, TrashEntry& out)
{
	if(!FileExists(file))
		return false;
	
	String text = LoadFile(file);
	if(text.IsEmpty())
		return false;
	
	String path, date;
	for(const String& line : Split(text, '\n')) {
		String l = TrimBoth(line);
		if(l.StartsWith("Path=")) {
			path = l.Mid(5);
		}
		else if(l.StartsWith("DeletionDate=")) {
			date = l.Mid(13);
		}
	}
	
	if(path.IsEmpty())
		return false;
	
	out.original_path = path;
	out.trash_info = file;
	out.trash_file = file;
	out.trash_file.Replace("/info/", "/files/");
	out.trash_file.Replace(".trashinfo", "");
	out.deletion_date = date;
	out.valid = true;
	return true;
}

// Forward declaration
String GetTrashDate();

namespace {

// Copy directory tree
bool CopyDirTree(const String& src, const String& dst)
{
	if(!DirectoryExists(src))
		return false;
	RealizeDirectory(dst);
	FindFile ff(AppendFileName(src, "*"));
	while(ff) {
		String src_path = ff.GetPath();
		String dst_path = AppendFileName(dst, ff.GetName());
		if(ff.IsFolder()) {
			if(!CopyDirTree(src_path, dst_path))
				return false;
		}
		else {
			if(!FileCopy(src_path, dst_path))
				return false;
		}
		ff.Next();
	}
	return true;
}

// Delete directory tree
bool DeleteDirTree(const String& path)
{
	if(!DirectoryExists(path))
		return true;
	FindFile ff(AppendFileName(path, "*"));
	while(ff) {
		String path_full = ff.GetPath();
		if(ff.IsFolder()) {
			if(!DeleteDirTree(path_full))
				return false;
		}
		else {
			if(!FileDelete(path_full))
				return false;
		}
		ff.Next();
	}
#ifdef PLATFORM_POSIX
	return ::rmdir(~path) == 0;
#else
	return false;
#endif
}

// Find the trash directory for a given file's filesystem.
// Returns the trash root that is on the same device as `path`.
TrashDir FindTrashForPath(const String& path, const Vector<TrashDir>& trashes)
{
	// Get device of the file
	int64 dev = 0;
#ifdef PLATFORM_POSIX
	struct stat st;
	if(stat(path, &st) == 0)
		dev = st.st_dev;
#endif
	
	if(dev != 0) {
		for(const TrashDir& t : trashes) {
			struct stat st;
			if(stat(t.root, &st) == 0 && st.st_dev == dev)
				return t;
		}
	}
	
	// Fall back to home trash
	for(const TrashDir& t : trashes) {
		if(t.is_home)
			return t;
	}
	
	return TrashDir();
}

// Ensure trash directories exist.
bool EnsureTrashDirs(const TrashDir& trash)
{
	if(!DirectoryExists(trash.files))
		RealizeDirectory(trash.files);
	if(!DirectoryExists(trash.info))
		RealizeDirectory(trash.info);
	return DirectoryExists(trash.files) && DirectoryExists(trash.info);
}

} // anonymous

Vector<TrashDir> FindTrashDirs(const TrashConfig& cfg)
{
	Vector<TrashDir> out;
	
	// Home trash
	TrashDir home_trash;
	String home = cfg.home_dir.GetCount() ? cfg.home_dir : GetHomeDirectory();
	if(home.IsEmpty())
		home = GetEnv("HOME");
	
	if(home.GetCount()) {
		home_trash.root = AppendFileName(home, ".local/share/Trash");
		home_trash.files = AppendFileName(home_trash.root, "files");
		home_trash.info = AppendFileName(home_trash.root, "info");
		home_trash.is_home = true;
		out.Add(home_trash);
	}
	
	// Per-volume trashes from XDG_DATA_DIRS
	Vector<String> data_dirs;
	if(cfg.top_dirs.GetCount() > 0)
		data_dirs <<= pick(cfg.top_dirs);
	else {
		XdgRoots xdg = XdgRoots::FromEnv();
		data_dirs = Split(xdg.data_dirs, ':');
	}
	
	for(const String& dir : data_dirs) {
		if(dir == "/usr/share" || dir == "/usr/local/share")
			continue; // skip system dirs that don't have user-writable trash
		
		TrashDir t;
		t.root = AppendFileName(dir, "Trash");
		t.files = AppendFileName(t.root, "files");
		t.info = AppendFileName(t.root, "info");
		t.is_home = false;
		out.Add(t);
	}
	
	return out;
}

Vector<TrashEntry> ListTrash(const TrashDir& trash)
{
	Vector<TrashEntry> out;
	if(!DirectoryExists(trash.info))
		return out;
	
	FindFile ff(AppendFileName(trash.info, "*.trashinfo"));
	while(ff) {
		TrashEntry e;
		if(ParseTrashInfo(ff.GetPath(), e))
			out.Add(e);
		ff.Next();
	}
	return out;
}

String GenerateTrashName(const TrashDir& trash, const String& original_name)
{
	String base = GetFileName(original_name);
	String name = base;
	String ext = GetFileExt(base);
	String stem = GetFileTitle(base);
	
	int counter = 0;
	while(true) {
		String candidate = counter == 0 ? base : Format("%s.%d%s", stem, counter, ext);
		String full = AppendFileName(trash.files, candidate);
		if(!FileExists(full))
			return candidate;
		counter++;
		if(counter > 10000)
			return Format("%s.%d%s", stem, (int)GetTickCount(), ext);
	}
}

String CreateTrashInfo(const String& original_path, const String& deletion_date)
{
	String info;
	info << "[Trash Info]\n";
	info << "Path=" << original_path << "\n";
	info << "DeletionDate=" << deletion_date << "\n";
	return info;
}

TrashResult MoveToTrash(const TrashConfig& cfg, const Vector<String>& files)
{
	TrashResult result;
	Vector<TrashDir> trashes = FindTrashDirs(cfg);
	
	if(trashes.GetCount() == 0) {
		result.status = DESKTOP_UNSUPPORTED;
		result.detail = "No trash directory available";
		return result;
	}
	
	for(const String& f : files) {
		if(!FileExists(f) && !DirectoryExists(f)) {
			result.failed_files.Add(f);
			continue;
		}
		
		TrashDir trash = FindTrashForPath(f, trashes);
		if(trash.root.IsEmpty()) {
			result.failed_files.Add(f);
			continue;
		}
		
		if(!EnsureTrashDirs(trash)) {
			result.failed_files.Add(f);
			continue;
		}
		
		String trash_name = GenerateTrashName(trash, f);
		String trash_file = AppendFileName(trash.files, trash_name);
		String trash_info_file = AppendFileName(trash.info, trash_name + ".trashinfo");
		
		// Try rename first (same filesystem)
		bool ok = false;
		if(FileExists(f)) {
			ok = FileMove(f, trash_file);
		}
		else if(DirectoryExists(f)) {
			ok = FileMove(f, trash_file);
		}
		
		// Cross-filesystem: copy then delete
		if(!ok) {
			if(FileExists(f)) {
				if(FileCopy(f, trash_file))
					ok = FileDelete(f);
			}
			else if(DirectoryExists(f)) {
				if(CopyDirTree(f, trash_file))
					ok = DeleteDirTree(f);
			}
		}
		
		if(!ok) {
			result.failed_files.Add(f);
			continue;
		}
		
		// Write .trashinfo
		String info = CreateTrashInfo(GetFullPath(f), GetTrashDate());
		if(!SaveFile(trash_info_file, info)) {
			result.failed_files.Add(f);
			continue;
		}
	}
	
	if(result.failed_files.GetCount() == files.GetCount()) {
		result.status = DESKTOP_ERROR;
		result.detail = "All files failed to move to trash";
	}
	else if(result.failed_files.GetCount() > 0) {
		result.status = DESKTOP_ERROR;
		result.detail = "Some files failed to move to trash";
	}
	
	return result;
}

TrashResult RestoreFromTrash(const TrashConfig& cfg, const Vector<String>& trash_info_files)
{
	TrashResult result;
	
	for(const String& info_file : trash_info_files) {
		TrashEntry e;
		if(!ParseTrashInfo(info_file, e) || !e.valid) {
			result.failed_files.Add(info_file);
			continue;
		}
		
		if(!FileExists(e.trash_file) && !DirectoryExists(e.trash_file)) {
			result.failed_files.Add(info_file);
			continue;
		}
		
		// Ensure parent directory exists
		String parent = GetFileFolder(e.original_path);
		if(!DirectoryExists(parent))
			RealizeDirectory(parent);
		
		bool ok = false;
		if(FileExists(e.trash_file)) {
			ok = FileMove(e.trash_file, e.original_path);
		}
		else if(DirectoryExists(e.trash_file)) {
			ok = FileMove(e.trash_file, e.original_path);
		}
		
		if(!ok) {
			result.failed_files.Add(info_file);
			continue;
		}
		
		// Remove .trashinfo
		FileDelete(info_file);
	}
	
	if(result.failed_files.GetCount() == trash_info_files.GetCount()) {
		result.status = DESKTOP_ERROR;
		result.detail = "All files failed to restore";
	}
	else if(result.failed_files.GetCount() > 0) {
		result.status = DESKTOP_ERROR;
		result.detail = "Some files failed to restore";
	}
	
	return result;
}

TrashResult DeleteFromTrash(const TrashConfig& cfg, const Vector<String>& trash_info_files)
{
	TrashResult result;
	
	for(const String& info_file : trash_info_files) {
		TrashEntry e;
		if(!ParseTrashInfo(info_file, e) || !e.valid) {
			result.failed_files.Add(info_file);
			continue;
		}
		
		bool ok = true;
		if(FileExists(e.trash_file))
			ok = FileDelete(e.trash_file);
		else if(DirectoryExists(e.trash_file))
			ok = DeleteDirTree(e.trash_file);
		
		if(ok)
			FileDelete(info_file);
		else
			result.failed_files.Add(info_file);
	}
	
	if(result.failed_files.GetCount() == trash_info_files.GetCount()) {
		result.status = DESKTOP_ERROR;
		result.detail = "All files failed to delete";
	}
	else if(result.failed_files.GetCount() > 0) {
		result.status = DESKTOP_ERROR;
		result.detail = "Some files failed to delete";
	}
	
	return result;
}

TrashResult EmptyTrash(const TrashConfig& cfg)
{
	TrashResult result;
	Vector<TrashDir> trashes = FindTrashDirs(cfg);
	
	for(const TrashDir& trash : trashes) {
		Vector<TrashEntry> entries = ListTrash(trash);
		Vector<String> info_files;
		for(const TrashEntry& e : entries)
			info_files.Add(e.trash_info);
		
		TrashResult r = DeleteFromTrash(cfg, info_files);
		result.failed_files.Append(r.failed_files);
	}
	
	if(result.failed_files.GetCount() > 0) {
		result.status = DESKTOP_ERROR;
		result.detail = "Some trash entries failed to delete";
	}
	
	return result;
}

TrashEntry FindTrashEntry(const TrashConfig& cfg, const String& original_path)
{
	Vector<TrashDir> trashes = FindTrashDirs(cfg);
	String abs_path = GetFullPath(original_path);
	
	for(const TrashDir& trash : trashes) {
		Vector<TrashEntry> entries = ListTrash(trash);
		for(const TrashEntry& e : entries) {
			if(e.original_path == abs_path)
				return e;
		}
	}
	
	return TrashEntry();
}

// Ensure trash directories exist.
bool EnsureTrashDirs(const TrashDir& trash)
{
	if(!DirectoryExists(trash.files))
		RealizeDirectory(trash.files);
	if(!DirectoryExists(trash.info))
		RealizeDirectory(trash.info);
	return DirectoryExists(trash.files) && DirectoryExists(trash.info);
}

// Find the trash directory for a given file's filesystem.
// Returns the trash root that is on the same device as `path`.
TrashDir FindTrashForPath(const String& path, const Vector<TrashDir>& trashes)
{
	// Get device of the file
	int64 dev = 0;
#ifdef PLATFORM_POSIX
	struct stat st;
	if(stat(path, &st) == 0)
		dev = st.st_dev;
#endif
	
	if(dev != 0) {
		for(const TrashDir& t : trashes) {
			struct stat st;
			if(stat(t.root, &st) == 0 && st.st_dev == dev)
				return t;
		}
	}
	
	// Fall back to home trash
	for(const TrashDir& t : trashes) {
		if(t.is_home)
			return t;
	}
	
	return TrashDir();
}

// Get current time in ISO 8601 format for .trashinfo.
String GetTrashDate()
{
	Time t = GetSysTime();
	return Format("%04d-%02d-%02dT%02d:%02d:%02d",
	              t.year, t.month, t.day, t.hour, t.minute, t.second);
}

} // Upp