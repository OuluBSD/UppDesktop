#include "ExplorerModel.h"
#include "ExplorerPolicy.h"
#include <Core/Desktop/Desktop.h>
#include <Core/Desktop/Free/DesktopFree.h>

NAMESPACE_UPP

// The process's desktop backend, registered on first use. A static archive
// member is only linked when one of its symbols is referenced from elsewhere,
// so the registration has to be called for, not assumed; the same
// ONCELOCK-guarded actuator is what every consumer of the desktop services
// does.
static const IDesktopBackend *ModelBackend()
{
	ONCELOCK {
		RegisterFreeDesktopBackend();
	}
	return GetDesktopBackend();
}

// The hard table is the answer when no desktop backend answers. Kept verbatim
// so a process that linked no desktop services - the headless twin above all -
// classifies files exactly as it always did.
static String HardcodedMimeFromExtension(const String& e)
{
	if(e.IsEmpty()) return "application/octet-stream";
	
	if(e == "txt" || e == "md" || e == "log" || e == "ini" || e == "cfg" || e == "conf")
		return "text/plain";
	if(e == "html" || e == "htm" || e == "xhtml")
		return "text/html";
	if(e == "css")
		return "text/css";
	if(e == "js" || e == "mjs")
		return "text/javascript";
	if(e == "json")
		return "application/json";
	if(e == "xml" || e == "xsd" || e == "xsl")
		return "application/xml";
	if(e == "pdf")
		return "application/pdf";
	if(e == "zip" || e == "tar" || e == "gz" || e == "bz2" || e == "xz" || e == "7z" || e == "rar")
		return "application/zip";
	if(e == "png" || e == "jpg" || e == "jpeg" || e == "gif" || e == "bmp" || e == "tiff" || e == "webp" || e == "svg")
		return "image/png";
	if(e == "mp3" || e == "wav" || e == "ogg" || e == "flac" || e == "m4a")
		return "audio/mpeg";
	if(e == "mp4" || e == "avi" || e == "mkv" || e == "mov" || e == "webm")
		return "video/mp4";
	if(e == "c" || e == "h" || e == "cpp" || e == "hpp" || e == "py" || e == "rs" || e == "go" || e == "java" || e == "js" || e == "ts")
		return "text/x-source";
	if(e == "sh" || e == "bash" || e == "zsh")
		return "application/x-shellscript";
	if(e == "py")
		return "text/x-python";
	if(e == "exe" || e == "dll" || e == "so" || e == "dylib")
		return "application/x-executable";
	
	return "application/octet-stream";
}

// The extension to MIME mapping asks the desktop backend, whose DetectMime
// reads the same shared-mime-info database every FreeDesktop application
// reads (globs2 with weights, aliases, magic). A process that linked no
// desktop backend, or one that cannot answer, falls back to the hard table
// above: same classes, same values.
static String GetMimeFromExtension(const String& ext)
{
	const IDesktopBackend *backend = ModelBackend();
	if(backend && backend->IsSupported() && !ext.IsEmpty()) {
		// `ext` arrives dot-less; the glob tables match a whole name, so the
		// detector gets one that carries the extension. The lowercasing is the
		// old rule: the tables match lowercase unless a glob is flagged "ci".
		DesktopMimeResult m = backend->DetectMime(String("x.") + ToLower(ext));
		if(m.IsOk() && m.mime.GetCount())
			return m.mime;
	}
	return HardcodedMimeFromExtension(ToLower(ext));
}

ExplorerEntry::ExplorerEntry(const VfsItem& item)
{
	name        = item.name;
	type        = item.type;
	is_dir      = item.type == VFS_DIRECTORY || item.type == VFS_SYMLINK;
	hidden      = ExplorerModel::IsHiddenName(item.name);
	ext         = item.type_str;
	data        = item.name;

	// Try to get MIME type from VFS item type_str if it looks like a MIME type
	// (contains '/'), otherwise derive from extension
	mime = item.type_str;
	if(mime.Find('/') < 0) {
		// Derive from extension if type_str is not a MIME type
		if(!ext.IsEmpty())
			mime = GetMimeFromExtension(ext);
		else if(is_dir)
			mime = "inode/directory";
		else
			mime = "application/octet-stream";
	}

	// Size and time are facts the listing already paid for: SystemFS hands them
	// back inside the item, and a VFS with nothing to say hands back a plain
	// VfsItem, which reads as "unknown" wherever the value is shown. The model
	// no longer stats behind the VFS's back, and no longer guesses from a
	// mount-type string -- a mount that shadows /home must not be answered from
	// the host path its name resembles.
	if(const SystemVfsItem* meta = dynamic_cast<const SystemVfsItem*>(&item)) {
		has_meta  = true;
		size      = meta->length;
		modified  = meta->modified;
		read_only = meta->read_only;
		unix_mode = meta->unix_mode;
		uid       = meta->uid;
		gid       = meta->gid;
		link      = meta->link;
	}
}

// One decimal, written out rather than through "%.1f": Sprintf follows the locale,
// so a Finnish desktop printed "26,2 GB" in the window while the twin printed
// "26.2 GB". Two binaries describing the same disk have to say the same thing, and a
// size is not a number anyone types back.
static String FormatOneDecimal(double v, const char *unit)
{
	int whole = (int)v;
	int tenth = (int)((v - whole) * 10 + 0.5);
	if(tenth > 9) { whole++; tenth = 0; }
	return Sprintf("%d.%d %s", whole, tenth, unit);
}

// Core has no byte formatter in this fork, and a properties panel that prints a raw
// byte count is not one. Scales the way a file manager does, and says "0 B" rather
// than an empty cell for a genuinely empty file.
String FormatSize(int64 bytes)
{
	if(bytes < 0) return String("?");
	static const char *units[] = { "B", "kB", "MB", "GB", "TB", "PB" };
	double v = (double)bytes;
	int u = 0;
	while(v >= 1024.0 && u < 5) { v /= 1024.0; u++; }
	return u == 0 ? Sprintf("%.0f %s", v, units[u])
	              : FormatOneDecimal(v, units[u]);
}



const char *ViewModeName(ViewMode mode)
{
	switch(mode) {
	case VIEW_LIST:        return "list";
	case VIEW_BIG_ICONS:   return "big-icons";
	case VIEW_SMALL_ICONS: return "small-icons";
	}
	return "?";
}

const char *IconKindName(IconKind kind)
{
	switch(kind) {
	case ICON_FOLDER:     return "folder";
	case ICON_MOUNT:      return "mount";
	case ICON_TEXT:       return "text";
	case ICON_IMAGE:      return "image";
	case ICON_AUDIO:      return "audio";
	case ICON_VIDEO:      return "video";
	case ICON_ARCHIVE:    return "archive";
	case ICON_CODE:       return "code";
	case ICON_EXECUTABLE: return "executable";
	case ICON_FILE:       return "file";
	}
	return "?";
}

// --- columns ---------------------------------------------------------------

// t_() rather than plain literals: these headings are drawn in the window *and*
// printed by the twin, and a column called "Size" in a Finnish window is a column
// the user has to guess at. The keys stay English, so an untranslated heading
// falls back to the word the rest of the code uses. The lookup happens **per
// call**: a static array of t_() results is filled at static-init, before the
// language is known, and froze the English keys into the heading strip of every
// Finnish window.
static const char *list_column_keys[LCOL_MAX] = {
	"Name", "Type", "Size", "Modified",
	"User", "Group", "Permissions", "Link",
	"Hidden", "Mounted as"
};

const char *ListColumnTitle(ListColumn c)
{
	if(c < 0 || c >= LCOL_MAX) return "";
	return t_(list_column_keys[c]);
}

//< The user and group ids, as numbers. Name lookup (getpwuid) is a per-row NSS
//< question the listing must not pay for; the number is the fact the stat holds,
//< and -1 (or a mount that did not answer) is unknown, which is what Windows is
//< truthful about all the time.
static String UidText(const ExplorerEntry& e)
{
	return !e.has_meta || e.uid < 0 ? String(t_("unknown")) : Sprintf("%d", e.uid);
}

static String GidText(const ExplorerEntry& e)
{
	return !e.has_meta || e.gid < 0 ? String(t_("unknown")) : Sprintf("%d", e.gid);
}

//< Three different facts, in the order they are knowable: the mount did not
//< answer; the platform has no mode word (Win32), where the only true statement
//< is read-only vs writable; the mode word is real, and the nine characters are
//< its rwxrwxrwx. Synthesizing rwx from a read-only flag would be inventing a
//< fact, which is exactly what the unknown case exists to prevent.
static String PermissionsText(const ExplorerEntry& e)
{
	if(!e.has_meta)
		return t_("unknown");
	if(e.unix_mode) {
		static const char *rwx[8] = { "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx" };
		dword m = e.unix_mode & 0777;
		String s;
		for(int shift = 6; shift >= 0; shift -= 3)
			s << rwx[(m >> shift) & 7];
		return s;
	}
	return e.read_only ? t_("read-only") : t_("writable");
}

String ListColumnValue(ListColumn c, const ExplorerEntry& e)
{
	switch(c) {
	case LCOL_NAME:
		return e.name;
	case LCOL_TYPE: {
		String type = e.IsDir() ? t_("Folder") : t_("File");
		if(!e.IsDir() && !e.ext.IsEmpty())
			type << " (" << e.ext << ")";
		return type;
	}
	case LCOL_SIZE:
		// The same three answers as the properties panel, for the same reason: a
		// VFS that exposes no size is not a zero-byte file, and a directory is
		// not an empty one.
		if(e.size < 0)     return t_("unknown");
		if(e.IsDir())      return "-";
		return FormatSize(e.size);
	case LCOL_MODIFIED:
		return IsNull(e.modified) ? String(t_("unknown"))
		                          : FormatTime(e.modified, "YYYY-MM-DD HH:mm:ss");
	case LCOL_USER:
		return UidText(e);
	case LCOL_GROUP:
		return GidText(e);
	case LCOL_PERMISSIONS:
		return PermissionsText(e);
	case LCOL_LINK:
		// Three facts: the mount did not answer, the entry is not a link (empty,
		// the same convention as Mounted as), it is a link and this is its target.
		if(!e.has_meta)
			return t_("unknown");
		if(e.type != VFS_SYMLINK)
			return "";
		return e.link.IsEmpty() ? String(t_("unknown")) : e.link;
	case LCOL_HIDDEN:
		return e.hidden ? "yes" : "no";
	case LCOL_MOUNTED:
		// Empty rather than "no": an entry that is not a mount point has no mount
		// type, which is not the same as having one that is empty.
		return e.mount_type;
	}
	return "";
}

int ListColumnWidth(ListColumn c)
{
	switch(c) {
	case LCOL_NAME:     return 260;   //< the one column that has to hold whole names
	case LCOL_TYPE:     return 120;
	case LCOL_SIZE:     return 90;
	case LCOL_MODIFIED: return 150;
	case LCOL_USER:     return 60;
	case LCOL_GROUP:    return 60;
	case LCOL_PERMISSIONS: return 100;
	case LCOL_LINK:     return 120;
	case LCOL_HIDDEN:   return 60;
	case LCOL_MOUNTED:  return 120;
	}
	return 100;
}

bool ListColumnDefaultVisible(ListColumn c)
{
	return c == LCOL_NAME || c == LCOL_TYPE || c == LCOL_SIZE || c == LCOL_MODIFIED;
}

int ListColumnSortKey(ListColumn c)
{
	switch(c) {
	case LCOL_NAME:     return ExplorerModel::NAME;
	case LCOL_TYPE:     return ExplorerModel::TYPE;
	case LCOL_SIZE:     return ExplorerModel::SIZE;
	case LCOL_MODIFIED: return ExplorerModel::TIME;
	default:          return -1;    //< a flag is not an order
	}
}

void ExplorerModel::SetColumnVisible(ListColumn c, bool on)
{
	if(c < 0 || c >= LCOL_MAX) return;
	// The name column stays. Without it the rows are values with nothing saying
	// which entry they belong to, and the only way back would be a dialog: a
	// listing that can be talked into being unusable is a bug, not a setting.
	if(!on && c == LCOL_NAME) return;
	column_visible[c] = on;
}

void ExplorerModel::ResetColumns()
{
	for(int i = 0; i < LCOL_MAX; i++)
		column_visible[i] = ListColumnDefaultVisible((ListColumn)i);
}

Vector<ListColumn> ExplorerModel::VisibleColumns() const
{
	Vector<ListColumn> v;
	for(int i = 0; i < LCOL_MAX; i++)
		if(column_visible[i])
			v.Add((ListColumn)i);
	if(v.IsEmpty())
		v.Add(LCOL_NAME);
	return v;
}

// A cell is the picture plus room for its name. The numbers are what the pictures
// are drawn at too, so the icon is never scaled after the fact. The two icon modes
// are laid out differently on purpose: the big one stacks picture over name, the
// small one puts them side by side, so their cells are not the same shape.
Size ViewCellSize(ViewMode mode)
{
	return ViewCellSizeAt(mode, VIEW_ZOOM_DEFAULT);
}

// The zoom levels are powers of two, so a doubled zoom doubles the picture exactly
// and the drawn icon set stays the only source of pictures. Levels outside the
// range are pulled to the nearest end rather than rejected: the two ways in here
// are a scroll bar and a command line, and neither can be "wrong" on purpose.
int ViewZoomClamp(int pixels)
{
	int p = pixels;
	if(p < VIEW_ZOOM_SMALLEST) p = VIEW_ZOOM_SMALLEST;
	if(p > VIEW_ZOOM_LARGEST) p = VIEW_ZOOM_LARGEST;
	int level = VIEW_ZOOM_SMALLEST;
	while(level * 2 <= p && level * 2 <= VIEW_ZOOM_LARGEST)
		level *= 2;
	return level;
}

int ViewIconSizeAt(ViewMode mode, int zoom)
{
	// The small mode never grows past what a list row can carry: a picture taller
	// than the row hides the name, which is the only other thing in the cell.
	int z = ViewZoomClamp(zoom);
	return mode == VIEW_SMALL_ICONS ? min(z, 32) : z;
}

Size ViewCellSizeAt(ViewMode mode, int zoom)
{
	int icon = ViewIconSizeAt(mode, zoom);
	if(mode == VIEW_BIG_ICONS)
		return Size(icon + 46, icon + 46);
	if(mode == VIEW_SMALL_ICONS)
		return Size(icon + 140, max(icon + 4, 22));
	return Size(0, 0);
}

int ViewColumns(ViewMode mode, int panel_width, int zoom)
{
	Size cell = ViewCellSizeAt(mode, zoom);
	if(cell.cx <= 0) return 0;                 //< the detailed mode has no cells
	return max(1, panel_width / cell.cx);
}

// Extension sets, longest patterns first where they matter: "tar.gz" has to beat
// "gz", because the compressed *tarball* is the thing a user is looking at.
static const char *ext_archives[] = { "gz",  "bz2", "xz",  "zst", "7z",  "rar",
	                                  "zip", "tar", "tgz", "tbz", "iso", nullptr };
static const char *ext_images[]   = { "png", "jpg", "jpeg", "gif", "bmp", "webp",
	                                  "svg", "tif", "tiff", nullptr };
static const char *ext_audio[]    = { "mp3", "flac", "ogg", "wav", "m4a", "opus", nullptr };
static const char *ext_video[]    = { "mp4", "mkv", "avi", "mov", "webm", "wmv", nullptr };
static const char *ext_code[]     = { "c",  "h",  "cpp", "hpp", "py",  "js",  "ts",
	                                  "rs", "go", "java", "sh",  "json", "xml", "md", nullptr };
static const char *ext_text[]     = { "txt", "log", "ini", "cfg", "conf", nullptr };

static bool ExtIn(const String& ext, const char *const *list)
{
	for(int i = 0; list[i]; i++)
		if(ext == list[i]) return true;
	return false;
}

IconKind EntryIconKind(const ExplorerEntry& e)
{
	if(e.IsDir())  return e.IsMount() ? ICON_MOUNT : ICON_FOLDER;
	if(e.IsMount()) return ICON_MOUNT;

	String ext = e.ext;
	ext = ToLower(ext);
	// The type field of an ordinary entry is its extension *with* the dot.
	if(ext.StartsWith(".")) ext = ext.Mid(1);

	if(ExtIn(ext, ext_archives)) return ICON_ARCHIVE;
	if(ExtIn(ext, ext_images))   return ICON_IMAGE;
	if(ExtIn(ext, ext_audio))    return ICON_AUDIO;
	if(ExtIn(ext, ext_video))    return ICON_VIDEO;
	if(ExtIn(ext, ext_code))     return ICON_CODE;
	if(ExtIn(ext, ext_text))     return ICON_TEXT;
	// A file with no extension at all is as likely to be a program as anything
	// else, and it is the one case where guessing "executable" is both cheap and
	// usually right.
	if(ext.IsEmpty())             return ICON_EXECUTABLE;
	return ICON_FILE;
}

Vector<ExplorerEntry::PropertyRow> EntryProperties(const ExplorerEntry& e,
                                                   const String& parent,
                                                   const String& mount,
                                                   bool can_modify)
{
	Vector<ExplorerEntry::PropertyRow> rows;
	rows.Add({ t_("Name"),     e.name });
	rows.Add({ t_("Location"), parent });

	String type = e.IsDir() ? t_("Folder") : t_("File");
	if(!e.IsDir() && !e.ext.IsEmpty())
		type << " (" << e.ext << ")";
	rows.Add({ t_("Type"),     type });

	String size;
	if(e.size < 0)     size = t_("unknown");   //< the VFS does not expose sizes
	else if(e.IsDir()) size = "-";        //< a directory has no size of its own
	else                size = FormatSize(e.size);
	rows.Add({ t_("Size"),     size });

	rows.Add({ t_("Modified"), IsNull(e.modified) ? String(t_("unknown"))
	                                           : FormatTime(e.modified, "YYYY-MM-DD HH:mm:ss") });
	rows.Add({ t_("Hidden"),   e.hidden ? t_("yes") : t_("no") });
	rows.Add({ t_("User"),        UidText(e) });
	rows.Add({ t_("Group"),       GidText(e) });
	rows.Add({ t_("Permissions"), PermissionsText(e) });
	// The link row exists only for a link: on anything else the target question
	// has no answer because there is no target, and a row of dashes would say
	// the VFS was asked and had nothing to say.
	if(e.type == VFS_SYMLINK)
		rows.Add({ t_("Link"), e.link.IsEmpty() ? String(t_("unknown")) : e.link });
	if(e.IsMount())
		rows.Add({ t_("Mounted as"), e.mount_type });
	rows.Add({ t_("Filesystem"), mount });
	// No free-space row here on purpose: EntryProperties() only sees the entry, so
	// filling it in would mean a statvfs per directory row while listing. xfe shows
	// free space in the status bar, which is where ExplorerModel::StatusText puts it.
	return rows;
}

bool ExplorerModel::IsHiddenName(const String& name)
{
	if(name.IsEmpty())
		return false;
	if(name == "." || name == "..")
		return false;
	#ifdef PLATFORM_WIN32
	return name[0] == '.' || name.Find(" (hidden)") >= 0;
	#else
	return name[0] == '.';
	#endif
}

const char *ExplorerModel::TypeName(VfsItemType t)
{
	switch(t) {
	case VFS_DIRECTORY: return "dir";
	case VFS_FILE:      return "file";
	case VFS_SYMLINK:   return "link";
	case VFS_NULL:      return "null";
	}
	return "?";
}

ExplorerModel::ExplorerModel()
	: mounts(MountManager::System())
{
	// Not a member initialiser list: the defaults are a function of the column
	// table above, and a second copy of that list here is a second place for the
	// two to disagree.
	ResetColumns();
}

String ExplorerModel::DescribeMounts() const
{
	String s;
	for(MountPoint* mp : mounts.Enumerate()) {
		s << mp->path.AsSysPath("/");
		if(!mp->type.IsEmpty()) s << " (" << mp->type << ")";
		s << ' ' << mp->Caps().ToString();
		s << '\n';
	}
	return s;
}

bool ExplorerModel::SetPath(const String& new_path, String& error)
{
	error.Clear();

	VfsPath vp;
	vp.Set(new_path);

	VfsPath rel;
	MountPoint* mp = mounts.FindDeepest(vp, &rel);

	if(!mp) {
		error = "no mount serves '" + new_path + "'";
		EERROR(ExplorerChannels::MODEL, error);
		return false;
	}

	// Remember what is shadowing the mount in use: a user who cannot see a file
	// should be able to find out why, and the GUI surfaces this.
	shadowing = nullptr;
	for(MountPoint* other : mounts.Enumerate()) {
		if(other == mp) continue;
		// other sits below mp when mp's path is a prefix of other's path.
		if(other->path.IsLeft(mp->path) && other->path.GetPartCount() > mp->path.GetPartCount())
			if(vp.IsLeft(other->path)) {
				shadowing = other;
				break;
			}
	}

	if(!mp->IsBrowsable()) {
		error = String("mount at '") + mp->path.AsSysPath("/") + "' is a "
		      + mp->Caps().ToString() + ", not a browsable filesystem";
		EERROR(ExplorerChannels::MODEL, error);
		return false;
	}

	if(!mp->vfs) {
		error = "mount has no VFS attached";
		EERROR(ExplorerChannels::MODEL, error);
		return false;
	}

	Vector<Shared<VfsItem>> items;
	if(!mp->vfs->GetFiles(rel, items)) {
		error = mp->vfs->last_error.IsEmpty()
		      ? String("failed to list '") + new_path + "'"
		      : mp->vfs->last_error;
		EERROR(ExplorerChannels::MODEL, error);
		return false;
	}

	String sys_root = mp->vfs == (VFS*)nullptr ? String() : String();

	path     = new_path;
	rel_path = rel.AsSysPath("/");
	mount    = mp;

	entries.Clear();
	for(const Shared<VfsItem>& item : items) {
		ExplorerEntry e(*item);
		entries.Add(pick(e));
	}

	// Mark the mount-point entries. VfsItem::type_str carries the file *extension*
	// for an ordinary entry and the mount type only for a mount point, so it cannot
	// be used to tell them apart: reading mount_type from it made every "notes.txt"
	// look like a mount point, and IsMount() then claimed it was one. Ask the mount
	// registry instead -- an entry named N in this directory is a mount point exactly
	// when something is mounted at this directory plus N.
	for(ExplorerEntry& e : entries) {
		MountPoint* mp = mounts.Find(path + '/' + e.name);
		if(mp)
			e.mount_type = mp->type;
	}

	if(shadowing)
		EINFO(ExplorerChannels::MODEL, "'" << path << "' is served by mount '"
			<< mp->path.AsSysPath("/") << "', shadowed by '"
			<< shadowing->path.AsSysPath("/") << "'");
	else
		EDEBUG(ExplorerChannels::MOUNT, "resolved '" << path << "' to mount '"
			<< mp->path.AsSysPath("/") << "' rel='" << rel_path << "'");

	// Record the visit for back/forward.
	if(history_pos < 0 || history[history_pos] != path) {
		while(history.GetCount() > history_pos + 1)
			history.Remove(history.GetCount() - 1);
		history.Add(path);
		history_pos = history.GetCount() - 1;
	}

	RefreshEntries();
	return true;
}

bool ExplorerModel::CanUp() const
{
	VfsPath vp;
	vp.Set(path);
	return vp.GetPartCount() > 0;
}

bool ExplorerModel::Up(String& error)
{
	if(!CanUp()) {
		error = "already at the mount root";
		return false;
	}
	String parent;
	VfsPath vp;
	vp.Set(path);
	vp.RemoveLast();
	parent = vp.AsSysPath("/");
	if(parent.IsEmpty()) parent = "/";
	return SetPath(parent, error);
}

bool ExplorerModel::Back(String& error)
{
	if(!CanBack()) {
		error = "no earlier location";
		return false;
	}
	history_pos--;
	String target = history[history_pos];
	// Do not re-record the visit we just undid.
	int saved = history_pos;
	bool ok = SetPath(target, error);
	history_pos = saved;
	return ok;
}

bool ExplorerModel::Forward(String& error)
{
	if(!CanForward()) {
		error = "no later location";
		return false;
	}
	history_pos++;
	String target = history[history_pos];
	int saved = history_pos;
	bool ok = SetPath(target, error);
	history_pos = saved;
	return ok;
}

void ExplorerModel::SetSort(SortBy by, SortDir dir)
{
	sort_by  = by;
	sort_dir = dir;
	RefreshEntries();
}

String ExplorerModel::DescribeSort() const
{
	String s = SortKeyName(sort_by);
	s << ' ' << (sort_dir == ASCENDING ? "ascending" : "descending");
	// Both toggles named whatever way they are, including the negative: a listing
	// that does not say which of the two settings are in force is one a user has to
	// open the menu to find out about.
	if(dirs_first) s << ", folders first";
	if(ignore_case) s << ", case ignored";
	else           s << ", case-sensitive";
	return s;
}

// Text comparison, optionally as if both were lower case, and always ending in a
// byte-wise tie-break.
//
// The tie-break is not decoration. With case ignored, "Notes" and "notes" compare
// equal, and an ordering that calls two entries equal leaves their relative order
// to std::sort -- which is not a stable sort, so the listing could come back
// differently ordered on the next refresh for a reason no user can see. The
// tie-break makes the order a function of the names alone.
static bool TextLess(const String& a, const String& b, bool ignore_case)
{
	if(ignore_case) {
		String la = ToLower(a), lb = ToLower(b);
		if(la != lb) return la < lb;
	}
	return a < b;
}

static bool EntryLess(const ExplorerEntry& a, const ExplorerEntry& b,
                      ExplorerModel::SortBy by, bool ignore_case)
{
	switch(by) {
	case ExplorerModel::NAME:
		return TextLess(a.name, b.name, ignore_case);
	case ExplorerModel::EXTENSION:
		return TextLess(a.ext, b.ext, ignore_case);
	case ExplorerModel::SIZE:
		return a.size < b.size;
	case ExplorerModel::TIME:
		return a.modified < b.modified;
	case ExplorerModel::TYPE:
		return TextLess(a.mount_type, b.mount_type, ignore_case);
	}
	return false;
}

void ExplorerModel::RefreshEntries()
{
	// Filter
	Vector<ExplorerEntry> kept;
	for(ExplorerEntry& e : entries) {
		if(!show_hidden && e.hidden)
			continue;
		if(!MatchesFilter(e))
			continue;
		kept.Add(pick(e));
	}
	entries = pick(kept);

	// Sort. Dirs first is the behaviour every file manager has, so it stays on by
	// default; the menu turns it off, and it is the model's flag rather than a
	// constant so that the twin and the window answer the same question about it.
	// std::sort is used directly: this fork's Vector has no Sort member, and the
	// comparator needs the sort key and direction captured.
	std::sort(entries.begin(), entries.end(),
	          [&](const ExplorerEntry& a, const ExplorerEntry& b) {
		if(dirs_first && a.is_dir != b.is_dir)
			return a.is_dir;
		// Descending is the comparator with its arguments *swapped*, not the
		// negation of its answer. "a is not less than b" is also true of two
		// entries that compare equal -- two files of one size, or two names that
		// differ only in case -- so negating claims each is less than the other,
		// and std::sort is handed an ordering with no transitivity to work with.
		// The visible symptom is a descending listing in an arbitrary order.
		return sort_dir == ASCENDING
			? EntryLess(a, b, sort_by, ignore_case)
			: EntryLess(b, a, sort_by, ignore_case);
	});
}

static bool GlobMatch(const String& pattern, const String& text, int pi, int ti)
{
	while(pi < pattern.GetCount()) {
		char p = pattern[pi];
		if(p == '*') {
			// Collapse runs of '*', then try every split point.
			while(pi < pattern.GetCount() && pattern[pi] == '*') pi++;
			if(pi == pattern.GetCount()) return true;
			for(int i = ti; i <= text.GetCount(); i++)
				if(GlobMatch(pattern, text, pi, i))
					return true;
			return false;
		}
		if(ti >= text.GetCount()) return false;
		if(p != '?' && p != text[ti]) return false;
		pi++;
		ti++;
	}
	return ti == text.GetCount();
}

void ExplorerModel::SetFilter(const String& pattern)
{
	filter = pattern;
	RefreshEntries();
}

bool ExplorerModel::MatchesFilter(const ExplorerEntry& e) const
{
	if(filter.IsEmpty())
		return true;
	return GlobMatch(filter, e.name, 0, 0);
}

Vector<MountPoint*> ExplorerModel::GetBrowsableMounts()
{
	return mounts.Enumerate(MountCapabilities::FILESYSTEM);
}

// Places are checked through the mount registry rather than the host: the sidebar
// belongs to the filesystem the user is looking at, and a mount that shadows /home
// is exactly the case where the two disagree.
static bool PlaceExists(MountManager& mounts, const String& path)
{
	if(path.IsEmpty()) return false;
	VfsPath vp;
	vp.Set(path);
	return mounts.DirectoryExists(vp);
}

String ExplorerModel::GetTrashPath()
{
	String home = GetHomeDirectory();
	if(home.IsEmpty()) return Null;
	// The home trash, answered by the desktop backend's Trash spec reader.
	// Row visibility stays a model decision: the mount manager's existence
	// check is the same one every other place row goes through, and unless a
	// backend capability is explicitly adopted, a trash that is not on the
	// mount's own filesystem is still not shown.
	const IDesktopBackend *backend = ModelBackend();
	if(backend && backend->IsSupported()) {
		DesktopTrashInfo info = backend->GetTrashInfo();
		if(!info.trash_dir.IsEmpty())
			return PlaceExists(mounts, info.trash_dir) ? info.trash_dir : Null;
	}
	String trash = home + "/.local/share/Trash/files";
	return PlaceExists(mounts, trash) ? trash : Null;
}

Vector<ExplorerModel::Place> ExplorerModel::GetXdgPlaces()
{
	Vector<Place> places;
	String home = GetHomeDirectory();
	if(home.IsEmpty()) return places;

	// The declared directories come from the desktop backend's user-dirs
	// parser, which reads ~/.config/user-dirs.dirs and expands $HOME the way
	// this function used to by hand. What the model adds is exactly what a
	// file manager cannot leave to the host: the mount manager's existence
	// check (a VFS mount must not be measured against the host filesystem!),
	// the home-pointing row that is not a place, already-listed dedupe, and
	// the translated labels. The backend's list is taken without its own
	// existence filter, because for a mount the question is the mount's.
	XdgRoots roots = XdgRoots::FromEnv(home);
	Vector<XdgUserDir> dirs = LoadUserDirs(roots, false);
	Index<String> seen;

	for(const XdgUserDir& d : dirs) {
		if(d.path.IsEmpty() || d.path == home) continue;
		if(!PlaceExists(mounts, d.path)) continue;
		if(seen.Find(d.path) >= 0) continue;
		seen.Add(d.path);
		// t_() on the label, not on the table: the table holds the keys, and a
		// translated string in a static table is a translated *key*, which is
		// looked up in whatever language happens to be in force at start-up.
		places.Add(Place(t_(d.label), d.path, "xdg"));
	}
	return places;
}

Vector<ExplorerModel::Place> ExplorerModel::GetPlaces(bool include_mounts)
{
	Vector<Place> places;

	// The filesystem itself, named the way a file manager names it rather than shown
	// as a bare "/", which reads like a path field left over.
	if(PlaceExists(mounts, "/"))
		places.Add(Place(t_("File system"), "/", "filesystem"));

	String home = GetHomeDirectory();
	if(!home.IsEmpty() && PlaceExists(mounts, home))
		places.Add(Place(t_("Home"), home, "home"));

	places.Append(GetXdgPlaces());

	String trash = GetTrashPath();
	if(!trash.IsEmpty())
		places.Add(Place(t_("Trash can"), trash, "trash"));

	if(!include_mounts) return places;

	// Every browsable mount is a drive. Deepest first, which is the registry's own
	// order, so an overlay appears above the mount it shadows -- the same order the
	// shadow detection uses, and the order a user needs to make sense of it.
	for(MountPoint* mp : mounts.Enumerate()) {
		if(!mp->IsBrowsable()) continue;
		String p = mp->path.AsSysPath("/");
		if(p.IsEmpty()) p = "/";
		String label = p;
		if(!mp->type.IsEmpty() && p != "/") label << "  [" << mp->type << "]";
		places.Add(Place(label, p, "mount"));
	}
	return places;
}

Vector<String> ExplorerModel::StatusCells() const
{
	int dirs = 0, files = 0;
	for(const ExplorerEntry& e : entries) {
		if(e.is_dir) dirs++;
		else files++;
	}
	String s;
	s << dirs << (dirs == 1 ? " directory" : " directories");
	s << ", " << files << (files == 1 ? " file" : " files");
	if(!filter.IsEmpty()) s << " matching '" << filter << "'";
	if(mount) s << " via " << mount->path.AsSysPath("/");
	// The free space is its own cell, not a continuation of the count: in 98 it
	// is a separate panel on the right of the strip, and a strip whose right-hand
	// cell is the tail of the left one cannot be laid out that way.
	int64 total, free_space;
	String space;
	if(GetDiskSpace(total, free_space))
		space = FormatSize(free_space) << " free of " << FormatSize(total);
	else
		space = "free space unknown";
	Vector<String> cells;
	cells.Add(s);
	cells.Add(space);
	return cells;
}

String ExplorerModel::StatusText() const
{
	return Join(StatusCells(), "   ");
}

// ---------------------------------------------------------------------------
// Operations
//
// Order matters here and is the same for every one of them: validate the names,
// authorize through ExplorerPolicy, and only then touch MountManager. Doing it in
// any other order means the policy log records attempts that were never real, or
// worse, that a rejected name still reached the filesystem.
// ---------------------------------------------------------------------------

void ExplorerModel::RereadQuiet()
{
	String error;
	if(!SetPath(path, error))
		EWARN(ExplorerChannels::OPS, "cannot re-read '" << path << "': " << error);
}

bool ExplorerModel::IsValidName(const String& name, String& error)
{
	if(name.IsEmpty()) {
		error = "the name is empty";
		return false;
	}
	if(name == "." || name == "..") {
		error = "'" + name + "' is not a name that can be created or removed";
		return false;
	}
	if(name.Find('/') >= 0 || name.Find('\\') >= 0) {
		error = "'" + name + "' contains a path separator; these operations act on a "
		              "single entry of the current directory";
		return false;
	}
	return true;
}

String ExplorerModel::PathUnder(const String& dir, const String& name)
{
	if(dir.IsEmpty()) return name;
	if(dir.EndsWith("/")) return dir + name;
	return dir + '/' + name;
}

String ExplorerModel::PathOf(const String& name) const
{
	return PathUnder(path, name);
}

bool ExplorerModel::GetDiskSpace(int64& total, int64& free_space) const
{
	if(path.IsEmpty()) return false;
	VfsPath vp;
	vp.Set(path);
	return mounts.GetDiskSpace(vp, total, free_space);
}

bool ExplorerModel::CanModify(String& reason) const
{
	if(!mount) {
		reason = "no mount is serving this directory";
		return false;
	}
	if(!mount->Caps().CanMutate()) {
		reason = "the mount at '" + mount->path.AsSysPath("/") + "' is read-only";
		return false;
	}
	return true;
}

bool ExplorerModel::CreateDir(const String& name, String& error)
{
	if(!IsValidName(name, error)) return false;
	String target = PathOf(name);
	if(!GetExplorerPolicy().Authorize(
			ExplorerPolicy::Request(ExplorerPolicy::CREATE_DIR, target,
			                        GetExplorerPolicy().Classify(target),
			                        "Create the directory '" + name + "' here"), error))
		return false;

	VfsPath vp;
	vp.Set(target);
	if(!mounts.CreateDir(vp)) {
		error = mounts.last_error.IsEmpty() ? "cannot create directory '" + target + "'"
		                                   : mounts.last_error;
		EWARN(ExplorerChannels::OPS, "mkdir " << target << " failed: " << error);
		return false;
	}
	EINFO(ExplorerChannels::OPS, "mkdir " << target);
	RereadQuiet();
	return true;
}

bool ExplorerModel::CreateFile(const String& name, String& error)
{
	if(!IsValidName(name, error)) return false;
	String target = PathOf(name);
	if(!GetExplorerPolicy().Authorize(
			ExplorerPolicy::Request(ExplorerPolicy::CREATE_FILE, target,
			                        GetExplorerPolicy().Classify(target),
			                        "Create the file '" + name + "' here"), error))
		return false;

	VfsPath vp;
	vp.Set(target);
	if(!mounts.CreateFile(vp)) {
		error = mounts.last_error.IsEmpty() ? "cannot create file '" + target + "'"
		                                   : mounts.last_error;
		EWARN(ExplorerChannels::OPS, "create " << target << " failed: " << error);
		return false;
	}
	EINFO(ExplorerChannels::OPS, "create " << target);
	RereadQuiet();
	return true;
}

bool ExplorerModel::CreateSymlink(const String& name, const String& target, String& error)
{
	if(!IsValidName(name, error)) return false;
	String link_target = PathOf(target);
	if(!GetExplorerPolicy().Authorize(
			ExplorerPolicy::Request(ExplorerPolicy::CREATE_SYMLINK, link_target,
					GetExplorerPolicy().Classify(link_target),
					"Create a symbolic link '" + name + "' here"), error))
		return false;

	VfsPath vp;
	vp.Set(link_target);
	VfsPath vt;
	vt.Set(target);
	if(!mounts.CreateSymlink(vp, vt)) {
		error = mounts.last_error.IsEmpty() ? "cannot create symlink '" + link_target + "'"
		                                   : mounts.last_error;
		EWARN(ExplorerChannels::OPS, "symlink " << link_target << " failed: " << error);
		return false;
	}
	EINFO(ExplorerChannels::OPS, "symlink " << link_target);
	RereadQuiet();
	return true;
}

bool ExplorerModel::EmptyTrashbin(String& error)
{
	String trash_path = GetTrashPath();
	if(trash_path.IsEmpty()) {
		error = "no trash directory configured";
		return false;
	}
	if(!GetExplorerPolicy().Authorize(
			ExplorerPolicy::Request(ExplorerPolicy::DELETE, trash_path,
					GetExplorerPolicy().Classify(trash_path),
					"Empty the trash bin"), error))
		return false;
	VfsPath vp_trash;
	vp_trash.Set(trash_path);
	if(!mounts.Remove(vp_trash, true)) {
		error = mounts.last_error.IsEmpty() ? "cannot empty trash bin" : mounts.last_error;
		EWARN(ExplorerChannels::OPS, "empty trashbin failed: " << error);
		return false;
	}
	EINFO(ExplorerChannels::OPS, "empty trashbin");
	RereadQuiet();
	return true;
}

bool ExplorerModel::Rename(int row, const String& new_name, String& error)
{
	if(row < 0 || row >= entries.GetCount()) {
		error = "no such row";
		return false;
	}
	if(!IsValidName(new_name, error)) return false;
	String from = PathOf(entries[row].name);
	String to   = PathOf(new_name);
	if(!GetExplorerPolicy().Authorize(
			ExplorerPolicy::Request(ExplorerPolicy::RENAME, from,
			                        GetExplorerPolicy().Classify(from),
			                        "Rename '" + entries[row].name + "' to '" +
			                        new_name + "'"), error))
		return false;

	VfsPath vf, vt;
	vf.Set(from);
	vt.Set(to);
	if(!mounts.Rename(vf, vt)) {
		error = mounts.last_error.IsEmpty() ? "cannot rename '" + from + "'" : mounts.last_error;
		EWARN(ExplorerChannels::OPS, "rename " << from << " -> " << to << " failed: " << error);
		return false;
	}
	EINFO(ExplorerChannels::OPS, "rename " << from << " -> " << to);
	RereadQuiet();
	return true;
}

bool ExplorerModel::Delete(const Vector<String>& names, bool recursive, String& error)
{
	if(names.IsEmpty()) {
		error = "nothing selected";
		return false;
	}
	for(const String& n : names)
		if(!IsValidName(n, error))
			return false;

	// Every selected name is authorized before the first one is removed. Removing
	// the first and then discovering the second is not allowed would leave a
	// half-applied delete, which is the one outcome a file manager must never
	// produce.
	for(const String& n : names) {
		String target = PathOf(n);
		if(!GetExplorerPolicy().Authorize(
				ExplorerPolicy::Request(ExplorerPolicy::DELETE, target,
				                        ExplorerPolicy::DESTRUCTIVE,
				                        "Delete '" + n + "'"
				                        + (recursive ? " and everything inside it" : "")), error))
			return false;
	}

	for(const String& n : names) {
		String target = PathOf(n);
		VfsPath vp;
		vp.Set(target);
		if(!mounts.Remove(vp, recursive)) {
			error = mounts.last_error.IsEmpty() ? "cannot remove '" + target + "'"
			                                   : mounts.last_error;
			EWARN(ExplorerChannels::OPS, "rm " << target << " failed: " << error);
			return false;
		}
		EINFO(ExplorerChannels::OPS, "rm " << target);
	}
	RereadQuiet();
	return true;
}

bool ExplorerModel::Copy(const Vector<String>& names, const String& dest_dir, String& error)
{
	return Transfer(names, dest_dir, false, error);
}

bool ExplorerModel::Move(const Vector<String>& names, const String& dest_dir, String& error)
{
	return Transfer(names, dest_dir, true, error);
}

bool ExplorerModel::Transfer(const Vector<String>& names, const String& dest_dir,
                             bool move, String& error)
{
	if(names.IsEmpty()) {
		error = "nothing selected";
		return false;
	}
	for(const String& n : names)
		if(!IsValidName(n, error))
			return false;

	for(const String& n : names) {
		String target = PathUnder(dest_dir, n);
		if(!GetExplorerPolicy().Authorize(
				ExplorerPolicy::Request(ExplorerPolicy::WRITE, target,
				                        GetExplorerPolicy().Classify(target),
				                        String(move ? "Move '" : "Copy '") + n +
				                        "' to '" + dest_dir + "'"), error))
			return false;
	}

	for(const String& n : names) {
		String from = PathOf(n);
		String to   = PathUnder(dest_dir, n);
		VfsPath vf, vt;
		vf.Set(from);
		vt.Set(to);
		bool ok = move ? mounts.Move(vf, vt) : mounts.Copy(vf, vt);
		if(!ok) {
			error = mounts.last_error.IsEmpty()
				? String(move ? "cannot move '" : "cannot copy '") + from + "'"
				: mounts.last_error;
			EWARN(ExplorerChannels::OPS, (move ? "mv " : "cp ") << from << " -> " << to
			      << " failed: " << error);
			return false;
		}
		EINFO(ExplorerChannels::OPS, (move ? "mv " : "cp ") << from << " -> " << to);
	}
	RereadQuiet();
	return true;
}

END_UPP_NAMESPACE
