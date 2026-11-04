// DesktopFree.cpp
//
// FreeDesktop backend implementation of IDesktopBackend.

#include <Core/Desktop/Free/DesktopFree.h>
#include <Core/Desktop/DesktopBackend.h>

namespace Upp {

FreeDesktopConfig FreeDesktopConfig::FromEnv()
{
	FreeDesktopConfig cfg;
	cfg.xdg_roots = XdgRoots::FromEnv();
	
	// The data roots every service starts from; each module composes its own
	// leaf: mime/, applications/, icons/.
	Vector<String> data = cfg.xdg_roots.data_dirs.GetCount()
		? Split(cfg.xdg_roots.data_dirs, ':')
		: Vector<String>({"/usr/share"});
	
	cfg.icon_config.theme_roots <<= data;
	cfg.icon_config.fallback_theme = "hicolor";
	// The theme a GUI layer prefers wins, then the desktop's own hint, and
	// only then the hicolor fallback answers.
	cfg.icon_config.theme = GetPreferredIconTheme();
	if(cfg.icon_config.theme.IsEmpty())
		cfg.icon_config.theme = GetEnv("XDG_CURRENT_THEME");
	
	// MIME: the shared-mime-info database is the mime/ leaf of each data
	// directory; the user's XDG_DATA_HOME copy leads, magic stays enabled.
	cfg.mime_config.mime_roots.Clear();
	if(cfg.xdg_roots.data_home.GetCount())
		cfg.mime_config.mime_roots.Add(AppendFileName(cfg.xdg_roots.data_home, "mime"));
	for(const String& d : data)
		cfg.mime_config.mime_roots.Add(AppendFileName(d, "mime"));
	cfg.mime_config.use_magic = true;
	
	cfg.desktop_config.data_dirs <<= data;
	cfg.desktop_config.current_desktop = GetEnv("XDG_CURRENT_DESKTOP");
	
	cfg.trash_config.top_dirs <<= data;
	cfg.trash_config.home_dir = cfg.xdg_roots.home;
	
	return cfg;
}

// Cached MIME database (loaded once per process).
struct MimeCache {
	Vector<MimeGlob> globs;
	Vector<MimeGlob2> globs2;
	Vector<MimeAlias> aliases;
	Vector<MimeSubclass> subclasses;
	Vector<MimeMagic> magic;
	Vector<MimeInfo> info;
	bool loaded = false;
};

static MimeCache& GetMimeCache()
{
	static MimeCache cache;
	return cache;
}

void EnsureMimeCache(const MimeLookupConfig& cfg)
{
	MimeCache& cache = GetMimeCache();
	if(cache.loaded)
		return;
	LoadMimeDatabase(cfg, cache.globs, cache.globs2, cache.aliases,
	                 cache.subclasses, cache.magic, cache.info);
	cache.loaded = true;
}

// FreeDesktop backend implementation.
class FreeDesktopBackend : public IDesktopBackend {
public:
	const char* Name() const override { return "FreeDesktop"; }
	
	bool IsSupported() const override
	{
		return true;
	}
	
	String Diagnostics() const override
	{
		Vector<String> d;
		XdgRoots r = XdgRoots::FromEnv();
		d.Add("config_home: " + r.config_home);
		d.Add("data_home: " + r.data_home);
		d.Add("data_dirs: " + r.data_dirs);
		d.Add("desktop: " + GetEnv("XDG_CURRENT_DESKTOP"));
		return Join(d, ", ");
	}
	
	String GetUserDirectory(const String& xdg_var, const String& fallback, DesktopStatus& status) const override
	{
		// The shell-folder chain keeps byte-parity with the Core/App reader
		// it replaces, including its single-file selection and second key.
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		return ResolveShellFolder(cfg.xdg_roots, xdg_var, fallback, status);
	}
	
	DesktopMimeResult DetectMime(const String& path) const override
	{
		DesktopMimeResult result;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		EnsureMimeCache(cfg.mime_config);
		
		MimeDetectResult r = Upp::DetectMime(cfg.mime_config,
		                                GetMimeCache().globs, GetMimeCache().globs2,
		                                GetMimeCache().aliases, GetMimeCache().magic,
		                                path, String());
		
		if(r.found) {
			result.mime = r.mime;
			result.description = GetMimeInfo(GetMimeCache().info, r.mime).description;
			result.detail = "source: " + r.source;
			result.status = DESKTOP_OK;
		}
		else {
			result.status = DESKTOP_NOT_FOUND;
			result.detail = "No MIME match";
		}
		return result;
	}
	
	DesktopMimeResult GetMimeDescription(const String& mime) const override
	{
		DesktopMimeResult result;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		EnsureMimeCache(cfg.mime_config);
		
		MimeInfo info = GetMimeInfo(GetMimeCache().info, mime);
		if(info.mime.GetCount()) {
			result.mime = info.mime;
			result.description = info.description;
			result.status = DESKTOP_OK;
		}
		else {
			result.mime = mime;
			result.status = DESKTOP_NOT_FOUND;
		}
		return result;
	}
	
	DesktopIconResult LookupIcon(const String& mime_or_name, int size, int scale) const override
	{
		DesktopIconResult result;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		
		IconLookup lookup = Upp::LookupIcon(cfg.icon_config, mime_or_name, size, scale);
		
		if(lookup.found) {
			result.mime_or_name = mime_or_name;
			result.theme = lookup.theme;
			result.file_path = lookup.file;
			result.size = lookup.size;
			result.scale = lookup.scale;
			result.fallback_reason = lookup.fallback;
			result.status = DESKTOP_OK;
		}
		else {
			result.mime_or_name = mime_or_name;
			result.fallback_reason = lookup.fallback;
			result.status = DESKTOP_NOT_FOUND;
		}
		return result;
	}
	
	DesktopIconResult LookupAppIcon(const String& name, int size, int scale) const override
	{
		DesktopIconResult result;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		
		DesktopEntry entry;
		if(FindDesktopEntry(cfg.desktop_config, name, entry)) {
			if(entry.icon.GetCount()) {
				return LookupIcon(entry.icon, size, scale);
			}
			result.mime_or_name = name;
			result.fallback_reason = "No Icon= in desktop file";
			result.status = DESKTOP_NOT_FOUND;
		}
		else {
			result.mime_or_name = name;
			result.fallback_reason = "Desktop file not found";
			result.status = DESKTOP_NOT_FOUND;
		}
		return result;
	}
	
	DesktopApp GetDesktopApp(const String& desktop_file) const override
	{
		DesktopApp result;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		
		DesktopEntry entry;
		if(FindDesktopEntry(cfg.desktop_config, desktop_file, entry)) {
			result.name = entry.name;
			result.icon = entry.icon;
			result.exec = entry.exec;
			result.mime = Join(entry.mime_types, ";");
			result.file = entry.file;
			result.visible = ShouldShowEntry(entry, cfg.desktop_config.current_desktop);
			result.has_display = true;
			result.status = DESKTOP_OK;
		}
		else {
			result.status = DESKTOP_NOT_FOUND;
		}
		return result;
	}
	
	Vector<DesktopDefaultApp> GetDefaultApps(const String& mime_or_ext) const override
	{
		Vector<DesktopDefaultApp> out;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		
		String def = GetDefaultApp(cfg.desktop_config, mime_or_ext);
		if(def.GetCount()) {
			DesktopDefaultApp d;
			d.desktop_file = def;
			d.mime = mime_or_ext;
			d.status = DESKTOP_OK;
			out.Add(d);
		}
		
		return out;
	}
	
	DesktopTrashInfo GetTrashInfo() const override
	{
		DesktopTrashInfo result;
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		Vector<TrashDir> trashes = Upp::FindTrashDirs(cfg.trash_config);
		
		for(const TrashDir& t : trashes) {
			if(t.is_home) {
				result.trash_dir = t.files;
				result.info_dir = t.info;
				// A capability question must not create the directories it
				// asks about; can_move is what exists right now, and
				// MoveToTrash realizes what it needs when it runs.
				result.can_move = DirectoryExists(t.files);
				result.can_restore = true;
				result.can_empty = true;
				result.status = DESKTOP_OK;
				return result;
			}
		}
		
		if(trashes.GetCount()) {
			result.trash_dir = trashes[0].files;
			result.info_dir = trashes[0].info;
			result.can_move = DirectoryExists(trashes[0].files);
			result.can_restore = true;
			result.can_empty = true;
			result.status = DESKTOP_OK;
		}
		else {
			result.status = DESKTOP_UNSUPPORTED;
		}
		return result;
	}
	
	DesktopStatus MoveToTrash(const String& path, String& new_path) const override
	{
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		Vector<String> files = { path };
		Vector<String> failed;
		String detail;
		TrashResult r = Upp::MoveToTrash(cfg.trash_config, files);
		failed = pick(r.failed_files);
		detail = r.detail;
		if(r.status == DESKTOP_OK && failed.GetCount() == 0) {
			new_path = AppendFileName(Upp::FindTrashDirs(cfg.trash_config)[0].files, GetFileName(path));
			return DESKTOP_OK;
		}
		return r.status;
	}
	
	DesktopStatus RestoreTrash(const String& trash_path, String& original_path) const override
	{
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		Vector<String> trash_info_files = { trash_path };
		Vector<String> failed;
		String detail;
		TrashResult r = Upp::RestoreFromTrash(cfg.trash_config, trash_info_files);
		failed = pick(r.failed_files);
		detail = r.detail;
		if(r.status == DESKTOP_OK && failed.GetCount() == 0) {
			TrashEntry e;
			if(Upp::ParseTrashInfo(trash_path, e)) {
				original_path = e.original_path;
			}
			return DESKTOP_OK;
		}
		return r.status;
	}
	
	DesktopStatus EmptyTrash(String& error) const override
	{
		FreeDesktopConfig cfg = FreeDesktopConfig::FromEnv();
		TrashResult r = Upp::EmptyTrash(cfg.trash_config);
		error = r.detail;
		return r.status;
	}
};

// Register the FreeDesktop backend. The registration owns nothing: the
// singleton lives in static storage for the program's lifetime, so registering
// it and never freeing it is not a leak the heap diagnostic should report.
void RegisterFreeDesktopBackend()
{
	static FreeDesktopBackend singleton;
	RegisterDesktopBackend(&singleton);
}

} // Upp