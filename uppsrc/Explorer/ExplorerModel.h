#ifndef _Explorer_ExplorerModel_h
#define _Explorer_ExplorerModel_h

#ifdef DOUBLE_CLICK
#undef DOUBLE_CLICK
#endif

#include "ExplorerLog.h"
#include <Vfs/Core/VfsBase.h>

NAMESPACE_UPP

// Human-readable byte count, scaled the way a file manager writes it. One function
// for the model, the window and the twin: three copies of a formatter drift, and the
// free-space column of the sidebar has to agree with the status bar and with
// `--mounts`. A negative size is "unknown" rather than "0 B" -- a VFS that cannot
// report sizes says so instead of claiming an empty file.
String FormatSize(int64 bytes);

// One row of a directory listing.
//
// Deliberately free of any presentation concern: no Image, no Font, no colour.
// Icons and fonts belong to the GUI layer, which derives them from name and type.
// That is what lets the headless twin list the exact same rows.
struct ExplorerEntry : Moveable<ExplorerEntry> {
	String       name;
	VfsItemType  type = VFS_NULL;
	bool         is_dir = false;
	bool         hidden = false;
	int64        size = 0;
	Time         modified;
	String       ext;
	String       mount_type;  //< non-empty when this row is a mount point entry
	String       mime;        //< MIME type from VFS or derived from extension
	Value        data;

	//< What the mount volunteered about the entry, when it did. has_meta is the
	//< "the listing came from a VFS that knows" signal: without it the four
	//< fields below mean nothing and every column that shows them says unknown,
	//< which is a different fact from a mount that answered but had no mode word
	//< (read_only still does) or no uid. uid/gid default to -1 because root is 0.
	bool         has_meta = false;
	bool         read_only = false;
	dword        unix_mode = 0;
	int          uid = -1;
	int          gid = -1;
	String       link;

	ExplorerEntry() {}
	ExplorerEntry(const VfsItem& item);

	bool IsDir() const   { return is_dir; }
	bool IsMount() const { return !mount_type.IsEmpty(); }

	//< One "Property / Value" row as a file manager shows it.
	struct PropertyRow : Moveable<PropertyRow> {
		String label, value;
		PropertyRow(const char *l, const String& v) : label(l), value(v) {}
		PropertyRow() {}
	};
};

//< The rows a properties panel shows for an entry.
//
// This lives in the model, not in the dialog, and that is the whole point: the
// properties dialog and the --props output of the headless twin both read this one
// list, so the two cannot drift apart, and every value is verifiable without a GUI.
// An unknown stays unknown -- a VFS that cannot report a size says so rather than
// the host being asked for a number that does not apply to it.
Vector<ExplorerEntry::PropertyRow> EntryProperties(const ExplorerEntry& e,
                                                   const String& parent,
                                                   const String& mount,
                                                   bool can_modify);

// The columns the detailed listing can show, in the order they are shown in.
//
// One column per value, rather than one row with a name at one end and a
// description at the other: the values differ in kind -- a size, a time, a yes or
// no -- and lining them up is what makes a listing scannable. Which of them are
// shown is the user's choice, so the set lives in the model next to the sort order
// and the twin can print it.
enum ListColumn {
	LCOL_NAME,
	LCOL_TYPE,
	LCOL_SIZE,
	LCOL_MODIFIED,
	LCOL_USER,
	LCOL_GROUP,
	LCOL_PERMISSIONS,
	LCOL_LINK,
	LCOL_HIDDEN,
	LCOL_MOUNTED,
	LCOL_MAX
};

//< Column heading, in the window and in the twin. Translated at the call site.
const char *ListColumnTitle(ListColumn c);
//< The cell text for one entry. Empty for LCOL_MOUNTED on an ordinary row, and
//< "unknown" where the VFS does not expose the value: a blank cell and a
//< missing one are different facts and the listing has to be able to say both.
String      ListColumnValue(ListColumn c, const ExplorerEntry& e);
//< Default width in pixels, and whether the column is on out of the box.
int         ListColumnWidth(ListColumn c);
bool        ListColumnDefaultVisible(ListColumn c);
//< The sort key a click on this column's heading sorts by, or -1 when the
//< column is not a sort key (hidden is a flag, not an order).
int         ListColumnSortKey(ListColumn c);

// How the listing is laid out. The three are the ones a file manager offers, and
// they differ in more than size: the icon modes draw a picture and a wrapped name
// per cell, the detailed mode draws columns.
enum ViewMode {
	VIEW_LIST,        //< name, size, time, type
	VIEW_BIG_ICONS,   //< one large picture and a wrapped name per cell
	VIEW_SMALL_ICONS  //< one small picture and a name per cell
};

//< Name of a view mode, as the menu and the twin both write it.
const char *ViewModeName(ViewMode mode);

// What kind of thing an entry is, for the sake of its icon. Classifying by name and
// type is a question about the listing, not about drawing, so it lives here: the
// twin prints the kind next to every row, which is the only way to check that
// `.tar.gz` gets the archive icon without looking at a picture.
enum IconKind {
	ICON_FOLDER,
	ICON_MOUNT,
	ICON_TEXT,
	ICON_IMAGE,
	ICON_AUDIO,
	ICON_VIDEO,
	ICON_ARCHIVE,
	ICON_CODE,
	ICON_EXECUTABLE,
	ICON_FILE
};

IconKind      EntryIconKind(const ExplorerEntry& e);
const char   *IconKindName(IconKind kind);
//< Size of the picture and of the whole cell, per view mode, at 100% zoom.
Size          ViewCellSize(ViewMode mode);
//< Icon sizes come in steps, not in percentages: a zoom level that lands the icon
//< on 27 pixels produces a picture that is neither one of the drawn sizes nor a
//< scaled copy of one. The levels below are the icon edge lengths, in pixels.
enum { VIEW_ZOOM_SMALLEST = 20, VIEW_ZOOM_LARGEST = 128, VIEW_ZOOM_DEFAULT = 64 };
//< Clamps `pixels` into the levels above. Out-of-range zoom is a value from a
//< scroll bar or a mistyped --zoom, not something to reject outright.
int           ViewZoomClamp(int pixels);
//< A cell of `mode` with its picture at `zoom` pixels. The cell is the picture
//< plus room for the name, so it grows with the picture.
Size          ViewCellSizeAt(ViewMode mode, int zoom);
//< The picture size for `mode` at `zoom`; what ExplorerIcons is asked for.
int           ViewIconSizeAt(ViewMode mode, int zoom);
//< Cells per row for a panel of `panel_width` pixels. At least one: a window
//< narrower than a cell must still show one cell rather than divide by zero.
int           ViewColumns(ViewMode mode, int panel_width, int zoom = VIEW_ZOOM_DEFAULT);
//< Which cell an item index lands in, for a grid `columns` wide. Kept here so the
//< window and the twin agree on the arrangement instead of only the window knowing.
inline int ViewRow(int index, int columns)    { return columns > 0 ? index / columns : 0; }
inline int ViewColumn(int index, int columns) { return columns > 0 ? index % columns : 0; }

// Directory browsing state: where we are, what is listed, how it is sorted and
// filtered, and how we got here.
//
// Everything reads through MountManager, so the shown filesystem can be replaced
// wholesale by mounting something else over it. There is no separate "real path"
// path through this class.
struct ExplorerModel : Moveable<ExplorerModel> {
	enum SortBy { NAME, EXTENSION, SIZE, TIME, TYPE };
	//< Heading for a sort key, for the column header's arrow and for the twin.
	static const char *SortKeyName(SortBy by) {
		switch(by) {
		case NAME:      return "name";
		case EXTENSION: return "ext";
		case SIZE:      return "size";
		case TIME:      return "time";
		case TYPE:      return "type";
		}
		return "?";
	}
	enum SortDir { ASCENDING, DESCENDING };

	ExplorerModel();

	// --- navigation ---------------------------------------------------------

	//< Reads the directory through the mount that owns it. Returns false and fills
	//< error when the path cannot be listed.
	bool SetPath(const String& path, String& error);
	bool Refresh(String& error)                 { return SetPath(path, error); }

	const String& GetPath() const               { return path; }
	//< Path relative to the owning mount, which is what the VFS wants.
	String        GetMountRelativePath() const  { return rel_path; }
	//< The mount currently serving this path.
	MountPoint*   GetMount() const              { return mount; }
	//< The deepest mount shadowing the one in use, if any.
	MountPoint*   GetShadowingMount() const     { return shadowing; }

	bool Up(String& error);
	bool Back(String& error);
	bool Forward(String& error);
	bool CanBack() const    { return history_pos > 0; }
	bool CanForward() const { return history_pos >= 0 && history_pos < history.GetCount() - 1; }
	bool CanUp() const;

	const Vector<String>& GetHistory() const   { return history; }

	// --- listing ------------------------------------------------------------

	Vector<ExplorerEntry>& Entries()            { return entries; }
	const Vector<ExplorerEntry>& Entries() const { return entries; }
	int  GetCount() const                       { return entries.GetCount(); }

	void   SetSort(SortBy by, SortDir dir);
	SortBy GetSortBy() const                    { return sort_by; }
	SortDir GetSortDir() const                  { return sort_dir; }
	void   ToggleSortDir()                      { SetSort(sort_by, sort_dir == ASCENDING ? DESCENDING : ASCENDING); }

	//< Directories before files, whatever the key is. On out of the box, because a
	//< listing that interleaves them is harder to read, and reachable from the menu
	//< because "all my folders together" is not true of every directory.
	bool   GetDirsFirst() const                 { return dirs_first; }
	void   SetDirsFirst(bool on)                { if(on != dirs_first) { dirs_first = on; RefreshEntries(); } }
	//< Compare names, extensions and mount types as if they were all lower case.
	//< Off out of the box, which is what the listing has always done. It only
	//< applies to the text keys: a size and a time have no case to ignore.
	bool   GetIgnoreCase() const                { return ignore_case; }
	void   SetIgnoreCase(bool on)               { if(on != ignore_case) { ignore_case = on; RefreshEntries(); } }

	// --- activation policy ---------------------------------------------------
	//
	// Single-click or double-click to open. Single-click selects and opens;
	// double-click requires two clicks to open (first selects, second opens).
	// This is a global preference shared by all tabs/panes, and the headless
	// twin respects it for its diagnostic activation tests.
#ifdef DOUBLE_CLICK
#undef DOUBLE_CLICK
#endif
	enum ActivatePolicy { SINGLE_CLICK, DOUBLE_CLICK };
	void   SetActivatePolicy(ActivatePolicy p)  { activate_policy = p; }
	ActivatePolicy GetActivatePolicy() const    { return activate_policy; }

	//< The whole sort state as one sentence, for the window's dump and the twin's
	//< listing header. Four settings and three callers: two callers describing the
	//< same order in their own words is the drift worth not having.
	String DescribeSort() const;

	//< Shell-style filter, supporting * and ?. An empty filter matches everything.
	void   SetFilter(const String& pattern);
	const String& GetFilter() const             { return filter; }
	void   SetShowHidden(bool b)                { show_hidden = b; }
	bool   GetShowHidden() const                { return show_hidden; }
	bool   MatchesFilter(const ExplorerEntry& e) const;

	//< Applies sort and filter. Called by SetPath, and by the setters above.
	void   RefreshEntries();

	// --- view mode -----------------------------------------------------------

	//< How the panel draws these entries. Per tab, like the sort order: one listing
	//< being read as icons says nothing about how another should be read.
	void   SetViewMode(ViewMode mode)           { view_mode = mode; }
	ViewMode GetViewMode() const                { return view_mode; }
	//< Icon edge length for the current mode. Per tab like the mode itself, and
	//< only meaningful for the icon modes: the detailed list has no picture to
	//< scale, so zooming it is a no-op rather than a change nobody can see.
	int    GetViewZoom() const                  { return zoom; }
	void   SetViewZoom(int pixels)              { zoom = ViewZoomClamp(pixels); }
	//< The cell this mode would use in a panel of `panel_width` pixels.
	int    IconColumns(int panel_width) const
		{ return ViewColumns(view_mode, panel_width, zoom); }
	Size   CellSize() const                     { return ViewCellSizeAt(view_mode, zoom); }

	// --- columns -------------------------------------------------------------

	//< Which columns the detailed listing draws. Per tab, like the mode: one tab
	//< read as "name and size only" says nothing about what the others should show.
	bool   IsColumnVisible(ListColumn c) const  { return column_visible[c]; }
	void   SetColumnVisible(ListColumn c, bool on);
	//< The default set again, for the "Reset columns" that every configuration of
	//< this kind needs somewhere to go.
	void   ResetColumns();
	//< The visible columns, in display order. Never empty: hiding the name column
	//< would leave rows of unlabelled values with no way back except the dialog.
	Vector<ListColumn> VisibleColumns() const;

	// --- mounts --------------------------------------------------------------

	//< Mounts that behave like a browsable filesystem, deepest first.
	Vector<MountPoint*> GetBrowsableMounts();
	//< Every mount, whatever its role.
	Vector<MountPoint*> GetAllMounts()           { return mounts.Enumerate(); }

	String DescribeMounts() const;

	// --- places ---------------------------------------------------------------

	//< One row of the sidebar's Places and Mount points lists.
	//
	// The kind is what a caller branches on rather than the label: "filesystem",
	// "home", "xdg", "trash" or "mount". A file manager shows these, and which icon
	// a row gets is a presentation decision made from this, not from the text.
	struct Place : Moveable<Place> {
		String label, path, kind;
		Place() {}
		Place(const char *l, const String& p, const char *k) : label(l), path(p), kind(k) {}
	};

	//< The places a file manager puts in its sidebar: the mount root, the user's
	//< home, the XDG directories that actually exist, the trash, and every browsable
	//< mount as a drive.
	//<
	//< Existence goes through the mount registry rather than the host, so a place
	//< the shown filesystem does not serve is not offered. Every path is returned
	//< as the sidebar should open it, absolute, which is what makes this testable
	//< from the headless twin without a window.
	Vector<Place> GetPlaces(bool include_mounts = true);

	//< The user's trash directory, or empty when there is none. xfe shows it as a
	//< place; a file manager that hides where deletions end up is lying about
	//< what it did.
	String GetTrashPath();

	//< The XDG user directories declared in ~/.config/user-dirs.dirs, in the order
	//< the specification lists them. Only those that exist are returned; the file
	//< names entries that do not exist are common and offering them would produce a
	//< sidebar row that fails when double-clicked.
	Vector<Place> GetXdgPlaces();

	// --- operations -----------------------------------------------------------
	//
	// Every mutation goes through GetExplorerPolicy() *before* the filesystem is
	// touched, so the headless twin denies exactly what the GUI would have had to
	// confirm. Keeping the check here rather than in the window is what stops a
	// second caller from reaching MountManager's mutators without passing policy.
	//
	// All of them take and fill `error`, never throw, and re-read the listing on
	// success so the panel and the headless printout stay in step.
	bool CreateDir(const String& name, String& error);
	bool CreateFile(const String& name, String& error);
	bool Rename(int row, const String& new_name, String& error);
	//< Creates a symbolic link from `name` to `target`.
	bool CreateSymlink(const String& name, const String& target, String& error);
	//< `recursive` must be passed explicitly. A plain Remove of a populated
	//< directory fails at the Vfs layer, so a stray true cannot silently eat a
	//< subtree -- but the GUI still has to ask the user, and it has to mean it.
	bool Delete(const Vector<String>& names, bool recursive, String& error);
	bool Copy(const Vector<String>& names, const String& dest_dir, String& error);
	bool Move(const Vector<String>& names, const String& dest_dir, String& error);
	bool EmptyTrashbin(String& error);

	//< Absolute path of `name` inside the directory currently shown. Empty when the
	//< name is not a plain child name.
	String PathOf(const String& name) const;
	//< Absolute path of `name` inside `dir`.
	static String PathUnder(const String& dir, const String& name);

	//< Rejects empty names, "." and "..", and anything carrying a separator. These
	//< are the names that would let a caller escape the directory it was given.
	static bool IsValidName(const String& name, String& error);

	//< Free and total bytes of the device holding the current directory. False when
	//< the mount cannot answer, which callers must render as "unknown" and never as
	//< 0: a full disk and a filesystem that has no idea are different problems.
	bool GetDiskSpace(int64& total, int64& free_space) const;

	//< True when the current mount can be written to at all, so the GUI can disable
	//< its editing commands instead of offering something that must fail.
	bool CanModify(String& reason) const;

	MountManager& GetMounts()                    { return mounts; }

	//< Human readable summary of the current listing, shared by the GUI status bar
	//< and the headless twin so both say the same thing.
	String StatusText() const;
	//< The same summary as the pieces the status strip is built from: what is
	//< listed, and how much room is left. The window needs them apart because a
	//< 98 status bar is two *panelled* cells, not one sentence; the twin needs
	//< them joined, and gets them from here rather than from its own formatting.
	//<
	//< StatusText() is the join, so the two cannot drift: a cell the window draws
	//< and a line the twin prints are the same bytes by construction.
	Vector<String> StatusCells() const;

	static bool IsHiddenName(const String& name);
	static const char *TypeName(VfsItemType t);

private:
	//< Re-reads the current directory after an operation changed it, logging rather
	//< than reporting failure. RefreshEntries() only re-applies sort and filter to
	//< the entries already in memory, which is not enough once the disk has
	//< changed: that is what made a freshly created entry invisible to the very next
	//< command in the same run. The operation itself already succeeded, so a listing
	//< that cannot be refreshed is a different problem and must not fail it.
	void RereadQuiet();

	//< Shared body of Copy and Move. Everything else about the two is identical,
	//< and the copy-versus-move distinction belongs to the Vfs layer.
	bool Transfer(const Vector<String>& names, const String& dest_dir, bool move, String& error);

	MountManager& mounts;
	String        path;
	String        rel_path;
	MountPoint*   mount = nullptr;
	MountPoint*   shadowing = nullptr;
	Vector<ExplorerEntry> entries;

	Vector<String> history;
	int            history_pos = -1;

	SortBy   sort_by = NAME;
	SortDir  sort_dir = ASCENDING;
	ViewMode view_mode = VIEW_LIST;
	int    zoom = VIEW_ZOOM_DEFAULT;
	bool   column_visible[LCOL_MAX] = { false };
	String   filter;
	bool     show_hidden = false;
	bool     dirs_first = true;
	bool     ignore_case = false;
	ActivatePolicy activate_policy = DOUBLE_CLICK;
};

END_UPP_NAMESPACE

#endif
