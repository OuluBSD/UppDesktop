#include "Explorer.h"
#include "ExplorerGate.h"
#include "ExplorerDemoVfs.h"

#ifdef flagGUI

#include "ExplorerLang.h"
#include "ExplorerLogWindow.h"
#include "ExplorerSkin.h"
#include "ExplorerBookmarks.h"

using namespace Upp;

//< Sidebar width in pixels.
static const int NAV_WIDTH = 280;

// Splitter positions live in a fixed 10000-unit space across the whole splitter,
// so a pixel width has to be scaled before it can be handed to SetPos.
static int SplitUnits(int pixels, int total)
{
	return total > 0 ? pixels * 10000 / total : 0;
}

static Image DirIcon()  { return CtrlImg::Dir(); }

// Left-aligned in a fixed field, for the --dump-ui listing so its columns can be
// read down rather than across.
static String Pad(const String& s, int width)
{
	String out = s;
	while(out.GetLength() < width) out << ' ';
	return out;
}
static Image FileIcon() { return CtrlImg::File(); }

// ---------------------------------------------------------------------------
// ExplorerSettings
// ---------------------------------------------------------------------------

void ExplorerSettings::Jsonize(JsonIO& jio)
{
	int ap = (int)activate_policy;
	jio
		("activate_policy", ap)
		("default_icon_zoom", default_icon_zoom)
		("new_tab_same_dir", new_tab_same_dir)
		("close_tab_confirm", close_tab_confirm)
		("show_hidden_by_default", show_hidden_by_default)
		("default_view_mode", default_view_mode)
		("default_dirs_first", default_dirs_first)
		("default_ignore_case", default_ignore_case)
		("compact_toolbar", compact_toolbar)
		("show_nav_bar", show_nav_bar)
		("show_tool_bar", show_tool_bar)
		("show_view_bar", show_view_bar)
		("show_location_bar", show_location_bar)
		("split_horizontal", split_horizontal)
		("sync_panes", sync_panes);
	if(jio.IsLoading())
		activate_policy = (ActivatePolicy)ap;
}

String ExplorerSettings::ConfigPath()
{
	return ConfigFile("Explorer/settings.json");
}

ExplorerSettings ExplorerSettings::Load()
{
	ExplorerSettings s;
	String path = ConfigPath();
	if(FileExists(path)) {
		try {
			Value v = ParseJSON(LoadFile(path));
			if(!IsNull(v)) {
				JsonIO jio(v);
				s.Jsonize(jio);
			}
		}
		catch(...) {
			// Invalid config, use defaults
		}
	}
	return s;
}

void ExplorerSettings::Save() const
{
	String path = ConfigPath();
	RealizeDirectory(GetFileDirectory(path));
	SaveFile(path, AsJSON(StoreAsJsonValue(*this)));
}

// ---------------------------------------------------------------------------
// AddressBar
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ExplorerNav
// ---------------------------------------------------------------------------

// Free space of a drive, as the Mount points list shows it. "unknown" rather than 0
// for a filesystem that cannot report a device: the same reasoning as the status
// bar, because "0 B free" reads as a full disk. FormatSize is the model's, so this
// column cannot drift from the status bar.
static String DriveSpace(ExplorerModel& model, const String& path)
{
	VfsPath vp;
	vp.Set(path);
	int64 total, free_space;
	if(!model.GetMounts().GetDiskSpace(vp, total, free_space))
		return t_("unknown");
	return FormatSize(free_space) + " / " + FormatSize(total);
}

PlacesPanel::PlacesPanel(const char *name, Content initial)
{
	caption.SetLabel(name);

	places.LayoutId("places");
	drives.LayoutId("mounts");
	marks.LayoutId("bookmarks");
	places.AddColumn("place", 18);
	places.AddColumn("path", 0);
	places.NoHeader();
	places.MultiSelect(false);

	drives.AddColumn("mount", 20);
	drives.AddColumn("path", 0);
	drives.AddColumn("free", 16);
	drives.NoHeader();
	drives.MultiSelect(false);

	marks.AddColumn("bookmark", 18);
	marks.AddColumn("path", 0);
	marks.NoHeader();
	marks.MultiSelect(false);

	WireOpen();

	int ccy = GetStdFont().GetHeight() + 8;
	Add(caption.HSizePos(4, 4).TopPos(2, ccy));
	Add(places.HSizePos(0, 0).VSizePos(ccy + 2, 0));
	Add(drives.HSizePos(0, 0).VSizePos(ccy + 2, 0));
	Add(marks.HSizePos(0, 0).VSizePos(ccy + 2, 0));
	ShowContent(initial);
}

ArrayCtrl& PlacesPanel::Active()
{
	switch(content) {
	case MOUNTS:    return drives;
	case BOOKMARKS: return marks;
	default:        return places;
	}
}

void PlacesPanel::ShowContent(Content c)
{
	content = c;
	places.Show(c == PLACES);
	drives.Show(c == MOUNTS);
	marks.Show(c == BOOKMARKS);
	// The caption says which list this is. Without it, a panel whose content is
	// chosen from a menu is a list of unknown things: the names are labels, not
	// headings, and "Documents" does not say it is one of a set.
	caption.SetLabel(c == PLACES    ? t_("Places")
	               : c == MOUNTS   ? t_("Mounts")
	                             : t_("Bookmarks"));
	RefreshLayout();
}

void PlacesPanel::WireOpen()
{
	// Both routes to "open this": the double click a mouse user makes and the Enter
	// a keyboard user makes. Wiring only the double click is how a list ends up
	// looking broken to a user who clicks and to one who presses Enter. Shared by
	// all three lists so a list added later cannot come without the behaviour.
	ArrayCtrl *lists[3] = { &places, &drives, &marks };
	for(ArrayCtrl *list : lists) {
		ArrayCtrl *l = list;
		auto open = [this, l] {
			int i = l->GetCursor();
			if(i >= 0 && i < l->GetCount())
				WhenPlaceSelected(l->Get(i, 1));
		};
		l->WhenLeftDouble << open;
		l->WhenEnterKey << open;
		// Deliberately not WhenSel: that fires on a single click, and a sidebar
		// that navigates when you point at a place cannot be aimed.
	}
}

void PlacesPanel::PopulateBookmarks()
{
	String keep = SelectedBookmark();
	marks.Clear();
	for(const String& b : GetExplorerBookmarks().Entries())
		// Show the last component, keep the full path in the second column: the
		// sidebar is narrow and a full path in column 0 is unreadable, but the full
		// path is what has to be opened.
		marks.Add(GetFileName(b), b);
	// Keep the selection across a refresh: re-reading the list after a remove would
	// otherwise drop the cursor and make a second remove act on whatever slid into
	// that row.
	if(!keep.IsEmpty())
		for(int i = 0; i < marks.GetCount(); i++)
			if(marks.Get(i, 1) == keep) {
				marks.SetCursor(i);
				break;
			}
}

String PlacesPanel::SelectedBookmark() const
{
	int i = marks.GetCursor();
	if(i < 0 || i >= marks.GetCount()) return String();
	return marks.Get(i, 1);
}

void PlacesPanel::Populate()
{
	places.Clear();
	drives.Clear();
	ExplorerModel model;
	for(const ExplorerModel::Place& p : model.GetPlaces()) {
		// Mount points get their own list. A file manager shows drives where drives
		// belong, and burying them under a separator in Places is what made them
		// read as debug output rather than somewhere to go.
		ArrayCtrl& list = p.kind == "mount" ? drives : places;
		// Show the last component, keep the full path in the second column: the
		// sidebar is narrow and a full path in column 0 is unreadable, but the full
		// path is what has to be opened.
		String label = p.label;
		if(p.kind == "mount") {
			MountPoint* mp = model.GetMounts().Find(p.path);
			if(mp && !mp->type.IsEmpty() && label.Find('[') < 0)
				label << "  [" << mp->type << "]";
			list.Add(label, p.path, DriveSpace(model, p.path));
		}
		else
			list.Add(label, p.path);
	}
}

void PlacesPanel::SelectPlace(const String& path)
{
	// The current location is selected in whichever list holds it. Both lists are
	// updated rather than stopping at the first hit, so switching to Mounts after
	// navigating into an overlay shows where the user actually is.
	if(IsNull(path)) return;
	ArrayCtrl *lists[2] = { &places, &drives };
	for(ArrayCtrl *list : lists)
		for(int i = 0; i < list->GetCount(); i++)
			if(list->Get(i, 1) == path) {
				list->SetCursor(i);
				break;
			}
}

const char *ExplorerNav::ModeName(Mode m)
{
	// The vocabulary split (phase-02 task 14): "panel" means a main-area
	// listing pane, never the sidebar. The sidebar modes are named by what
	// they arrange -- columns of the sidebar, with or without the tree -- so
	// "two panels" can only ever mean the main-area split, which is what the
	// user's review found missing. The keys stay ASCII because the dump prints
	// them; t_() translates the whole key. TREE_ONLY is the Win98 folders
	// panel, the factory default since the sidebar restructure.
	switch(m) {
	case TREE_ONLY: return "sidebar-tree-only";
	case ONE:       return "sidebar-one-column";
	case TREE_ONE:  return "sidebar-tree-and-one-column";
	case TWO:       return "sidebar-two-columns";
	case TREE_TWO:  return "sidebar-tree-and-two-columns";
	}
	return "?";
}

LocationBar::LocationBar()
{
	// The path ctrl takes what the filter leaves: a fixed-width filter at the
	// right, the path in everything else. Layout metadata, not rects -- the
	// AGENTS' location-bar lesson, one row earlier in the window now.
	Add(path_ctrl.HSizePos(3, FILTER_WIDTH + 6).VSizePos(2, 2));
	// The filter right-anchored with RightPos(offset, size): HSizePos margins
	// would anchor it by its LEFT edge, which put it on top of the path ctrl
	// (the dump showed the filter at x=246 inside a path ctrl ending at 754).
	Add(filter.RightPos(3, FILTER_WIDTH).VSizePos(2, 2));
}

bool LocationBar::Access(Visitor& v)
{
	// The label first, then the ctrl, then the verb on the line: a planner
	// reading "value: /tmp" under "label: Location" needs the Navigate action
	// on the line to know the typed path is a way to move, not a label.
	v.AccessLabel(t_("Location"));
	path_ctrl.Access(v);
	v.AccessAction(t_("Navigate to the typed path"), [this] { if(WhenGo) WhenGo(); });
	filter.Access(v);
	return true;
}

ExplorerNav::ExplorerNav()
	: left("Places", PlacesPanel::PLACES)
	, right("Bookmarks", PlacesPanel::BOOKMARKS)
{
	// The tree. Built one level at a time, on expansion: a tree filled from "/"
	// all at once is tens of thousands of rows and a long wait for a list of
	// directories nobody asked for. A single click does not navigate -- see the
	// WhenSel note below.
	tree.WhenSel << [this] {
		String p = SelectedTreePath();
		if(!p.IsEmpty())
			WhenPlaceSelected(p);
	};
	// A single click must not navigate, or the tree cannot be walked: every row
	// on the way down would be opened. TreeCtrl::WhenSel fires for a click *and*
	// for the keyboard, so it is the wrong event for this.
	tree.WhenLeftDouble << [this] {
		String p = SelectedTreePath();
		if(!p.IsEmpty())
			WhenPlaceSelected(p);
	};
	// Filling on expansion, not on click: the open arrow is the only gesture that
	// says "I want to see what is in here".
	tree.WhenOpen << [this](int id) {
		String p = tree.Get(id).ToString();
		if(!p.IsEmpty() && tree.GetChildCount(id) == 0)
			FillTreeNode(id, p);
	};

	// The one vertical splitter owns all three sidebar panes, top to bottom:
	// the tree that follows the listing, then places, then bookmarks. The
	// horizontal space belongs to the sidebar/listing split alone.
	psplit.Vert();
	psplit.Add(tree);
	psplit.Add(left);
	psplit.Add(right);
	tree.SizePos();
	left.SizePos();
	right.SizePos();
	Add(psplit.SizePos());

	// Each panel's own selection is this sidebar's selection: two panels that
	// navigate independently would need a rule about which one a command means.
	PlacesPanel *panels[2] = { &left, &right };
	for(PlacesPanel *p : panels)
		p->WhenPlaceSelected << [this](String path) { WhenPlaceSelected(path); };

	SetMode(mode);
	PopulateBookmarks();
}

void ExplorerNav::SetMode(Mode m)
{
	// The dividers are remembered per mode, in the splitter's own 10000-unit
	// space: leaving a mode saves the dividers as the user dragged them, and
	// coming back restores exactly that shape instead of snapping to factory
	// proportions. A hidden pane's divider is clamped (tree hidden = the first
	// divider at 0, bookmarks hidden = the second at 10000), because the
	// splitter lays out all of its children every time and the band a hidden
	// pane gets must be nothing.
	if(psplit.GetPos(1) < 10000 && GetSize().cy > 0) {
		remembered_pos[mode][0] = psplit.GetPos(0);
		remembered_pos[mode][1] = psplit.GetPos(1);
		remembered_set[mode] = true;
	}
	mode = m;
	if(!remembered_set[mode]) {
		// First visit to this mode: the tree takes two fifths, and the
		// bookmarks panel takes a third of what is left.
		remembered_pos[mode][0] = 4000;
		remembered_pos[mode][1] = 7500;
		remembered_set[mode] = true;
	}
	int p0 = remembered_pos[mode][0], p1 = remembered_pos[mode][1];
	if(!ModeTree(m))  p0 = 0;
	if(!ModeRight(m)) p1 = 10000;
	// Higher index first: SetPos runs a layout, and the first SetPos on a
	// splitter whose pos array is still empty makes it allocate defaults and
	// lay out with them -- the second SetPos then clamps against those, and
	// the mode's own dividers never survive the round trip. The higher index
	// first extends the array without disturbing the lower one.
	psplit.SetPos(p1, 1);
	psplit.SetPos(p0, 0);
	RefreshLayout();
	// The splitter's own layout shows every child it lays out; Layout(),
	// below, re-asserts the mode's visible set after it.
}

void ExplorerNav::Layout()
{
	ParentCtrl::Layout();
	// See SetMode: the splitter shows what it lays out, so the mode's hidden
	// panes are hidden again here, after it. Idempotent -- this runs on every
	// window relayout -- and the hidden pane's band is already zero, so
	// nothing else has to move.
	tree.Show(ModeTree(mode));
	left.Show(mode != TREE_ONLY);
	right.Show(ModeRight(mode));
}

String ExplorerNav::SelectedTreePath() const
{
	int id = tree.GetCursor();
	if(id < 0 || !tree.IsValid(id)) return String();
	return tree.Get(id).ToString();
}

void ExplorerNav::FillTreeNode(int id, const String& path)
{
	// Only directories, and only one level: the tree is a way of walking down to
	// a directory, and a file in the tree is one more row that opens nothing.
	ExplorerModel model;
	String error;
	if(!model.SetPath(path, error))
		return;
	Vector<String> dirs;
	for(const ExplorerEntry& e : model.Entries())
		if(e.IsDir())
			dirs.Add(e.name);
	Sort(dirs);
	for(const String& d : dirs) {
		String child = path.EndsWith("/") ? path + d : path + '/' + d;
		// The folder icon per row, from the same drawn set as the listing, so a
		// directory looks the same in the tree as it does in the panel.
		int at = tree.Add(id, ExplorerIcons::Get(ICON_FOLDER, 16), child, d, true);
		tree.GetNode(at).SetImage(ExplorerIcons::Get(ICON_FOLDER, 16));
	}
	// Nothing there is a fact worth saying with a row: an empty directory still
	// has no children, and the open arrow then does nothing, which is correct.
	tree.Refresh();
}

void ExplorerNav::SyncTree(const String& path)
{
	if(!tree.IsVisible()) return;
	if(IsNull(path)) return;
	// The mount root, then each component down to the current directory. Built
	// from the path rather than by walking up: the mount registry knows where a
	// mount begins, and the host filesystem does not.
	ExplorerModel model;
	String error;
	if(!model.SetPath(path, error))
		return;
	MountPoint *mp = model.GetMount();
	String root = mp ? mp->path.AsSysPath("/") : "/";
	if(root.IsEmpty()) root = "/";

	tree.SetRoot(ExplorerIcons::Get(ICON_FOLDER, 16), root, root);
	FillTreeNode(0, root);

	// Open every step of the way down. The tree is a picture of where the user
	// is, and a tree that has the right rows but not the right ones open is a
	// different picture.
	String walk = root;
	// Already on a mount root: nothing to walk.
	if(walk != "/" && path.StartsWith(walk)) walk = "/";
	Vector<String> parts;
	String rest = walk == "/" ? path : path.Mid(walk.GetLength());
	if(walk != "/") parts.Add(walk);
	int cur = 0;
	for(String part : Split(rest, '/')) {
		if(part.IsEmpty()) continue;
		parts.Add(part);
		walk = walk.EndsWith("/") ? walk + part : walk + '/' + part;
		// Find the row for this path among the root's children, then descend.
		int next = -1;
		int start = 0;
		while(start < tree.GetChildCount(cur)) {
			int child = tree.GetChild(cur, start);
			if(tree.Get(child).ToString() == walk) {
				next = child;
				break;
			}
			start++;
		}
		if(next < 0) break;
		if(tree.GetChildCount(next) == 0)
			FillTreeNode(next, walk);
		cur = next;
		tree.Open(cur, true);
	}
	if(tree.IsValid(cur)) {
		tree.SetCursor(cur);
		tree.MakeVisible(cur);
	}
}

void ExplorerNav::Populate()
{
	left.Populate();
	right.Populate();
}

void ExplorerNav::PopulateBookmarks()
{
	left.PopulateBookmarks();
	right.PopulateBookmarks();
}

void ExplorerNav::SelectPlace(const String& path)
{
	left.SelectPlace(path);
	right.SelectPlace(path);
}

String ExplorerNav::SelectedBookmark() const
{
	// Whichever panel is showing bookmarks, and the left one when both do: the
	// command that asks is "remove the selected bookmark", and there is one
	// answer to give.
	if(left.GetContent() == PlacesPanel::BOOKMARKS)
		return left.SelectedBookmark();
	return right.SelectedBookmark();
}

// Free space of a drive, as the Mount points list shows it. "unknown" rather than 0
// for a filesystem that cannot report a device: the same reasoning as the status
// bar, because "0 B free" reads as a full disk. FormatSize is the model's, so this
// column cannot drift from the status bar.

// ---------------------------------------------------------------------------
// ExplorerPanel
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The detailed listing
// ---------------------------------------------------------------------------

Size ExplorerRow::GetStdSize(const Value& q) const
{
	// Height only. In the list mode the row is as wide as the list is, and the
	// columns inside it are placed by `lefts` -- a width here would be a second,
	// contradictory answer about how wide a row is.
	return Size(0, GetStdFont().GetHeight() + 4);
}

void ExplorerRow::Paint(Draw& w, const Rect& r, const Value& q, Color ink,
                        Color paper, dword style) const
{
	// The background is this display's job: ColumnList clips and hands the row
	// over, it does not fill it.
	w.DrawRect(r, paper);
	if(!lefts || lefts->GetCount() < 2) return;

	Font f = StdFont();
	int line_h = r.Height();
	for(int i = 0; i < values.GetCount(); i++) {
		int x0 = (*lefts)[i], x1 = (*lefts)[i + 1];
		if(x0 >= r.right) break;
		int avail = min(x1, r.right) - x0 - 4;
		if(avail <= 4) continue;
		Color c = (style & SELECT) ? SColorHighlightText() : ink;
		w.DrawText(x0 + 2, r.top + (line_h - f.GetHeight()) / 2,
		           FitText(values[i], f, avail), f, c);
	}
}

void ExplorerColumnHeader::SetColumns(const Vector<int>& l, int sort_col, bool asc)
{
	// Vector has no copy assignment in this fork, and Add() takes elements rather
	// than another vector, so this is a loop.
	lefts.Clear();
	for(int i = 0; i < l.GetCount(); i++) lefts.Add(l[i]);
	sort_column = sort_col;
	ascending = asc;
	Refresh();
}

int ExplorerColumnHeader::HeaderHeight() const
{
	// From the font, not a constant: a fixed strip is one line tall in the
	// default font and a clipped one under a larger system font.
	return GetStdFont().GetHeight() + 10;
}

int ExplorerColumnHeader::ColumnAt(Point p) const
{
	for(int i = 0; i + 1 < lefts.GetCount(); i++)
		if(p.x >= lefts[i] && p.x < lefts[i + 1])
			return i;
	return -1;
}

void ExplorerColumnHeader::Paint(Draw& w)
{
	Rect r = GetRect();
	// The face, then a line under the strip: without it the headings float over
	// the first row and it is not obvious where the strip ends.
	w.DrawRect(r, SColorFace);
	w.DrawRect(0, r.bottom - 1, r.right, 1, SColorLight);

	Font f = StdFont().Bold();
	// Hot while the pointer is over the heading, or for the whole press: letting
	// go off the strip must not take the highlight away from what was pressed.
	int hot = (inside || pressed) ? hover : -1;
	for(int i = 0; i + 1 < lefts.GetCount(); i++) {
		// This fork's Rect_(l, t, r, b) takes edges, not sizes: passing the
		// width as the right edge gave every column past the first a right
		// edge left of its own left, and the paint skipped them all -- which
		// is why only "Name" ever had a title.
		Rect c(lefts[i], 0, lefts[i + 1], r.bottom - 1);
		if(c.right > r.right) c.right = r.right;
		if(c.right <= c.left) continue;
		if(i == hot)
			w.DrawRect(c, SColorHighlight);
		// The sort arrow is inside the heading's own box, on the side the
		// values are drawn on: the rows are left-aligned, and a heading with an
		// arrow on the far side of a wide column is read against the wrong edge.
		int arrow = 0;
		Color ink = i == hot ? SColorHighlightText() : SColorText();
		if(i == sort_column) {
			arrow = 10;
			ink = i == hot ? SColorHighlightText() : SColorHighlight;
		}
		w.DrawText(c.left + 4, (c.Height() - f.GetHeight()) / 2,
		           FitText(ListColumnTitle((ListColumn)i), f,
		                   c.Width() - 8 - arrow), f, ink);
		if(i == sort_column) {
			int ax = min(c.right - 12, c.left + 4 + c.Width() - 8 - arrow);
			int ay = c.top + (c.Height() - 7) / 2;
			// Up for ascending. The list is sorted by the model and re-rendered,
			// so the arrow has to say which way the *next* render goes.
			Vector<Point> tri;
			// Up for ascending: the arrow says which way the list is ordered now,
			// and the list is ordered by the model before it is redrawn.
			if(ascending) {
				tri.Add(Point(ax, ay + 6));
				tri.Add(Point(ax + 7, ay + 6));
				tri.Add(Point(ax + 3, ay));
			}
			else {
				tri.Add(Point(ax, ay));
				tri.Add(Point(ax + 7, ay));
				tri.Add(Point(ax + 3, ay + 6));
			}
			w.DrawPolygon(tri, ink);
		}
	}
}

void ExplorerColumnHeader::MouseMove(Point p, dword flags)
{
	int c = ColumnAt(p);
	if(c != hover || c >= 0) {
		hover = c;
		inside = c >= 0;
		Refresh();
	}
}

void ExplorerColumnHeader::MouseLeave()
{
	inside = false;
	if(hover >= 0) {
		hover = -1;
		Refresh();
	}
}

void ExplorerColumnHeader::LeftDown(Point p, dword flags)
{
	int c = ColumnAt(p);
	if(c < 0) return;
	pressed = true;
	hover = c;
	Refresh();
	// The strip grabs the click and gives it back on LeftUp, so a press that
	// slides off the heading is not a sort.
	SetCapture();
}

void ExplorerColumnHeader::LeftUp(Point p, dword flags)
{
	if(!HasCapture()) return;
	ReleaseCapture();
	bool was = pressed;
	pressed = false;
	Refresh();
	if(!was) return;
	// Only a release over the same heading sorts: dragging along the strip to
	// the next one and letting go is a sweep, not a choice.
	if(ColumnAt(p) != hover) return;
	WhenSortClick(hover);
}

void ExplorerColumnHeader::RightDown(Point p, dword flags)
{
	if(ColumnAt(p) < 0) return;
	WhenColumnMenu();
}

Image ExplorerColumnHeader::CursorImage(Point p, dword flags)
{
	// The hand only over a heading, so the cursor says something about the strip
	// rather than about the whole window.
	return ColumnAt(p) >= 0 ? Image::Hand() : Image::Arrow();
}

ExplorerColumns::ExplorerColumns()
{
	rows.LayoutId("listing");
	rows.ListMode().MultiSelect();
	rows.ItemHeight(max(DPI(18), Draw::GetStdFontCy()));
	rows.WhenAction << [this] {
		if(GetCursor() >= 0) {
			WhenActionActivate();
			WhenActivate(); // backward compatibility
		}
	};
	rows.WhenLeftDouble << [this] {
		if(GetCursor() >= 0) {
			WhenDoubleClickActivate();
			WhenActivate(); // backward compatibility
		}
	};
	header.WhenSortClick << [this](int c) { WhenSort(c); };
	header.WhenColumnMenu << [this] { WhenColumnMenu(); };

	// A sibling of the list, not a child of it: ColumnList is a Ctrl, not a
	// ParentCtrl, so rows.Add(edit) does not compile, and the field's rect has to
	// be computed from the row the list reports.
	Add(edit);
	edit.WhenAction = [this] { EndRename(true); };
	edit.WhenCancel = [this] { EndRename(false); };

	int hh = header.HeaderHeight();
	Add(header.HSizePos(0, 0).TopPos(0, hh));
	Add(rows.HSizePos(0, 0).VSizePos(hh, 0));
}

void ExplorerColumns::SetActivatePolicy(ExplorerModel::ActivatePolicy p)
{
	// In single-click mode, Enter key activates; in double-click mode, Enter still activates
	// (it's an explicit key). Double-click activates in both modes.
	// The policy mainly affects icon view; here we just ensure both events fire.
	// The panel will decide which to connect based on policy.
	(void)p; // Currently both events fire; panel controls which to listen to
}

void ExplorerColumns::RightDown(Point p, dword keyflags)
{
	// Check if click is on the rows area (not header)
	int hh = header.HeaderHeight();
	if(p.y >= hh) {
		// Convert to screen coordinates and fire context menu event
		Point screen_p = GetScreenRect().TopLeft() + p;
		WhenRightClick(screen_p);
		return;
	}
	// Header right-click is handled by header's own RightDown
	ParentCtrl::RightDown(p, keyflags);
}

Vector<int> ExplorerColumns::MeasureColumns(const ExplorerModel& model) const
{
	// One past the last edge, so the last column's width is the difference
	// between the last two entries and no row has to know how many there are.
	Vector<int> lefts;
	Vector<ListColumn> cols = model.VisibleColumns();
	for(ListColumn c : cols)
		lefts.Add(ListColumnWidth(c));
	int x = 0;
	for(int i = 0; i < lefts.GetCount(); i++) {
		int w = lefts[i];
		lefts[i] = x;
		x += w;
	}
	lefts.Add(x);
	return lefts;
}

void ExplorerColumns::Build(ExplorerModel& model)
{
	// Any rename in progress is over: the row it was renaming has just been
	// rebuilt, and leaving a field over a row that is about to be a different
	// row is how a rename ends up applied to the wrong entry.
	if(editing) EndRename(false);

	Vector<ListColumn> cols = model.VisibleColumns();
	Vector<int> lefts = MeasureColumns(model);
	// A heading with no sort column when the sorted one is hidden, not the
	// arrow on a column that is not there.
	int sort_col = -1;
	int sort_key = ListColumnSortKey((ListColumn)0);
	{
		int key = (int)model.GetSortBy();
		for(int i = 0; i < cols.GetCount(); i++)
			if(ListColumnSortKey(cols[i]) == key) sort_col = i;
	}
	header.SetColumns(lefts, sort_col, model.GetSortDir() == ExplorerModel::ASCENDING);

	rows.Clear();
	row_cells.Clear();
	for(const ExplorerEntry& e : model.Entries()) {
		One<ExplorerRow> row;
		row.Create();
		row->lefts = &header.lefts;
		for(ListColumn c : cols)
			row->values.Add(ListColumnValue(c, e));
		// The value is the name: it is what a command asks for, and it is what
		// NameAt() hands back without a second lookup into the model.
		rows.Add(e.name, *row, true);
		row_cells.Add(pick(row));
	}
	// The rows hold a pointer to the header's vector, and the header is a
	// sibling that outlives them, so nothing dangles -- but the list has to be
	// told to re-measure, because its own idea of a row came from the last set.
	rows.ItemHeight(max(DPI(18), Draw::GetStdFontCy()));
	rows.RefreshLayout();
}

void ExplorerColumns::SelectAll()
{
	for(int i = 0; i < GetCount(); i++) SelectOne(i, true);
}

void ExplorerColumns::InvertSelection()
{
	int n = GetCount();
	Vector<bool> was(n);
	for(int i = 0; i < n; i++)
		was[i] = IsSel(i);
	for(int i = 0; i < n; i++)
		SelectOne(i, !was[i]);
}

void ExplorerColumns::DeselectAll()
{
	// No snapshot needed here, and the reason is worth stating because the loop
	// above looks like it needs one: ColumnList::SelectOne() writes one row's flag
	// and nothing else (ColumnList.cpp), so a plain loop cannot read the state it is
	// writing. InvertSelection reads first because its answer comes *from* the state
	// it is about to overwrite; this one's answer is the constant false.
	for(int i = 0; i < GetCount(); i++)
		SelectOne(i, false);
}

void ExplorerColumns::BeginRename()
{
	if(editing) return;
	int c = GetCursor();
	if(c < 0 || c >= GetCount()) return;
	editing = true;
	edit.SetRect(RenameRect(c));
	edit.SetFont(StdFont());
	edit.SetText(NameAt(c).ToWString());
	edit.SelectAll();
	edit.Show();
	edit.SetFocus();
}

Rect ExplorerColumns::RenameRect(int row) const
{
	// The name column, in the list's own coordinates, moved into this widget's:
	// the field is a sibling, so it does not share the list's origin.
	if(row < 0) return Rect();
	Rect r = const_cast<ColumnList&>(rows).GetItemRect(row);
	int w = header.lefts.GetCount() > 1 ? header.lefts[1] - header.lefts[0] : r.Width();
	// The list's own rect, which is in the parent's coordinates, not the list's:
	// GetPos() would hand back a LogPos here, and the two origins differ.
	Rect lr = rows.GetRect();
	// Edges, not sizes: this fork's Rect_(l, t, r, b). The sizes here were
	// handed to it as edges, and the field ended 4px wide inside the name
	// column -- wide enough to look like it worked, never wide enough to type.
	return Rect(lr.left + header.lefts[0] + 2, lr.top + r.top + 1,
	            lr.left + header.lefts[0] + w - 2, lr.top + r.bottom - 1);
}

void ExplorerColumns::EndRename(bool accept)
{
	if(!editing) return;
	editing = false;
	String newname = edit.GetText().ToString();
	edit.Hide();
	rows.SetFocus();
	if(!accept) return;
	String old = NameAt(GetCursor());
	newname = TrimLeft(TrimRight(newname));
	// The same rule as the rename dialog: an empty name, or one that is only
	// dots, is not a rename. A field left empty by a click elsewhere is a
	// cancelled rename, not a request to delete the entry's name.
	if(newname == old || newname.IsEmpty() || newname == "." || newname == "..")
		return;
	WhenRename(newname);
}

// ---------------------------------------------------------------------------
// ExplorerIconList - ColumnList with drag rectangle selection
// ---------------------------------------------------------------------------

void ExplorerIconList::LeftDown(Point p, dword keyflags)
{
	// Start drag selection on background click (no item under cursor)
	int item = GetItem(p);
	if(item < 0) {
		drag_selecting = true;
		drag_start = p;
		drag_rect = Rect(p.x, p.y, p.x, p.y);
		SetCapture();
		Refresh();
		return;
	}
	// Let base class handle item clicks (selection, activation)
	ColumnList::LeftDown(p, keyflags);
}

void ExplorerIconList::LeftUp(Point p, dword keyflags)
{
	if(drag_selecting) {
		drag_selecting = false;
		ReleaseCapture();
		// Select all items within the drag rectangle
		Rect sel_rect = drag_rect.Normalized();
		for(int i = 0; i < GetCount(); i++) {
			Rect item_rect = GetItemRect(i);
			if(sel_rect.Intersects(item_rect)) {
				SelectOne(i, true);
			}
		}
		drag_rect = Rect();
		Refresh();
		WhenSel();
		return;
	}
	ColumnList::LeftUp(p, keyflags);
}

void ExplorerIconList::MouseMove(Point p, dword keyflags)
{
	if(drag_selecting) {
		drag_rect = Rect(drag_start.x, drag_start.y, p.x, p.y);
		Refresh();
		return;
	}
	ColumnList::MouseMove(p, keyflags);
}

void ExplorerIconList::LeftDrag(Point p, dword keyflags)
{
	if(drag_selecting) {
		drag_rect = Rect(drag_start.x, drag_start.y, p.x, p.y);
		Refresh();
		return;
	}
	ColumnList::LeftDrag(p, keyflags);
}

void ExplorerIconList::CancelMode()
{
	if(drag_selecting) {
		drag_selecting = false;
		drag_rect = Rect();
		ReleaseCapture();
		Refresh();
		return;
	}
	ColumnList::CancelMode();
}

void ExplorerIconList::Paint(Draw& w)
{
	// Paint base first
	ColumnList::Paint(w);
	// Paint drag selection rectangle on top
	if(drag_selecting && !drag_rect.IsEmpty()) {
		Rect r = drag_rect.Normalized();
		w.DrawRect(r, SColorHighlight());
	}
}

bool ExplorerIconList::Key(dword _key, int count)
{
	// Handle Ctrl+arrows: move cursor without changing selection
	// This preserves the selection while navigating
	if((_key & K_CTRL) && !(_key & K_SHIFT)) {
		int key = _key & ~K_CTRL;
		int c = GetCursor();
		switch(key) {
		case K_UP:
			if(c < 0) c = GetCount() - 1;
			else if(c > 0) c--;
			break;
		case K_DOWN:
			if(c < 0) c = 0;
			else if(c < GetCount() - 1) c++;
			break;
		case K_LEFT:
			c = max(c - GetColumnItems(), 0);
			break;
		case K_RIGHT:
			c = min(c + GetColumnItems(), GetCount() - 1);
			break;
		case K_HOME:
			c = 0;
			break;
		case K_END:
			c = GetCount() - 1;
			break;
		default:
			return ColumnList::Key(_key, count);
		}
		SetCursor(c);
		return true;
	}
	// Handle Ctrl+Space: toggle selection of current item
	if((_key & K_CTRL) && (_key & ~K_CTRL) == K_SPACE) {
		int c = GetCursor();
		if(c >= 0 && GetCount() > 0) {
			SelectOne(c, !IsSelected(c));
			return true;
		}
	}
	return ColumnList::Key(_key, count);
}

void ExplorerIconList::MouseWheel(Point p, int zdelta, dword keyflags)
{
	// Ctrl+wheel: zoom in/out
	if(keyflags & K_CTRL) {
		WhenZoom(zdelta > 0 ? 1 : -1);
		return;
	}
	ColumnList::MouseWheel(p, zdelta, keyflags);
}

void ExplorerIconList::RightDown(Point p, dword keyflags)
{
	// Convert to screen coordinates
	Point screen_p = GetScreenRect().TopLeft() + p;
	// Check if there's an item under the cursor
	int item = GetItem(p);
	WhenRightClick(screen_p, item);
	// Don't call base class RightDown - we handle context menu ourselves
}

ExplorerPanel::ExplorerPanel(const char *name)
{
	caption.SetLabel(name);

	// Both listings get the same verbs, so a command above does not have to know
	// which one is showing. WhenAction alone is not enough: a list only turns a
	// double click into "open" when it is being used as a file dialog, so in a
	// plain panel the double click arrives with nothing bound to it -- which is
	// exactly the "double clicking does nothing" a user reports.
	// The activation policy (single vs double click) controls which event fires
	// WhenActivated. Default is DOUBLE_CLICK.
	auto activate_single = [this] {
		int i = Cursor();
		if(i >= 0) WhenActivated(i);
	};
	auto activate_double = [this] {
		int i = Cursor();
		if(i >= 0) WhenActivated(i);
	};
	// Default: double-click activates. Single-click (Enter) also activates in detailed view.
	columns.WhenActionActivate << activate_single;
	columns.WhenDoubleClickActivate << activate_double;
	icons.WhenAction << activate_single;
	icons.WhenLeftDouble << activate_double;
	icons.LayoutId("icons");
	// ExplorerIconList constructor already sets RowMode().MultiSelect().NoRoundSize()

	// Ctrl+wheel zoom: forward to panel's WhenZoom
	icons.WhenZoom << [this](int dir) {
		if(icon_mode)
			WhenZoom(dir);
	};

	// Right-click context menu: fire panel's context menu events
	icons.WhenRightClick << [this](Point screen_pos, int item) {
		if(item >= 0 && IsSelected(item)) {
			// Right-click on selected entry
			WhenEntryContextMenu(screen_pos);
		}
		else {
			// Right-click on background or unselected entry
			// If clicking on unselected entry, select it first
			if(item >= 0) {
				SelectOne(item, true);
				SetCursor(item);
			}
			else {
				// Background click - clear selection
				DeselectAll();
			}
			WhenBackgroundContextMenu(screen_pos);
		}
	};

	// Also handle right-click in detailed view (columns)
	columns.WhenRightClick << [this](Point screen_pos) {
		// Convert screen position to columns client coordinates
		Point client_pos = columns.GetScreenRect().TopLeft();
		client_pos = screen_pos - client_pos;
		int item = columns.GetItem(client_pos);
		if(item >= 0 && IsSelected(item)) {
			WhenEntryContextMenu(screen_pos);
		}
		else {
			if(item >= 0) {
				SelectOne(item, true);
				SetCursor(item);
			}
			else {
				DeselectAll();
			}
			WhenBackgroundContextMenu(screen_pos);
		}
	};

	int lry = location.RowHeight();
	int ccy = GetStdFont().GetHeight() + 8;
	Add(location.HSizePos(0, 0).TopPos(0, lry));
	Add(caption.HSizePos(4, 4).TopPos(lry + 2, ccy));
	Add(columns.HSizePos(0, 0).VSizePos(lry + ccy + 4, 0));
	Add(icons.HSizePos(0, 0).VSizePos(lry + ccy + 4, 0));
}

void ExplorerPanel::SetActivatePolicy(ExplorerModel::ActivatePolicy p)
{
	activation_policy = p;
	// Disconnect all activation events
	columns.WhenActionActivate.Clear();
	columns.WhenDoubleClickActivate.Clear();
	icons.WhenAction.Clear();
	icons.WhenLeftDouble.Clear();

	auto activate_single = [this] {
		int i = Cursor();
		if(i >= 0) WhenActivated(i);
	};
	auto activate_double = [this] {
		int i = Cursor();
		if(i >= 0) WhenActivated(i);
	};

	if(p == ExplorerModel::SINGLE_CLICK) {
		// Single-click mode: single-click (WhenAction) activates, double-click also activates
		columns.WhenActionActivate << activate_single;
		icons.WhenAction << activate_single;
		// Double-click still activates for explicit double-click
		columns.WhenDoubleClickActivate << activate_double;
		icons.WhenLeftDouble << activate_double;
	}
	else {
		// Double-click mode: Enter key activates, double-click activates
		// Single-click (icons.WhenAction) only selects, doesn't activate
		columns.WhenActionActivate << activate_single;
		columns.WhenDoubleClickActivate << activate_double;
		icons.WhenLeftDouble << activate_double;
	}
}

int ExplorerPanel::IconColumns() const
{
	// One column is the floor: a panel narrower than a single cell has to show one
	// cell rather than divide by zero, which is the same rule the headless twin
	// applies through ViewColumns().
	int w = GetSize().cx - 4;
	if(w < 1) w = 1;
	return ViewColumns(view_mode, w, icon_size);
}

void ExplorerPanel::RenderIcons(ExplorerModel& model)
{
	Size cell = model.CellSize();
	// One column when the panel is in the detailed mode: RowMode divides by the
	// column count, and zero is a crash rather than an empty grid. The list is not
	// visible then anyway -- it is filled so that switching modes shows the right
	// listing immediately.
	icons.ItemWidth(max(1, cell.cx)).ItemHeight(max(1, cell.cy))
	    .Columns(max(1, IconColumns()));
	icons.Clear();
	// ColumnList keeps a *pointer* to the display of a row, so a temporary on the
	// stack is a dangling pointer by the next line. One<> per cell: individually
	// allocated, and the vector's own reallocation cannot move them.
	cells.Clear();
	for(const ExplorerEntry& e : model.Entries()) {
		// One<> is a pointer here: Create() allocates, operator-> asserts. Declaring
		// one and writing through it trips the assertion rather than allocating.
		One<IconCell> c;
		c.Create();
		c->icon = ExplorerIcons::Get(EntryIconKind(e), ViewIconSizeAt(model.GetViewMode(), icon_size));
		c->label = e.name;
		c->is_dir = e.IsDir();
		c->Mode(model.GetViewMode() == VIEW_SMALL_ICONS ? ICON_MODE_SMALL
		                                                 : ICON_MODE_BIG);
		c->icon_size = model.GetViewZoom();
		icons.Add(e.name, *c, true);
		cells.Add(pick(c));
	}
}

void ExplorerPanel::FocusList()
{
	// Whichever list this panel is *drawing*, not whichever was drawn last: the
	// icon list is filled in the detailed mode too, so focusing "the icon list"
	// unconditionally would put the caret in a control the user cannot see.
	if(icon_mode) icons.SetFocus();
	else         columns.SetFocus();
}

void ExplorerPanel::Render(ExplorerModel& model)
{
	// Before the lists are filled, not after. RenderIcons asks the panel which mode
	// it is drawing to work out the cell size and the column count, and a panel
	// still reporting the *previous* mode divides by zero inside ColumnList::SetSb
	// (ncl == 0 in RowMode), which is a divide-by-zero crash on the first render of
	// a panel opened in the small-icon mode.
	view_mode = model.GetViewMode();
	// The zoom goes in before the lists are filled, for the same reason the mode
	// does: RenderIcons() sizes the cells from the model, and a cell sized from
	// the previous zoom is a grid with the wrong number of columns.
	icon_size = ViewZoomClamp(model.GetViewZoom());
	icon_mode = view_mode != VIEW_LIST;
	caption.SetLabel(model.GetPath());

	// Apply the model's activation policy to this panel
	SetActivatePolicy(model.GetActivatePolicy());

	// Both listings are filled, and only one is shown. A list left stale from the
	// last time its mode was active would come back showing a listing from two
	// navigations ago the moment the mode is switched again.
	columns.Build(model);
	RenderIcons(model);

	columns.Show(!icon_mode);
	icons.Show(icon_mode);
}

int ExplorerPanel::Count() const
{
	return icon_mode ? icons.GetCount() : columns.GetCount();
}

int ExplorerPanel::Cursor() const
{
	return icon_mode ? icons.GetCursor() : columns.GetCursor();
}

bool ExplorerPanel::IsSelected(int row) const
{
	if(icon_mode) return icons.IsSel(row);
	return columns.IsSel(row);
}

void ExplorerPanel::SelectOne(int row, bool select)
{
	if(icon_mode) icons.SelectOne(row, select);
	else columns.SelectOne(row, select);
}

void ExplorerPanel::SetCursor(int row)
{
	if(icon_mode) icons.SetCursor(row);
	else columns.SetCursor(row);
}

String ExplorerPanel::NameAt(int row) const
{
	// In icon mode the row *is* the name: the ColumnList keys hold the entry names,
	// because a cell display is presentation and the key is what a command needs.
	if(icon_mode)
		return (row < 0 || row >= icons.GetCount()) ? String() : icons.Get(row).ToString();
	return columns.NameAt(row);
}

void ExplorerPanel::SelectAll()
{
	for(int i = 0; i < Count(); i++)
		SelectOne(i, true);
}

void ExplorerPanel::InvertSelection()
{
	// Read the whole selection state first: inverting row by row while writing
	// SelectOne can settle back to the state it started in, because a plain
	// SelectOne may move the cursor and the cursor decides the last row.
	int n = Count();
	Vector<bool> was(n);
	for(int i = 0; i < n; i++)
		was[i] = IsSelected(i);
	for(int i = 0; i < n; i++)
		SelectOne(i, !was[i]);
}

void ExplorerPanel::DeselectAll()
{
	// On the list that is showing, which is the whole point of going through the
	// panel: a Deselect all that only worked in one of the two listing flavours
	// would look like it worked in the other one too.
	for(int i = 0; i < Count(); i++)
		SelectOne(i, false);
}

// ---------------------------------------------------------------------------
// ExplorerPane
// ---------------------------------------------------------------------------

ExplorerPanel* ExplorerPane::CreatePanel(const String& name)
{
	ExplorerPanel* panel = new ExplorerPanel(name);
	panels.Add(panel);
	return panel;
}

void ExplorerPane::WirePanel(ExplorerPanel& p, std::function<void(const String&)> on_path_enter,
                             std::function<void(String)> on_filter_text,
                             std::function<void()> on_activated,
                             std::function<void(int)> on_sort,
                             std::function<void()> on_column_menu,
                             std::function<void()> on_picked,
                             std::function<void(const String&)> on_rename)
{
	// The pane's own location row. The panel does not know which pane it is
	// (tabs re-index when one closes), so the owner resolves it the same way
	// WhenPicked does. A component button and a typed Enter both arrive as
	// WhenPathEnter; the parameter is the path the user asked for, so a button
	// can never navigate to the field's stale text.
	p.location.path_ctrl.WhenPath << [on_path_enter, &p](const String& path) {
		p.WhenPathEnter(path);
		if(on_path_enter) on_path_enter(path);
	};
	p.location.WhenGo << [&p] { if(p.WhenPathEnter) p.WhenPathEnter(String()); };
	p.location.filter.WhenAction = [on_filter_text, &p] {
		if(on_filter_text) on_filter_text(p.location.filter.GetText().ToString());
		p.WhenFilterText(p.location.filter.GetText().ToString());
	};
	p.location.Show(true);
	// The pane's row navigates and filters that pane.
	p.WhenPathEnter << [on_path_enter](const String& path) {
		if(on_path_enter) on_path_enter(path);
	};
	p.WhenFilterText << [on_filter_text](String text) {
		if(on_filter_text) on_filter_text(text);
	};
	p.WhenActivated << [on_activated](int row) {
		if(on_activated) on_activated();
	};
	p.columns.WhenActivate << [on_activated] {
		if(on_activated) on_activated();
	};
	p.columns.WhenSort << [on_sort](int col) {
		if(on_sort) on_sort(col);
	};
	p.columns.WhenColumnMenu << [on_column_menu] {
		if(on_column_menu) on_column_menu();
	};
	// A click in a panel is what makes it the pane a command acts on.
	p.WhenPicked << [on_picked] {
		if(on_picked) on_picked();
	};
	p.columns.WhenRename << [on_rename](const String& name) {
		if(on_rename) on_rename(name);
	};
}

int ExplorerPane::NewTab(const String& path, const String& name)
{
	ExplorerPanel* panel = CreatePanel(name);
	WirePanel(*panel,
		[this, panel](const String& path) { /* handled by owner */ },
		[this, panel](String text) { /* handled by owner */ },
		[this] { if(WhenTabChange) WhenTabChange(); },
		[this](int col) { /* handled by owner */ },
		[this] { /* handled by owner */ },
		[this] { /* handled by owner */ },
		[this](const String& name) { /* handled by owner */ }
	);
	// The owner will call the actual callbacks via its own wiring
	// This is just the panel's internal wiring
	
	models.Add(ExplorerModel());
	int tab = models.GetCount() - 1;
	tabs.Add(panel->SizePos(), DirIcon(), name);
	tabs.Set(tab);
	panels[tab]->Render(models[tab]);
	panels[tab]->SyncLocation(models[tab].GetPath(), models[tab].GetFilter());
	if(WhenTabChange) WhenTabChange();
	return tab;
}

bool ExplorerPane::CloseTab(int tab)
{
	if(tab < 0 || tab >= models.GetCount()) return true;
	if(models.GetCount() == 1) return false; // Last tab - caller should close window
	
	models.Remove(tab);
	panels.Remove(tab);
	tabs.Remove(tab);
	
	int active = tabs.Get();
	if(active >= models.GetCount()) active = models.GetCount() - 1;
	if(active >= 0) {
		tabs.Set(active);
		panels[active]->Render(models[active]);
		panels[active]->SyncLocation(models[active].GetPath(), models[active].GetFilter());
	}
	if(WhenTabChange) WhenTabChange();
	return true;
}

int ExplorerPane::RemoveAllTabs()
{
	if(models.GetCount() < 2) return ActiveTab();
	int keep = ActiveTab();
	for(int i = models.GetCount() - 1; i >= 0; i--)
		if(i != keep)
			CloseTab(i);
	return keep;
}

ExplorerModel& ExplorerPane::ActiveModel()
{
	int tab = ActiveTab();
	if(tab < 0 || tab >= models.GetCount()) {
		// Should not happen, but safety first
		if(models.GetCount() == 0) models.Add(ExplorerModel());
		return models[0];
	}
	return models[tab];
}

ExplorerPanel& ExplorerPane::ActivePanel()
{
	int tab = ActiveTab();
	if(tab < 0 || tab >= panels.GetCount()) {
		// Should not happen
		if(panels.GetCount() == 0) CreatePanel("Files");
		return *panels[0];
	}
	return *panels[tab];
}

void ExplorerPane::RenderActive()
{
	ActivePanel().Render(ActiveModel());
}

void ExplorerPane::SyncLocationActive()
{
	ExplorerModel& m = ActiveModel();
	ActivePanel().SyncLocation(m.GetPath(), m.GetFilter());
}

void ExplorerPane::FocusListActive()
{
	ActivePanel().FocusList();
}

// ---------------------------------------------------------------------------
// Explorer
// ---------------------------------------------------------------------------

int Explorer::ActiveTab()
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	return p.ActiveTab();
}

int Explorer::GetTabCount()
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	return p.GetTabCount();
}

// The model of the pane a command acts on. Everything above goes through here,
// which is what lets a split view have two independent listings without any
// command having to know there are two.
ExplorerModel& Explorer::ActiveModel()
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	return p.ActiveModel();
}

ExplorerPanel& Explorer::ActivePanel()
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	return p.ActivePanel();
}

// Turning the split on adds a second pane beside (or below) the main pane
// and gives it its own tabs/models, opened on the same directory. Turning it off
// hides the split pane: the split pane keeps its tabs/history across off/on cycles.
void Explorer::SetSplit(bool sideways)
{
	// The two View-bar items name two arrangements of the main area, and the
	// arrangement already showing is dismissed by clicking it again: two
	// direction items with no way back is a one-way door, which is what the
	// old wiring was -- "stacked" from the default state matched the early
	// return and did nothing at all, and while the split was on it removed it
	// under a name that promised a stacked arrangement nobody could reach.
	// The direction is remembered (split_dir), so a later "Two panels" comes
	// back the way it was left.
	if(split_on && split_dir == sideways) {
		SetSplitOff();
		return;
	}
	if(split_on) {
		// Direction change while the split is showing: the arrangement flips in
		// place.
		split_dir = sideways;
		if(sideways) panes_split.Horz();
		else          panes_split.Vert();
		panes_split.Layout();
		UpdateBars();
		UpdateStatus();
		return;
	}
	split_dir = sideways;
	split_on = true;
	
	// Ensure split_pane has at least one tab
	if(split_pane.GetTabCount() == 0) {
		split_pane.NewTab(main_pane.ActiveModel().GetPath(), "Files");
	}
	
	// Move main_pane's tab ctrl and split_pane's tab ctrl into panes_split
	if(main_pane.tabs.GetParent() != &panes_split)
		panes_split.Add(main_pane.tabs);
	if(split_pane.tabs.GetParent() != &panes_split)
		panes_split.Add(split_pane.tabs);
	if(panes_split.GetParent() != &split)
		split.Add(panes_split);
	panes_split.SizePos();
	
	// Render the split pane's active tab
	split_pane.RenderActive();
	split_pane.SyncLocationActive();
	
	panes_split.NoZoom();
	if(sideways) panes_split.Horz();
	else          panes_split.Vert();
	panes_split.Layout();
	split.Layout();
	UpdateBars();
	UpdateStatus();
}

void Explorer::SetSplitOff()
{
	if(!split_on) return;
	// Hide the split pane using the splitter's zoom: child 0 is main_pane,
	// child 1 is split_pane. Zooming to child 0 hides child 1.
	split_on = false;
	pane = 0;
	panes_split.Zoom(0);
	UpdateBars();
	UpdateStatus();
}

void Explorer::WirePolicyConfirmation()
{
	GetExplorerPolicy().confirm
		= [](const ExplorerPolicy::Request& req, const String& why) {
		String msg;
		msg << "Explorer wants to " << ExplorerPolicy::ActionName(req.action);
		msg << "\n\n" << req.path;
		if(!req.purpose.IsEmpty())
			msg << "\n\n" << req.purpose;
		if(!why.IsEmpty())
			msg << "\n\n" << why;
		// PromptYesNo takes QTF too, and this one is assembled from a path the user
		// does not control: a mount point may legitimately contain "[", which the QTF
		// parser would read as the start of a style. DeQtf is what makes the string
		// text instead of markup.
		return PromptYesNo(DeQtf(~msg)) == 1;
	};
}

// status_cells is initialized from `status`, so it has to be in the member
// initializer list rather than assigned here. `status` is declared before it, so
// it is constructed first and the reference it takes is to a live object.
Explorer::Explorer(const String& initial_path, bool dump_ui, ViewMode initial_view,
                   bool folders_first, bool ignore_case)
	: status_cells(status)
{
	SetTitle(t_("Explorer"));
	Sizeable();
	SetRect(80, 80, 1000, 640);
	this->ui_dump = dump_ui;

	WirePolicyConfirmation();

	nav.WhenPlaceSelected = [this](String path) { OpenPath(path); };
	nav.Populate();

	// Create main pane with initial tab
	main_pane.NewTab(initial_path.IsEmpty() ? GetCurrentDirectory() : initial_path, "Files");
	main_pane.models.Top().SetViewMode(initial_view);
	main_pane.models.Top().SetDirsFirst(folders_first);
	main_pane.models.Top().SetIgnoreCase(ignore_case);
	
	// Wire main pane's tab change to update UI
	main_pane.WhenTabChange << [this] {
		UpdateHistoryButtons();
		UpdateBars();
		UpdateStatus();
	};

	split.Horz();
	split.Add(nav);
	split.Add(main_pane.tabs);
	nav.SizePos();
	main_pane.tabs.SizePos();

	this->Add(split.SizePos());

	home_path = GetHomeDirectory();
	if(home_path.IsEmpty()) home_path = "/";
	
	// Open the initial path in the main pane
	OpenPath(initial_path.IsEmpty() ? GetCurrentDirectory() : initial_path);
	RefreshNav();

	// Each frame is added immediately before it is filled. A frame's height is
	// measured from its contents, and AddFrame caches that measurement, so a frame
	// registered before it has items keeps the empty height forever. The menu and
	// the toolbar also build from the active model, so they can only be filled once
	// the first tab and its panel exist.
	this->AddFrame(menu);
	menu.Set(THISBACK(Menu));

	// The three button groups are added in one pass, in xfe's order, each already
	// sized: a frame caches the height its contents measure at AddFrame time, and
	// filling a bar afterwards leaves the row at the height it was measured empty.
	// All three button groups, the same three settings, because they are one row
	// each and a row of mixed-size buttons is not a row:
	//
	// NOLABEL        untitled, as xfe's groups are; the text is in the tooltip and
	//                in the menu, and four labelled rows cost the listing its height.
	// MaxIconSize    the icons are borrowed from CtrlLib and they are not all the
	//                same size -- `write` is 48x48 next to a 16x16 `copy`, and one
	//                tall button sets the height of the whole row, which is how a
	//                16px icon ended up in a 58px row and the listing lost 80px.
	// ButtonMinSize every button the same square, so the icons line up and a
	//                glyph that is only 3x3 still has something to sit in.
	// The three groups feed one bar. Compact is the factory state (the review's
	// wording -- "it should be in compact mode by default"): the pane wraps
	// items itself, so the groups share rows and only what does not fit wraps.
	// The one-row-per-group look stays reachable as a View menu option; the
	// bar is the same, only the gaps between the groups turn into breaks.
	// Which of the two layouts is in force, and which groups are shown, is the
	// user's configuration and survives the run: loaded before the bar is
	// first filled, saved on every change. The first panel is built earlier
	// than the load, so its location row applies the loaded flag here; later
	// panels read it in WirePanel.
	LoadToolbarConf();
	for(int i = 0; i < main_pane.panels.GetCount(); i++)
		main_pane.panels[i]->SetLocationShown(bars_shown[3]);
	if(split_pane.panels.GetCount() > 0)
		split_pane.panels[0]->SetLocationShown(bars_shown[3]);
	all_bars.ButtonKind(ToolButton::NOLABEL);
	all_bars.MaxIconSize(Size(16, 16));
	all_bars.ButtonMinSize(Size(22, 22));
	all_bars.Set(THISBACK(CombinedBars));
	this->AddFrame(all_bars);
	// Enter in the field is "go there", and a click on a component button is a
	this->AddFrame(status);
	// Index 1, not AddFrame(): the size grip is already a frame of the status bar
	// and was added by its constructor, so appending would put the cell panels
	// *after* it in the paint order and draw a border across the grip.
	status.InsertFrame(1, status_cells);

	frames_ready = true;
	UpdateHistoryButtons();
	UpdateStatus();
	UpdateBars();

	// Remove all tabs, exercised once for a --dump-ui run, because the one thing
	// about that command nobody should have to take on trust from reading it is the
	// last tab: CloseTab() closes the *window* for it, so an implementation that
	// walked the tabs without thinking ends the window on the final iteration -- and
	// a run that exits 0 either way is exactly the difference a probe exists to
	// catch. Two extra tabs, so "one tab left" is a statement about the command and
	// not about a window that only ever had one. Here rather than in the dump,
	// because opening a tab adds a control to the tab strip, and the paint loop is
	// the worst place to do that.
	if(ui_dump) {
		ExplorerPane& p = main_pane;
		String here = p.ActiveModel().GetPath();
		p.NewTab(here);
		p.NewTab(here);
		int opened = p.GetTabCount();
		// Back to the tab the window opened on, because Remove all tabs keeps the
		// *active* one -- and the tab it would otherwise keep is one of the two this
		// probe just made, whose sort settings are the defaults rather than the ones
		// the run was asked for. The probe would then report the state of a tab
		// nobody opened, which is a measurement of the probe.
		p.tabs.Set(0);
		p.RemoveAllTabs();
		tab_probe = Sprintf("%d tabs -> %d tab, kept \"%s\"", opened, p.GetTabCount(),
		                    ~p.ActiveModel().GetPath());
				// Back where the window started, so the dump describes what the tab that
		// survived shows rather than what a tab opened by the probe showed.
		OpenPath(here);

		// Synchronize panels, exercised end to end. The half worth checking is Back:
		// a shared path *and* a shared history would drag the other pane back too,
		// which is the reason reading (1) was rejected -- and it is invisible in a
		// screenshot, invisible in the widget tree, and silent in a log. So the probe
		// navigates the split pane and reads both models' paths, then goes Back in
		// the split pane and reads them again.
		//
		// It needs a directory other than the one the window opened on, and its
		// parent, which is the directory the window was opened on -- no fixture
		// needed for it to mean anything.
		{
			String here = main_pane.ActiveModel().GetPath();
			String other = here.EndsWith("/") ? here + ".." : here + "/..";
			SetSplit(true);
			ToggleSyncPanels();      // aligns the split to the tab, then switches on
			pane = 1;
			NavigateTo(1, other);
			String followed = main_pane.ActiveModel().GetPath();
			String error;
			bool back = split_pane.ActiveModel().Back(error);
			NavigateTo(1, split_pane.ActiveModel().GetPath(), false);
			String held = main_pane.ActiveModel().GetPath();
			pane = 0;
			sync_panels = false;
			SetSplitOff();           // the probe's business was the split's off state
			OpenPath(here);
			sync_probe = Sprintf("sync on: split pane -> \"%s\", tab followed -> \"%s\"%s",
			                     ~other, ~followed, followed == other ? "" : " MISMATCH");
			sync_probe << Sprintf("; back in the split pane -> \"%s\", tab stayed -> \"%s\"%s",
			                      ~split_pane.ActiveModel().GetPath(), ~held, held == followed ? "" : " MISMATCH");
			sync_probe << (back ? "" : " (the split pane had nothing to go back to)");
		}
	}

	EINFO(ExplorerChannels::GUI, "main window ready");
}

// The Go verb, for the button, Enter in the field and the drop-list.
//
// The field is put back to wherever the navigation actually landed, rather than
// left showing what was typed: a path that does not exist should be reported and
// replaced, not left half-typed looking like the current location.
void Explorer::GoToTypedPath(ExplorerPanel& p)
{
	// The pane whose field was typed is the pane the navigation acts on: the
	// row lives inside that pane, so this *is* a click in the pane -- the
	// window's acting pane follows the row, not the other way round.
	bool was_split = (pane == 1);
	String wanted = p.location.path_ctrl.GetEditPath();
	if(wanted.IsEmpty()) {
		SyncChrome(p, ActiveModel());
		return;
	}
	if(was_split) pane = 1;
	OpenPath(wanted);
	pane = was_split ? 1 : 0;
	SyncChrome(p, ActiveModel());
}

void Explorer::OpenPathAt(int tab, const String& path)
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	if(tab < 0 || tab >= p.GetTabCount()) return;
	String error;
	if(!p.models[tab].SetPath(path, error))
		EWARN(ExplorerChannels::MODEL, "cannot open " << path << ": " << error);

	if(tab == p.ActiveTab()) {
		p.ActivePanel().Render(p.models[tab]);
		p.ActivePanel().SyncLocation(p.models[tab].GetPath(), p.models[tab].GetFilter());
		SyncChrome(p.ActivePanel(), p.models[tab]);
	}
	// Every navigation goes through here, so this is the one place that has to
	// refresh the history-dependent enable state.
	UpdateHistoryButtons();
}

// The breadcrumb, the address bar, the filter field, the places panel, the history
// list, the tree and the status cells, from one model. Split out of OpenPathAt
// because the split pane has to be able to do the same seven things about a
// *different* model, and a copy of seven lines is how the two drift.
void Explorer::SyncChrome(ExplorerPanel& p, ExplorerModel& m)
{
	p.SyncLocation(m.GetPath(), m.GetFilter());
	nav.SelectPlace(m.GetPath());
	// The tree follows the listing. Every navigation ends here, so this is the
	// one place that has to keep them together.
	nav.SyncTree(m.GetPath());
	UpdateStatus();
}

void Explorer::OpenPath(const String& path)
{
	NavigateTo(pane, path);
}

// One funnel for every navigation, because the synchronize-panels rule is a claim
// about *all* of them. A click, Go, the address bar, a bookmark, a sidebar row and
// a path typed by a script are six routes into a window; a rule applied in five
// of them is a rule that surprises someone exactly once, and that once is a bug
// report.
//
// One rule, with no exceptions: a navigation acts on the pane the window is
// already acting on. Not "the listing the user clicked in" -- the sidebar, the Go
// menu and the address bar are not clicks in a pane, and a sidebar that navigated
// somewhere other than where the window said it was would be its own bug.
//
// The rule itself is reading (3) of "synchronized": a navigation shows the same
// directory in both panes, and Back, Forward and Up never do. So the two models
// keep their own histories and only the current path is shared -- which is what
// makes the split worth having at all, since one shared path *and* one shared
// history would leave "Back" ambiguous and the two panes useless for comparing.
void Explorer::NavigateTo(int target, const String& path, bool follow)
{
	const bool share = sync_panels && follow;
	ExplorerPane& target_pane = target == 1 ? split_pane : main_pane;
	ExplorerPane& other_pane = target == 1 ? main_pane : split_pane;
	
	if(target == 1) {
		String error;
		if(!target_pane.ActiveModel().SetPath(path, error))
			EWARN(ExplorerChannels::MODEL, "cannot open " << path << ": " << error);
		target_pane.RenderActive();
		target_pane.SyncLocationActive();
		if(pane == 1)
			SyncChrome(target_pane.ActivePanel(), target_pane.ActiveModel());
		if(share) {
			// The other pane follows, and is re-rendered whether or not it is the pane
			// being looked at: a synchronized split that shows the same directory
			// in one pane and a stale one in the other is not synchronized. The
			// followed pane's row shows the shared directory too.
			String ignored;
			other_pane.ActiveModel().SetPath(path, ignored);
			other_pane.RenderActive();
			other_pane.SyncLocationActive();
		}
	}
	else {
		OpenPathAt(target_pane.ActiveTab(), path);
		if(share && split_on) {
			String error;
			if(!split_pane.ActiveModel().SetPath(path, error))
				EWARN(ExplorerChannels::MODEL, "cannot follow " << path << ": " << error);
			split_pane.RenderActive();
			split_pane.SyncLocationActive();
		}
	}
	// The active pane's history is what the two navigation buttons describe, and
	// the split branch above can have changed it.
	UpdateHistoryButtons();
}

void Explorer::SwitchPane()
{
	if(!split_on) return;
	pane = pane == 0 ? 1 : 0;
	// The focus moves with the target. Making pane 1 the pane a command acts on
	// while the keyboard focus is still in pane 0 is the version of this bug that
	// deletes the wrong thing: the status line would say one directory and the
	// Delete key would act on another.
	ActivePanel().FocusList();
	UpdateStatus();
}

void Explorer::ToggleSyncPanels()
{
	sync_panels = !sync_panels;
	if(sync_panels && split_on) {
		// Align the other pane now rather than at the next navigation: a checkbox
		// that changes nothing until the user happens to navigate reads as a
		// checkbox that does not work.
		NavigateTo(pane == 0 ? 1 : 0, ActiveModel().GetPath());
	}
	UpdateStatus();
}

int Explorer::NewTab(const String& path)
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	int tab = p.NewTab(path, "Files");
	// A new tab inherits the view mode of the tab it was opened from. xfe's panels
	// each remember their own mode, and a user who reads everything as icons does
	// not want every new tab to come back as a column listing.
	if(p.GetTabCount() > 1)
		p.models[tab].SetViewMode(p.models[p.GetTabCount() - 2].GetViewMode());
	p.tabs.Set(tab);
	p.RenderActive();
	p.SyncLocationActive();
	UpdateStatus();
	UpdateHistoryButtons();
	return tab;
}

void Explorer::RemoveAllTabs()
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	// One tab is the window: there is nothing to remove, and CloseTab() would close
	// the window rather than the tab. Reaching that by accident is the one outcome
	// this command must not have.
	if(p.GetTabCount() < 2) return;
	int keep = p.ActiveTab();
	// Down from the end, skipping `keep`. Closing upwards would renumber the tab
	// being kept on every step below it -- Remove(0) moves the kept tab from index 3
	// to index 2 -- and the loop would close it like any other. Closing from the
	// end leaves every index below the cursor untouched until it is used.
	//
	// CloseTab() is reused rather than the removal written out again: it is what
	// already re-renders the surviving tab, re-points the breadcrumb and the tree,
	// and settles the active index. One code path for "a tab went away" is what
	// stops Remove all tabs from leaving the chrome describing a tab that is not
	// there.
	for(int i = p.GetTabCount() - 1; i >= 0; i--)
		if(i != keep)
			p.CloseTab(i);
}

void Explorer::CloseTab(int tab)
{
	ExplorerPane& p = pane == 1 ? split_pane : main_pane;
	if(tab < 0 || tab >= p.GetTabCount()) return;
	if(p.GetTabCount() == 1) {
		// The last tab is the window. Closing it closes the window.
		Close();
		return;
	}
	if(!p.CloseTab(tab)) {
		// Should not happen - last tab handled above
		return;
	}
	// CloseTab already updates the active tab, renders it, and syncs location
	UpdateStatus();
	UpdateHistoryButtons();
}

void Explorer::ActivateSelection()
{
	ExplorerModel& m = ActiveModel();
	ExplorerPanel& p = ActivePanel();
	int row = p.Cursor();
	if(row < 0 || row >= m.GetCount()) return;

	const ExplorerEntry& e = m.Entries()[row];
	if(!e.IsDir()) {
		EINFO(ExplorerChannels::OPS, "open file " << e.name << ": no viewer yet");
		return;
	}

	String base = m.GetPath();
	String next = base.EndsWith("/") ? base + e.name : base + '/' + e.name;
	OpenPath(next);
}

void Explorer::ApplyFilter(ExplorerPanel& p, const String& text)
{
	filter_text = text;
	ExplorerModel& m = ActiveModel();
	m.SetFilter(text);
	p.Render(m);
	UpdateStatus();
}

void ExplorerStatusCells::FramePaint(Draw& w, const Rect& r)
{
	paints++;
	last_paint = r;
	int n = bar.GetTabCount();
	// GetTabOffset(i) is the *right* edge of cell i, so the cells are walked
	// edge to edge: each cell's left edge is the previous cell's right edge, and
	// the first cell starts at InfoCtrl's own left margin -- which is a literal 2
	// in InfoCtrl::Paint (`r.right = 2`) and has no accessor, so it is named here
	// rather than sprinkled. If it ever changes, the panels shift by two pixels,
	// which is visible, not silent.
	int a = r.left + CELL_MARGIN;
	for(int i = 0; i < n; i++) {
		int b = bar.GetTabOffset(i);
		// A cell narrower than its own border is not a cell, it is a rounding
		// error: four one-pixel lines in a box that cannot hold them. Skip it
		// rather than draw a smear.
		if(b - a >= 2 * CELL_PAD)
			// The 98 raised pair: 3DSHADOW on the top and the left, 3DLIGHT on the
			// bottom and the right. Not a ChBorder, because a panel is one
			// two-tone ring and DrawFrame's four-colour overload is exactly that.
			DrawFrame(w, a, r.top, b - a, r.Height(),
			          SColorShadow(), SColorShadow(), SColorLight(), SColorLight());
		a = b;
	}
}

void Explorer::UpdateStatus()
{
	ExplorerModel& m = ActiveModel();
	Vector<String> cells = m.StatusCells();
	// The shadowing note describes the listing, so it belongs to the left cell
	// with the count and not to the free space on the right.
	if(m.GetShadowingMount())
		cells[0] << "   [shadowed by " << m.GetShadowingMount()->path.AsSysPath("/") << "]";
	// Two cells, sized to their text the way 98 sizes them: the left one takes
	// what is left of the strip and the right one is exactly as wide as its own
	// text, pinned to the right edge. InfoCtrl spells the second as a *negative*
	// width -- `r.right = max(r.left, sz.cx + width)` -- and the first as a
	// positive one, so the order of these two calls is not a matter of taste:
	// a positive width on the left cell would eat the right cell whole, because
	// its right edge is clamped to the control's and the loop stops there.
	// The size grip is a frame of the status bar too, and it sits in the corner of
	// the last panel -- so the right-hand cell has to be that much wider than its
	// own text, or the text runs under the grip. SizeGrip is a FrameRight<Ctrl>,
	// whose width is public, so ask the grip instead of repeating its DPI(14) here
	// and having the two drift apart the first time somebody resizes it.
	int grip = 0;
	if(status.GetFrameCount()) {
		if(SizeGrip *g = dynamic_cast<SizeGrip*>(&status.GetFrame(status.GetFrameCount() - 1)))
			grip = g->GetWidth();
	}
	int right = cells.GetCount() > 1
	            ? GetTextSize(cells[1], StdFont()).cx + ExplorerStatusCells::CELL_PAD + grip
	            : 0;
	if(right) {
		status.Set(0, cells[0], -right);
		status.Set(1, cells[1], right);
	}
	else
		status.Set(0, cells[0], INT_MAX);
}

void Explorer::UpdateHistoryButtons()
{
	// The history buttons are enabled from the model, so anything that moves the
	// history has to refresh them. Skipped before the frames exist: the first
	// OpenPath() runs inside the constructor, before AddFrame(toolbar).
	if(!frames_ready) return;
	UpdateBars();
	menu.Post(THISBACK(Menu));
}

void Explorer::RefreshNav()
{
	nav.Populate();
}

void Explorer::ToggleHidden()
{
	ExplorerModel& m = ActiveModel();
	m.SetShowHidden(!m.GetShowHidden());
	m.RefreshEntries();
	ActivePanel().Render(m);
	UpdateStatus();
	UpdateHistoryButtons();
}

// ---------------------------------------------------------------------------
// Operations
//
// Every one of these is a prompt, a call into ExplorerModel, and a re-read. The
// decision of *whether* something may happen belongs to the model and its policy;
// this layer only collects the intent and shows the outcome.
// ---------------------------------------------------------------------------

Vector<int> Explorer::SelectedRows()
{
	Vector<int> rows;
	const ExplorerPanel& p = ActivePanel();
	int n = p.Count();
	for(int i = 0; i < n; i++)
		if(p.IsSelected(i))
			rows.Add(i);
	// With nothing explicitly selected, the cursor row stands in: that is what a
	// single click means, and a menu command should act on it rather than refuse.
	if(!rows.GetCount()) {
		int cursor = p.Cursor();
		if(cursor >= 0 && cursor < n)
			rows.Add(cursor);
	}
	return rows;
}

Vector<String> Explorer::SelectedNames()
{
	Vector<String> names;
	const ExplorerPanel& p = ActivePanel();
	for(int row : SelectedRows()) {
		String name = p.NameAt(row);
		if(!name.IsEmpty())
			names.Add(name);
	}
	return names;
}

// A modal single-line text prompt.
//
// CtrlLib in this fork has no edit-ok-cancel dialog, and creating a folder,
// creating a file and renaming all need one. Prompt() renders rich text and
// returns a button index, so it cannot collect input; this is the small dialog
// those commands need instead of forcing them onto a menu that cannot ask.
struct NamePrompt : TopWindow {
	EditField edit;
	Pusher    ok, cancel;
	String    prompt_title;
	int       result = -1;

	static bool Ask(const char *title, const char *initial, String& out) {
		NamePrompt self;
		// Under the gui-user-gate the planner has named the action that opens
		// this dialog; the answer is the text the planner would have typed, and
		// a modal has no event loop for the planner to type it into. Normal runs
		// never have a scripted answer and run the modal as always.
		String scripted;
		if(ExplorerGate::TakeDialogAnswer(scripted)) {
			out = TrimLeft(TrimRight(scripted));
			return !out.IsEmpty();
		}
		self.prompt_title = title;
		self.edit.SetText(initial);
		self.Add(self.edit.HSizePos(8, 8).TopPos(8, self.edit.GetMinSize().cy));
		// Pusher, not Button: Button in this fork has no label setter at all, only
		// Pusher and ButtonOption do.
		self.ok.SetLabel(t_("OK"));
		self.ok.WhenPush << [&] { self.result = 1; self.Close(); };
		self.cancel.SetLabel(t_("Cancel"));
		self.cancel.WhenPush << [&] { self.result = -1; self.Close(); };
		self.Add(self.ok.HSizePos(8, 80).BottomPos(8));
		self.Add(self.cancel.HSizePos(80, 8).BottomPos(8));
		self.RunAppModal();
		if(self.result != 1)
			return false;
		// TrimLeft/TrimRight, never String::Trim: that overload *truncates* at the
		// given position, so trimming a name with it yields the empty string and the
		// dialog can never be accepted.
		out = TrimLeft(TrimRight(self.edit.GetText().ToString()));
		return !out.IsEmpty();
	}
};

bool Explorer::AskName(const char *title, const char *initial, String& value)
{
	String shown = t_(title);
	return NamePrompt::Ask(~shown, initial, value);
}

String Explorer::ModifyBlockReason()
{
	String reason;
	if(!ActiveModel().CanModify(reason))
		return reason;
	return Null;
}

void Explorer::AddBookmark()
{
	AddBookmark(ActiveModel().GetPath());
}

void Explorer::AddBookmark(const String& path)
{
	String error;
	if(!GetExplorerBookmarks().Add(path, error))
		PromptOK(DeQtf(t_("Cannot add the bookmark.\n\n") + error));
	else
		nav.PopulateBookmarks();
}

void Explorer::RemoveBookmark()
{
	// The sidebar selection wins over the file list: removing a bookmark while a
	// file happens to be highlighted in the other panel would delete the wrong thing
	// from the user's point of view, and the sidebar is where bookmarks are managed.
	String target = nav.SelectedBookmark();
	if(target.IsEmpty()) {
		PromptOK(DeQtf(t_("Select a bookmark in the sidebar first.")));
		return;
	}
	String error;
	if(!GetExplorerBookmarks().Remove(target, error))
		PromptOK(DeQtf(t_("Cannot remove the bookmark.\n\n") + error));
	else
		nav.PopulateBookmarks();
}

void Explorer::SelectAll()
{
	ActivePanel().SelectAll();
}

void Explorer::InvertSelection()
{
	ActivePanel().InvertSelection();
}

void Explorer::DeselectAll()
{
	ActivePanel().DeselectAll();
}

// Copy Names puts text on the *system* clipboard, and that is the whole decision
// behind it. The internal clipboard (clip_names) holds entries to be pasted as
// files, and putting names in it would make Paste create files called "notes.txt"
// in whatever directory the next paste happened in -- a name list pasted into a
// folder as though each line were a file to create. The system clipboard is
// text, so this is a text list, and pasting it into a terminal or an editor gives
// the names. Nothing about the internal clipboard is touched: Copy and Cut keep
// working exactly as they did.
void Explorer::CopyNames()
{
	Vector<String> names = SelectedNames();
	if(names.IsEmpty()) return;
	// One per line, joined rather than appended, so there is no trailing newline to
	// paste into the middle of the next line.
	WriteClipboardText(Join(names, "\n"));
	// Said in the status bar, because a command that puts something on the system
	// clipboard and shows nothing at all looks like a command that did nothing. One
	// key rather than a count woven into a sentence: a translation reorders a
	// sentence, and a number placed between two halves of one is where it gets lost.
	String said = t_("Copied names to the system clipboard");
	said << ' ' << names.GetCount();
	status.Set(said);
	EINFO(ExplorerChannels::OPS, "copied " << FormatInt((int64)names.GetCount())
	     << " names to the system clipboard");
}

// Switching the listing's view mode is a model change, not a widget one: the mode is
// per tab and belongs with the sort order, and the headless twin reads the same
// state. The panel re-renders because it fills both lists and shows one of them.
void Explorer::SetViewMode(ViewMode mode)
{
	ExplorerModel& m = ActiveModel();
	if(m.GetViewMode() == mode) return;
	m.SetViewMode(mode);
	ActivePanel().Render(m);
	UpdateStatus();
}

// One step of the zoom levels in ExplorerModel, not a percentage: a picture is
// drawn at the size it is used, so a 27px icon would have to be a scaled copy of a
// 32px one. SetZoom re-renders because the cell size, and therefore the number of
// columns and the whole arrangement, changes with the picture.
void Explorer::SetZoom(int pixels)
{
	ExplorerModel& m = ActiveModel();
	int z = ViewZoomClamp(pixels);
	if(z == m.GetViewZoom()) return;
	m.SetViewZoom(z);
	ActivePanel().Render(m);
	// The panel's column count is a division by the panel's own width, so the
	// toolbar and the menu have to be rebuilt to show the new enable state.
	UpdateHistoryButtons();
	UpdateStatus();
}

void Explorer::ZoomIn()
{
	SetZoom(ActiveModel().GetViewZoom() * 2);
}

void Explorer::ZoomOut()
{
	SetZoom(ActiveModel().GetViewZoom() / 2);
}

void Explorer::SetSort(ExplorerModel::SortBy by)
{
	ExplorerModel& m = ActiveModel();
	// Re-picking the key that is already active flips the direction, which is what a
	// second click on the same column header is expected to do.
	m.SetSort(by, m.GetSortBy() == by && m.GetSortDir() == ExplorerModel::ASCENDING
	                  ? ExplorerModel::DESCENDING
	                  : ExplorerModel::ASCENDING);
	ActivePanel().Render(m);
	ActivePanel().SyncLocation(m.GetPath(), m.GetFilter());
	UpdateStatus();
}

void Explorer::FocusLocationBar()
{
	ActivePanel().location.FocusPath(ActiveModel().GetPath());
}

// Properties dialog.
//
// A GridCtrl because the whole point is a readable two-column table, and a VFS
// that cannot report a size has to be able to say "unknown" in the same place a
// known value goes. Every field here comes from ExplorerEntry; nothing is
// re-derived by poking at the host filesystem, so the dialog tells the truth about
// an AST-backed mount instead of showing host values that do not apply.
struct PropertiesPrompt : TopWindow {
	Pusher close;

	// A GridCtrl because the whole point is a readable two-column table. The rows
	// come from EntryProperties() in the model, which is also what the headless
	// twin's --props prints, so the dialog and the script can never disagree.
	static void Show(const ExplorerEntry& e, const String& parent, const String& mount,
	                 bool can_modify) {
		PropertiesPrompt self;
		GridCtrl g;

		Vector<ExplorerEntry::PropertyRow> rows = EntryProperties(e, parent, mount, can_modify);

		g.AddColumn(t_("Property"), 170);
		g.AddColumn(t_("Value"), 330);
		for(int i = 0; i < rows.GetCount(); i++) {
			// Set(row, column, value). The two-argument Set(name, value) is "set the
			// column called name" -- it takes a *column* name, and handing it a
			// property label is an assertion about a column that does not exist.
			g.AddRow();
			g.Set(i, 0, rows[i].label);
			g.Set(i, 1, rows[i].value);
		}
		int r = rows.GetCount();

		self.Add(g.HSizePos(8, 8).VSizePos(8, r * 20 + 8));
		self.close.SetLabel(t_("Close"));
		self.close.WhenPush << [&] { self.Close(); };
		self.Add(self.close.HSizePos(70, 8).BottomPos(8));

		self.SetRect(240, 200, 8 + 170 + 330 + 8, 8 + r * 20 + 8 + 24 + 8 + 20);
		self.RunAppModal();
	}
};

void Explorer::ShowProperties()
{
	Vector<String> names = SelectedNames();
	if(names.GetCount() != 1) {
		// Properties of several unrelated entries is a column-per-entry layout, not
		// a longer list. One entry at a time keeps the table honest.
		if(names.GetCount() > 1)
			PromptOK(DeQtf(t_("Select a single entry to see its properties.")));
		return;
	}
	ExplorerModel& m = ActiveModel();
	for(const ExplorerEntry& e : m.Entries())
		if(e.name == names[0]) {
			String mount = m.GetMount() ? m.GetMount()->path.AsSysPath("/")
			                            : String(t_("none"));
			String reason;
			bool can_modify = m.CanModify(reason);
			PropertiesPrompt::Show(e, m.GetPath(), mount, can_modify);
			return;
		}
	PromptOK(DeQtf(t_("That entry is no longer in the listing.")));
}

// Help > Keyboard shortcuts.
//
// The rows are read out of the live MenuBar, not out of a table written next to
// the window: a hand-written list of shortcuts is a second source of truth, and it
// starts lying the first time an item gets a key and the table does not -- and
// nothing would notice, because a shortcut nobody can check is exactly the thing a
// user cannot check either. The menu is the only place a key is ever written down,
// so the menu is where it is read from.
//
// AutomationBar::Item::Key used to drop the accelerator on the floor, which is
// why the list is built from GuiAutomationVisitor rather than by walking
// MenuItemBase by hand: the visitor is already the thing that walks the live bar
// with the submenu procedures filled in, and the code that fills them is the code
// the menu itself runs.
struct ShortcutRow : Moveable<ShortcutRow> {
	String menu_path;
	String command;
	String keys;
};

// Read out of a walk that has already been made, so the dump and the window cannot
// disagree about what the menu contains -- not even about which walk it was.
static Vector<ShortcutRow> ReadShortcuts(GuiAutomationVisitor& vis)
{
	Vector<ShortcutRow> rows;
	for(const AutomationElement& el : vis.elements) {
		if(!el.accel) continue;
		ShortcutRow row;
		row.menu_path = el.path;
		row.command = el.text;
		row.keys = GetKeyDesc(el.accel);
		rows.Add(row);
	}
	return rows;
}

struct ShortcutsPrompt : TopWindow {
	Pusher close;

	static void Show(const Vector<ShortcutRow>& rows) {
		ShortcutsPrompt self;
		GridCtrl g;

		g.AddColumn(t_("Command"), 230);
		g.AddColumn(t_("Shortcut"), 100);
		for(int i = 0; i < rows.GetCount(); i++) {
			g.AddRow();
			g.Set(i, 0, rows[i].menu_path);
			g.Set(i, 1, rows[i].keys);
		}
		int r = rows.GetCount();
		// Capped rather than sized to the rows: a list of shortcuts is as long as
		// the menu happens to be, and a window taller than the screen is a window
		// whose last rows -- the ones a user added a shortcut for -- cannot be
		// reached. GridCtrl scrolls itself once the view is shorter than the rows.
		int body = min(r * 20 + 8, 400);

		// VSizePos margins, not positions: the bottom margin clears the Close row
		// (8 + button + 8), and the grid's own height is whatever the window has
		// left -- which is why the window is sized from `body` below. Passing the
		// height here shrank the grid to a 50px slit under every row count.
		self.Add(g.HSizePos(8, 8).VSizePos(8, 36));
		self.close.SetLabel(t_("Close"));
		self.close.WhenPush << [&] { self.Close(); };
		self.Add(self.close.HSizePos(70, 8).BottomPos(8));

		self.SetTitle(t_("Keyboard shortcuts"));
		self.SetRect(240, 200, 8 + 230 + 100 + 8, 8 + body + 36);
		self.RunAppModal();
	}
};

// The manual, one scrolled page: what the window actually has, in the order a
// new user meets it. QTF through and through (the rich-text rule: SetQTF, never
// HTML), every paragraph through Explorer.t in its own key, so a translation
// carries its own emphasis. F1 and Help > Contents both land here.
struct ExplorerHelp : TopWindow {
	RichTextView view;

	ExplorerHelp()
	{
		Title(t_("Explorer help"));
		Sizeable().Zoomable();
		SetRect(0, 0, 660, 560);
		Add(view.SizePos());
		// QTF here is single-star bold (*text*) and one \n per paragraph --
		// the parser's own toggles: a double star is two toggles around an
		// empty span, and a double \n is an empty paragraph between.
		String qtf;
		qtf << t_("*Explorer*\n"
		          "A file manager over MountManager: what the panels show is what the mounted "
		          "filesystems say, and mounting any VFS under any path replaces that view.");
		qtf << "\n\n" << t_("*The toolbars*\n\n"
		           "Three groups of buttons (navigation, tools, view) share one row and wrap when the "
		           "window is narrow; the location row (path buttons, field, filter) sits under them. "
		           "Every group is switchable in View > Toolbars, and \"One row per group\" there gives "
		           "each group a row of its own.");
		qtf << "\n\n" << t_("*The location row*\n\n"
		           "One click on a path component goes there. The pencil button (or Ctrl+L) turns the "
		           "row into a field: type a path and press Enter. A path that does not exist is "
		           "reported and the row goes back to where you were. The field at the right filters "
		           "the listing as you type. Ctrl+D bookmarks the current directory.");
		qtf << "\n\n" << t_("*Tabs and panels*\n\n"
		           "Ctrl+T opens a tab, Ctrl+W closes the active one, and Remove all tabs keeps one. "
		           "Two listing panels: Two panels side by side or Two panels stacked turns the split "
		           "on, the same button again turns it off, and the other button flips the direction. "
		           "Ctrl+Tab switches the active pane; Synchronize panels makes a navigation in one "
		           "pane show the same directory in the other. Back never follows between panes: "
		           "each keeps its own history.");
		qtf << "\n\n" << t_("*The sidebar*\n\n"
		           "The directory tree follows the listing; Places, Mounts and Bookmarks sit under "
		           "it. Five layouts, Alt+6 through Alt+0: the tree alone (the default), places only, "
		           "tree and places, places and bookmarks, and everything. The dividers remember "
		           "where you left them.");
		qtf << "\n\n" << t_("*Selection and the two clipboards*\n\n"
		           "Ctrl+A selects all, Ctrl+I flips the selection. Copy names puts the selected "
		           "names on the clipboard as text; Copy and Cut put the files there, and Paste then "
		           "copies or moves them into the browsed directory. Names are text; files are "
		           "files. That is the one thing to get right.");
		qtf << "\n\n" << t_("*Delete is permanent*\n\n"
		           "Delete removes the selected entries immediately, without a trash bin, and asks "
		           "once. There is no undo.");
		qtf << "\n\n" << t_("*Sorting*\n\n"
		           "Alt+1 through Alt+5 sort by name, extension, size, time and type; the same key "
		           "again flips the direction. Folders come first, and Ignore case orders names "
		           "letter by letter instead of case by case.");
		qtf << "\n\n" << t_("*The headless twin*\n\n"
		           "The same program runs without a window: Explorer takes a directory and flags "
		           "like --lang, --props, --places, --mounts and --demo-overlay. That is developer "
		           "documentation: the full flag list lives in the package AGENTS "
		           "(uppsrc/Explorer/AGENTS.md).");
		qtf << "\n\n" << t_("*Help*\n\n"
		           "F1 opens this page; Keyboard shortcuts lists every key the window carries.");
		view.SetQTF(qtf);
	}

	bool Key(dword key, int cnt) override
	{
		if(key == K_ESCAPE) {
			Close();
			return true;
		}
		return TopWindow::Key(key, cnt);
	}
};

void Explorer::ShowHelp()
{
	static ExplorerHelp *help;
	if(!help) {
		help = new ExplorerHelp;
		help->OpenMain();
	}
	else
		help->SetFocus();
}

void Explorer::ShowSettings()
{
	ExplorerSettings settings = ExplorerSettings::Load();
	ExplorerSettings original = settings; // For cancel
	
	TopWindow dlg;
	dlg.SetTitle(t_("Settings"));
	dlg.Sizeable(false);
	
	// Tab control for categories
	TabCtrl tabs;
	
	// --- General tab ---
	ParentCtrl general_tab;
	Option opt_single_click;
	opt_single_click.SetLabel(t_("Single-click to open items"));
	opt_single_click.Set(settings.activate_policy == ExplorerSettings::SINGLE_CLICK);
	
	Option opt_show_hidden;
	opt_show_hidden.SetLabel(t_("Show hidden files by default"));
	opt_show_hidden.Set(settings.show_hidden_by_default);
	
	Option opt_new_tab_same;
	opt_new_tab_same.SetLabel(t_("New tab opens current directory"));
	opt_new_tab_same.Set(settings.new_tab_same_dir);
	
	general_tab.Add(opt_single_click.HSizePos(16, 16).TopPos(16, 24));
	general_tab.Add(opt_show_hidden.HSizePos(16, 16).TopPos(48, 24));
	general_tab.Add(opt_new_tab_same.HSizePos(16, 16).TopPos(80, 24));
	
	// --- View tab ---
	ParentCtrl view_tab;
	Option opt_compact_toolbar;
	opt_compact_toolbar.SetLabel(t_("Compact toolbar (groups share rows)"));
	opt_compact_toolbar.Set(settings.compact_toolbar);
	
	Option opt_dirs_first;
	opt_dirs_first.SetLabel(t_("Folders first in listings"));
	opt_dirs_first.Set(settings.default_dirs_first);
	
	Option opt_ignore_case;
	opt_ignore_case.SetLabel(t_("Ignore case when sorting"));
	opt_ignore_case.Set(settings.default_ignore_case);
	
	Label zoom_label;
	zoom_label.SetLabel(t_("Default icon size:"));
	EditInt zoom_edit;
	zoom_edit.SetData(settings.default_icon_zoom);
	zoom_edit.MinMax(16, 128);
	
	view_tab.Add(opt_compact_toolbar.HSizePos(16, 16).TopPos(16, 24));
	view_tab.Add(opt_dirs_first.HSizePos(16, 16).TopPos(48, 24));
	view_tab.Add(opt_ignore_case.HSizePos(16, 16).TopPos(80, 24));
	view_tab.Add(zoom_label.HSizePos(16, 16).TopPos(120, 24));
	view_tab.Add(zoom_edit.HSizePos(180, 16).TopPos(120, 24));
	
	// --- Toolbars tab ---
	ParentCtrl toolbar_tab;
	Option opt_nav_bar;
	opt_nav_bar.SetLabel(t_("Navigation bar"));
	opt_nav_bar.Set(settings.show_nav_bar);
	Option opt_tool_bar;
	opt_tool_bar.SetLabel(t_("Tools bar"));
	opt_tool_bar.Set(settings.show_tool_bar);
	Option opt_view_bar;
	opt_view_bar.SetLabel(t_("View bar"));
	opt_view_bar.Set(settings.show_view_bar);
	Option opt_loc_bar;
	opt_loc_bar.SetLabel(t_("Location bar"));
	opt_loc_bar.Set(settings.show_location_bar);
	
	toolbar_tab.Add(opt_nav_bar.HSizePos(16, 16).TopPos(16, 24));
	toolbar_tab.Add(opt_tool_bar.HSizePos(16, 16).TopPos(48, 24));
	toolbar_tab.Add(opt_view_bar.HSizePos(16, 16).TopPos(80, 24));
	toolbar_tab.Add(opt_loc_bar.HSizePos(16, 16).TopPos(112, 24));
	
	// --- Split tab ---
	ParentCtrl split_tab;
	Option opt_split_horiz;
	opt_split_horiz.SetLabel(t_("Horizontal split (side-by-side)"));
	opt_split_horiz.Set(settings.split_horizontal);
	Option opt_sync_panes;
	opt_sync_panes.SetLabel(t_("Synchronize panes by default"));
	opt_sync_panes.Set(settings.sync_panes);
	
	split_tab.Add(opt_split_horiz.HSizePos(16, 16).TopPos(16, 24));
	split_tab.Add(opt_sync_panes.HSizePos(16, 16).TopPos(48, 24));
	
	tabs.Add(general_tab.SizePos(), t_("General"));
	tabs.Add(view_tab.SizePos(), t_("View"));
	tabs.Add(toolbar_tab.SizePos(), t_("Toolbars"));
	tabs.Add(split_tab.SizePos(), t_("Split"));
	
	// Buttons
	Button ok, cancel;
	ok.SetLabel(t_("OK"));
	cancel.SetLabel(t_("Cancel"));
	
	dlg.Add(tabs.HSizePos(8, 8).VSizePos(8, 48));
	dlg.Add(ok.HSizePos(8, 90).BottomPos(8, 32));
	dlg.Add(cancel.HSizePos(90, 8).BottomPos(8, 32));
	
	ok.WhenPush << [&] {
		// Apply settings
		settings.activate_policy = opt_single_click.Get() ? ExplorerSettings::SINGLE_CLICK : ExplorerSettings::DOUBLE_CLICK;
		settings.show_hidden_by_default = opt_show_hidden.Get();
		settings.new_tab_same_dir = opt_new_tab_same.Get();
		settings.compact_toolbar = opt_compact_toolbar.Get();
		settings.default_dirs_first = opt_dirs_first.Get();
		settings.default_ignore_case = opt_ignore_case.Get();
		settings.default_icon_zoom = zoom_edit.GetData();
		settings.show_nav_bar = opt_nav_bar.Get();
		settings.show_tool_bar = opt_tool_bar.Get();
		settings.show_view_bar = opt_view_bar.Get();
		settings.show_location_bar = opt_loc_bar.Get();
		settings.split_horizontal = opt_split_horiz.Get();
		settings.sync_panes = opt_sync_panes.Get();
		
		settings.Save();
		
		// Apply immediately to current window
		ExplorerModel& m = ActiveModel();
		m.SetActivatePolicy(settings.activate_policy == ExplorerSettings::SINGLE_CLICK
			? ExplorerModel::SINGLE_CLICK : ExplorerModel::DOUBLE_CLICK);
		m.SetDirsFirst(settings.default_dirs_first);
		m.SetIgnoreCase(settings.default_ignore_case);
		m.SetViewZoom(settings.default_icon_zoom);
		
		// Apply toolbar visibility
		bars_shown[0] = settings.show_nav_bar;
		bars_shown[1] = settings.show_tool_bar;
		bars_shown[2] = settings.show_view_bar;
		bars_shown[3] = settings.show_location_bar;
		for(int i = 0; i < main_pane.panels.GetCount(); i++)
			main_pane.panels[i]->SetLocationShown(bars_shown[3]);
		if(split_pane.panels.GetCount() > 0)
			split_pane.panels[0]->SetLocationShown(bars_shown[3]);
		
		// Apply split settings
		split_dir = settings.split_horizontal;
		sync_panels = settings.sync_panes;
		
		UpdateBars();
		OpenPath(ActiveModel().GetPath());
		
		dlg.Close();
	};
	
	cancel.WhenPush << [&] {
		// Restore original settings
		dlg.Close();
	};
	
	dlg.Sizeable(false);
	dlg.SetRect(0, 0, 500, 450);
	dlg.CenterScreen();
	dlg.RunAppModal();
}

void Explorer::ShowShortcuts()
{
	GuiAutomationVisitor vis;
	vis.Read(menu);
	Vector<ShortcutRow> rows = ReadShortcuts(vis);
	if(rows.IsEmpty()) {
		// Not a placeholder branch: a menu with no shortcuts at all would mean the
		// accelerator never reached the automation walk, and an empty window would
		// look like an answer rather than like that.
		PromptOK(DeQtf(t_("No menu item has a keyboard shortcut.")));
		return;
	}
	ShortcutsPrompt::Show(rows);
}

// File > Create symlink: creates a symbolic link to the selected entry.
void Explorer::CreateSymlink()
	{
		// The symlink verb needs a selected entry to link to, and there is nothing to
		// create without a destination.
		if(SelectedNames().IsEmpty()) return;
		String name = SelectedNames()[0];
		// Ask for the target of the symlink: a name prompt that asks for a path
		// relative to the current directory, with the selected entry as the default.
		String target;
		if(!AskName("Create symlink to", name, target)) return;
		String error;
		if(!ActiveModel().CreateSymlink(name, target, error)) {
			PromptOK(DeQtf(t_("Cannot create symlink: ") + error));
		}
	}

// Tools > Empty Trashbin: empties the trash bin permanently.
void Explorer::EmptyTrashbin()
	{
		// The trashbin is a directory on the filesystem; emptying it requires
		// confirming with the user before the deletion.
		String error;
		if(!ActiveModel().EmptyTrashbin(error)) {
			PromptOK(DeQtf(t_("Cannot empty trashbin: ") + error));
		}
	}

void Explorer::ToggleSortDir()
{
	ExplorerModel& m = ActiveModel();
	m.ToggleSortDir();
	ActivePanel().Render(m);
	UpdateStatus();
}

// The two sort toggles. Both go through the model, because the model is what the
// twin reads and what the next navigation re-applies; a flag kept in the window
// would be lost the moment the tab was re-read, and would not exist at all in the
// headless run that has to be able to check the order.
void Explorer::ToggleDirsFirst()
{
	ExplorerModel& m = ActiveModel();
	m.SetDirsFirst(!m.GetDirsFirst());
	ActivePanel().Render(m);
	UpdateStatus();
}

void Explorer::ToggleIgnoreCase()
{
	ExplorerModel& m = ActiveModel();
	m.SetIgnoreCase(!m.GetIgnoreCase());
	ActivePanel().Render(m);
	UpdateStatus();
}

bool Explorer::Key(dword key, int count)
{
	// Delete goes to the model, which decides whether policy allows it. Returning
	// false lets the default handling run for anything not claimed here.
	if(key == K_DELETE && ModifyBlockReason().IsEmpty()) {
		DeleteSelected();
		return true;
	}
	if(key == K_F2 && ModifyBlockReason().IsEmpty()) {
		// In place in the detailed listing, because a name with a column of
		// values next to it is edited where it is; the dialog stays for the icon
		// views, where there is no row to write on.
		if(!ActivePanel().IsIconMode() && ActivePanel().Cursor() >= 0)
			ActivePanel().columns.BeginRename();
		else
			RenameSelected();
		return true;
	}
	if(key == K_CTRL | 'A') { SelectAll(); return true; }
	if(key == K_CTRL | K_SHIFT | 'A') { DeselectAll(); return true; }
	if(key == K_CTRL | 'I') { InvertSelection(); return true; }
	// Copy, Cut and Paste are also keys on the Edit menu items, and a menu
	// accelerator is only reached when the focused control forwards the key it did
	// not use to the menu bar (ColumnList does, a tree does not). Claiming them here
	// as well is what makes them work with the focus anywhere; it cannot fire the
	// verb twice, because the key stops at whichever of the two handles it first.
	if(key == K_CTRL | 'C') { CopySelected(false); return true; }
	if(key == K_CTRL | 'X') { CopySelected(true);  return true; }
	if(key == K_CTRL | 'V') { PasteClipboard();    return true; }
	// Ctrl+T opens a new tab in the pane the window is acting on. It has to be
	// claimed here, before the list controls get it: the detailed list's own
	// key handler sees a bare Ctrl+T as "select all" because it masks the
	// modifiers differently, and a shortcut that silently does the wrong
	// thing is worse than one that does nothing.
	if(key == K_CTRL | 'T') { NewTab(ActiveModel().GetPath()); return true; }
	// Ctrl+W closes the focused pane's active tab. The last remaining tab
	// closes the window instead -- CloseTab does that -- so this key is
	// never a way out of a window with one tab.
	if(key == K_CTRL | 'W') { CloseTab(ActiveTab()); return true; }
	// Ctrl+Tab cycles tabs in the focused pane, Ctrl+Shift+Tab the other way.
	// Pane switching is a separate verb (Ctrl+Tab used to do that here, and
	// the two are not the same thing): a tab cycle moves within the pane
	// the window is acting on, and a pane switch changes which pane that is.
	if(key == K_CTRL_TAB) {
		ExplorerPane& p = (pane == 1) ? split_pane : main_pane;
		if(p.GetTabCount() > 1) {
			int next = p.ActiveTab() + 1;
			if(next >= p.GetTabCount()) next = 0;
			p.tabs.Set(next);
			p.RenderActive();
			p.SyncLocationActive();
			p.FocusListActive();
			UpdateStatus();
			UpdateHistoryButtons();
		}
		return true;
	}
	if(key == (K_CTRL | K_SHIFT | K_TAB)) {
		ExplorerPane& p = (pane == 1) ? split_pane : main_pane;
		if(p.GetTabCount() > 1) {
			int prev = p.ActiveTab() - 1;
			if(prev < 0) prev = p.GetTabCount() - 1;
			p.tabs.Set(prev);
			p.RenderActive();
			p.SyncLocationActive();
			p.FocusListActive();
			UpdateStatus();
			UpdateHistoryButtons();
		}
		return true;
	}
	// Pane switching is a distinct command from tab cycling, and it lives in
	// the View menu rather than on a key: a key that means "switch pane" and
	// a key that means "next tab" are two different verbs, and Ctrl+Tab was
	// doing both. The View bar's two direction items and the Left/Right pane
	// menus own the split now.
	// The listing modes. Same keys as the View menu, so a user who reaches for the
	// shortcut gets the same answer as a user who reaches for the menu item.
	if(key == (K_CTRL | '1')) { SetViewMode(VIEW_BIG_ICONS);   return true; }
	if(key == (K_CTRL | '2')) { SetViewMode(VIEW_SMALL_ICONS); return true; }
	if(key == (K_CTRL | '3')) { SetViewMode(VIEW_LIST);        return true; }
	if(key == K_F5) {
		OpenPath(ActiveModel().GetPath());
		return true;
	}
	return TopWindow::Key(key, count);
}

// The modal "Edit columns" dialog.
//
// Modal, and not the right-click menu, because this is the one that can be
// reasoned about: every column is a row with a check box, the set is applied at
// once when the dialog is accepted, and nothing changes while it is open. A menu
// that changes the listing with every click leaves no state to go back to.
struct ColumnsPrompt : TopWindow {
	//< One check box per column, in display order.
	Array<One<Option>> boxes;
	Array<ListColumn> which;
	Pusher           ok, cancel;
	int              result = 0;

	static bool Ask(ExplorerModel& model) {
		ColumnsPrompt self;
		Font f = StdFont();
		int row = f.GetHeight() + 8;
		for(int c = 0; c < LCOL_MAX; c++) {
			ListColumn col = (ListColumn)c;
			One<Option> o;
			o.Create();
			o->SetLabel(t_(ListColumnTitle(col)));
			o->Set(model.IsColumnVisible(col) ? 1 : 0);
			// A check box that cannot be unticked is worse than none: it says
			// "on" and then does nothing. The name column is shown greyed out
			// instead, and SetColumnVisible refuses to hide it.
			if(col == LCOL_NAME) o->Disable();
			self.Add(o->HSizePos(16, 16).TopPos(8 + c * row, row));
			self.boxes.Add(pick(o));
		}

		self.ok.SetLabel(t_("OK"));
		self.ok.WhenPush << [&] { self.result = 1; self.Close(); };
		self.cancel.SetLabel(t_("Cancel"));
		self.cancel.WhenPush << [&] { self.result = -1; self.Close(); };
		self.Add(self.ok.HSizePos(8, 90).BottomPos(8));
		self.Add(self.cancel.HSizePos(90, 8).BottomPos(8));

		self.SetRect(240, 200, 16 + 220 + 16, 8 + LCOL_MAX * row + 8 + 8 + 30);
		self.RunAppModal();
		if(self.result != 1)
			return false;
		// Applied together, after the dialog, not as each box is ticked: the
		// listing the user is looking at stays put while they decide.
		for(int i = 0; i < self.boxes.GetCount(); i++)
			model.SetColumnVisible((ListColumn)i, self.boxes[i]->Get() != 0);
		return true;
	}
};

void Explorer::WirePanel(ExplorerPanel& p)
{
	// The pane's own location row. The panel does not know which pane it is
	// (tabs re-index when one closes), so the owner resolves it the same way
	// WhenPicked does. A component button and a typed Enter both arrive as
	// WhenPathEnter; the parameter is the path the user asked for, so a button
	// can never navigate to the field's stale text.
	p.location.path_ctrl.WhenPath << [this, &p](const String& path) {
		p.WhenPathEnter(path);
	};
	p.location.WhenGo << [&p] { if(p.WhenPathEnter) p.WhenPathEnter(String()); };
	p.location.filter.WhenAction = [&p] { p.WhenFilterText(p.location.filter.GetText().ToString()); };
	p.location.Show(bars_shown[3]);
	// The pane's row navigates and filters that pane. The split pane is
	// recognized the way WhenPicked does; a tab panel is pane 0 and, being
	// visible only while its tab is active, is the pane the window acts on.
	p.WhenPathEnter << [this, &p](const String& path) {
		if(path.IsEmpty()) { GoToTypedPath(p); return; }
		ExplorerPane& target_pane = (&p == &split_pane.ActivePanel()) ? split_pane : main_pane;
		if(&target_pane == &split_pane) {
			NavigateTo(1, path);
			SyncChrome(split_pane.ActivePanel(), split_pane.ActiveModel());
		}
		else {
			NavigateTo(0, path);
			SyncChrome(p, ActiveModel());
		}
	};
	p.WhenFilterText << [this, &p](String text) {
		ApplyFilter(p, text);
	};
	p.WhenActivated << [this](int row) {
		// The panel says *which* row; the row is meaningless without the tab it
		// belongs to, so it is resolved here where the active tab is known.
		if(row == ActivePanel().Cursor())
			ActivateSelection();
	};
	// Ctrl+wheel zoom in icon mode
	p.WhenZoom << [this](int dir) {
		if(dir > 0) ZoomIn();
		else        ZoomOut();
	};
	// Context menus: selected entries vs background
	p.WhenEntryContextMenu << [this](Point screen_pos) {
		ShowEntryContextMenu(screen_pos);
	};
	p.WhenBackgroundContextMenu << [this](Point screen_pos) {
		ShowBackgroundContextMenu(screen_pos);
	};
	// A click on a heading sorts. A second click on the heading that is already
	// active flips the direction, which is what clicking the same column twice
	// means in every other list.
	p.columns.WhenSort << [this](int col) { SortByColumn(col); };
	p.columns.WhenColumnMenu << [this] { ShowColumnMenu(); };
	// A click in a panel is what makes it the pane a command acts on.
	p.WhenPicked << [this, &p] {
		pane = (&p == &split_pane.ActivePanel()) ? 1 : 0;
	};
	p.columns.WhenRename << [this](const String& name) {
		String error;
		if(!ActiveModel().Rename(ActivePanel().Cursor(), name, error))
			PromptOK(DeQtf(t_("Cannot rename.\n\n") + error));
		AfterOperation();
	};
}

void Explorer::SetPanelMode(ExplorerNav::Mode m)
{
	if(m == nav.GetMode()) return;
	nav.SetMode(m);
	// The tree is only built while it is on show, and showing it is what makes
	// it have rows: SyncTree() returns early on a hidden tree, so a mode that
	// brings the tree back needs the current path put in it again.
	nav.SyncTree(ActiveModel().GetPath());
	UpdateHistoryButtons();
}

void Explorer::SortByColumn(int visible_index)
{
	ExplorerModel& m = ActiveModel();
	Vector<ListColumn> shown = m.VisibleColumns();
	if(visible_index < 0 || visible_index >= shown.GetCount()) return;
	int key = ListColumnSortKey(shown[visible_index]);
	// Hidden and Mounted as are not orderings. Sorting by "hidden: yes first"
	// would be a rule nobody asked for, so the click does nothing and says so
	// through a disabled menu entry rather than by silently re-sorting.
	if(key < 0) return;
	SetSort((ExplorerModel::SortBy)key);
}

void Explorer::ShowColumnMenu()
{
	ExplorerModel& m = ActiveModel();
	// A local MenuBar, executed at the pointer: this menu is about the listing
	// under the pointer, and the window's own menu bar is the wrong owner for it.
	MenuBar bar;
	for(int c = 0; c < LCOL_MAX; c++) {
		ListColumn col = (ListColumn)c;
		bar.Add(t_(ListColumnTitle(col)), [this, col] {
			ExplorerModel& mm = ActiveModel();
			mm.SetColumnVisible(col, !mm.IsColumnVisible(col));
			ActivePanel().Render(mm);
			// The headings and the rows are both rebuilt, so the toolbar and menu
			// enable state has to be recomputed with them.
			UpdateHistoryButtons();
		})
		.Check(m.IsColumnVisible(col))
		.Enable(col != LCOL_NAME)
		.Tip(col == LCOL_NAME
			? t_("The name column is always shown")
			: t_("Show or hide this column"));
	}
	bar.Separator();
	bar.Add(t_("Edit columns..."), THISBACK(EditColumns));
	bar.Add(t_("Reset columns"), [this] {
		ActiveModel().ResetColumns();
		ActivePanel().Render(ActiveModel());
		UpdateHistoryButtons();
	});
	// Under the heading strip, in screen coordinates: a menu that appears at the
	// window's own origin is at least a long way from the thing it is about.
	// The heading strip owns the menu, and the menu opens under it: a menu at
	// the pointer is at least near the thing it is about.
	Rect hr = ActivePanel().columns.header.GetScreenRect();
	bar.Execute(&ActivePanel().columns.header, Point(hr.left + 8, hr.bottom + 2));
}

void Explorer::EditColumns()
{
	if(!ColumnsPrompt::Ask(ActiveModel())) return;
	ActivePanel().Render(ActiveModel());
	UpdateHistoryButtons();
}

void Explorer::ShowEntryContextMenu(Point screen_pos)
{
	ExplorerModel& m = ActiveModel();
	ExplorerPanel& p = ActivePanel();
	Vector<String> names = SelectedNames();
	bool single = names.GetCount() == 1;
	String name = single ? names[0] : String();
	bool can_rename = single;
	bool can_delete = names.GetCount() > 0;
	bool can_copy = names.GetCount() > 0;
	bool can_cut = names.GetCount() > 0 && !ModifyBlockReason().IsEmpty();
	bool can_paste = !clip_names.IsEmpty() && clip_path == m.GetPath();
	
	MenuBar bar;
	if(single) {
		bar.Add(t_("Open"), [this] { ActivateSelection(); });
		bar.Add(t_("Open with..."), [this] { /* TODO */ });
		bar.Separator();
	}
	bar.Add(t_("Copy"), [this] { CopySelected(false); })
		.Enable(can_copy);
	bar.Add(t_("Cut"), [this] { CopySelected(true); })
		.Enable(can_cut);
	bar.Add(t_("Paste"), [this] { PasteClipboard(); })
		.Enable(can_paste);
	bar.Separator();
	if(can_rename)
		bar.Add(t_("Rename"), [this] { RenameSelected(); });
	bar.Add(t_("Delete"), [this] { DeleteSelected(); })
		.Enable(can_delete);
	bar.Separator();
	bar.Add(t_("Properties"), [this] { ShowProperties(); });
	
	// Execute at screen position
	bar.Execute(this, screen_pos);
}

void Explorer::ShowBackgroundContextMenu(Point screen_pos)
{
	ExplorerModel& m = ActiveModel();
	bool can_paste = !clip_names.IsEmpty() && clip_path == m.GetPath();
	bool can_new_folder = !ModifyBlockReason().IsEmpty();
	bool can_new_file = !ModifyBlockReason().IsEmpty();
	
	MenuBar bar;
	bar.Add(t_("New Folder"), [this] { NewFolder(); })
		.Enable(can_new_folder);
	bar.Add(t_("New File"), [this] { NewFile(); })
		.Enable(can_new_file);
	bar.Separator();
	bar.Add(t_("Paste"), [this] { PasteClipboard(); })
		.Enable(can_paste);
	bar.Separator();
	bar.Add(t_("Properties"), [this] { ShowProperties(); });
	
	// Execute at screen position
	bar.Execute(this, screen_pos);
}

void Explorer::AfterOperation()
{
	ExplorerModel& m = ActiveModel();
	ActivePanel().Render(m);
	UpdateStatus();
	// The availability of every editing command depends on the mount and on what
	// is selected, both of which the operation just changed.
	UpdateBars();
	menu.Post(THISBACK(Menu));
}

void Explorer::NewFolder()
{
	String name;
	if(!AskName(t_("New folder"), t_("New folder"), name)) return;
	String error;
	if(!ActiveModel().CreateDir(name, error))
		PromptOK(DeQtf(t_("Cannot create the folder.\n\n") + error));
	AfterOperation();
}

void Explorer::NewFile()
{
	String name;
	if(!AskName(t_("New file"), t_("New file"), name)) return;
	String error;
	if(!ActiveModel().CreateFile(name, error))
		PromptOK(DeQtf(t_("Cannot create the file.\n\n") + error));
	AfterOperation();
}

void Explorer::RenameSelected()
{
	Vector<String> names = SelectedNames();
	if(names.GetCount() != 1) {
		// Renaming several entries at once would need a collision policy per entry
		// and a way to undo it. Offering it anyway is how half the batch ends up
		// named wrongly, so it is refused with an explanation instead.
		if(names.GetCount() > 1)
			PromptOK(DeQtf(t_("Select a single entry to rename.")));
		return;
	}
	String name;
	if(!AskName(t_("Rename"), names[0], name)) return;
	if(name == names[0]) return;
	String error;
	if(!ActiveModel().Rename(ActivePanel().Cursor(), name, error))
		PromptOK(DeQtf(t_("Cannot rename.\n\n") + error));
	AfterOperation();
}

void Explorer::DeleteSelected()
{
	Vector<String> names = SelectedNames();
	if(!names.GetCount()) return;

	// Ask about recursion once, and only when something selected can actually need
	// it. Asking "delete everything inside too?" about a single file trains people
	// to click through the prompt that matters.
	bool has_dir = false;
	for(const String& n : names) {
		for(const ExplorerEntry& e : ActiveModel().Entries())
			if(e.name == n && e.IsDir()) { has_dir = true; break; }
		if(has_dir) break;
	}
	// The gui-user-gate answers these prompts from its script, because a modal
	// has no event loop for the planner to answer them in; -1 means no script
	// and the user is asked as always.
	int scripted = ExplorerGate::ScriptedYesNo();
	bool recursive = false;
	if(has_dir) {
		String q = t_("Delete ") + FormatInt(names.GetCount()) + t_(" item(s)?");
		if(scripted < 0 ? PromptYesNo(DeQtf(q + t_("\n\nThis cannot be undone."))) != 1
		                : scripted != 1)
			return;
		String deep = t_("At least one folder is selected.\n\n"
		                 "Delete everything inside those folders too?");
		recursive = (scripted < 0 ? PromptYesNo(DeQtf(~deep)) : ExplorerGate::ScriptedYesNo()) == 1;
	}
	else if(scripted < 0 ? PromptYesNo(DeQtf(t_("Delete ") + FormatInt(names.GetCount())
	                       + t_(" item(s)?\n\nThis cannot be undone."))) != 1
	                     : scripted != 1)
		return;

	String error;
	if(!ActiveModel().Delete(names, recursive, error))
		PromptOK(DeQtf(t_("Cannot delete.\n\n") + error));
	AfterOperation();
}

void Explorer::TransferSelected(bool move)
{
	Vector<String> names = SelectedNames();
	if(!names.GetCount()) return;
	String dest;
	if(!AskName(move ? t_("Move to folder")
	                 : t_("Copy to folder"),
	                 move ? t_("Destination folder") : t_("Destination folder"), dest))
		return;

	// A bare name is a folder *inside* the current one; anything with a separator is
	// taken as a full path. Both are useful, and mixing them up silently is not.
	if(dest.Find('/') < 0 && dest.Find('\\') < 0)
		dest = ActiveModel().PathOf(dest);

	String error;
	bool ok = move ? ActiveModel().Move(names, dest, error)
	               : ActiveModel().Copy(names, dest, error);
	if(!ok)
		PromptOK(DeQtf((move ? t_("Cannot move.\n\n") : t_("Cannot copy.\n\n")) + error));
	AfterOperation();
}

void Explorer::ShowLog()
{
#ifdef flagDEBUG
	ExplorerLogWindow::Open(this);
#else
	LOG("the log window is only available in debug builds");
#endif
}

// Group one: getting somewhere and working on files.
//
// Every toolbar item needs an image. Bar::Add(text, callback) without one is a
// MenuBar-only overload: on a ToolBar it returns NilItem() and the button is
// silently dropped, which leaves an empty toolbar with no error anywhere.
void Explorer::NavBar(Bar& bar)
{
	bar.Add(t_("Back"), CtrlImg::go_back(), [this] {
		String error;
		if(ActiveModel().Back(error))
			NavigateTo(pane, ActiveModel().GetPath(), false);
	})
	// The history buttons are enabled from the model rather than always offered, so
	// the toolbar says whether Forward can go anywhere before it is pressed.
	.Enable(ActiveModel().CanBack())
	.Key(K_ALT | K_LEFT)
	.Tip(t_("Back to the previous location"));

	bar.Add(t_("Forward"), CtrlImg::go_forward(), [this] {
		String error;
		if(ActiveModel().Forward(error))
			NavigateTo(pane, ActiveModel().GetPath(), false);
	})
	.Enable(ActiveModel().CanForward())
	.Key(K_ALT | K_RIGHT)
	.Tip(t_("Forward to the next location"));

	bar.Add(t_("Up"), CtrlImg::DirUp(), [this] {
		String error;
		ActiveModel().Up(error);
		NavigateTo(pane, ActiveModel().GetPath(), false);
	})
	.Enable(ActiveModel().CanUp())
	.Key(K_ALT | K_UP)
	.Tip(t_("Up one level"));

	bar.Add(t_("Home"), CtrlImg::Home(), [this] {
		OpenPath(home_path);
	}).Tip(t_("The home directory"));

	bar.Add(t_("Refresh"), CtrlImg::redo(), [this] {
		OpenPath(ActiveModel().GetPath());
	}).Key(K_F5).Tip(t_("Re-read this directory"));

	bar.Separator();

	String blocked = ModifyBlockReason();
	bool have_selection = SelectedNames().GetCount() > 0;

	bar.Add(t_("New folder"), CtrlImg::MkDir(), THISBACK(NewFolder))
		.Enable(blocked.IsEmpty())
		.Tip(t_("Create a directory here"));
	bar.Add(t_("New file"), CtrlImg::new_doc(), THISBACK(NewFile))
		.Enable(blocked.IsEmpty())
		.Tip(t_("Create an empty file here"));
	bar.Add(t_("Rename"), CtrlImg::write(), THISBACK(RenameSelected))
		.Enable(blocked.IsEmpty() && have_selection)
		.Tip(t_("Rename the selected entry"));
	bar.Add(t_("Delete permanently"), CtrlImg::remove(), THISBACK(DeleteSelected))
		.Enable(blocked.IsEmpty() && have_selection)
		.Tip(t_("Remove the selected entries for good -- there is no trash to restore them from"));
	bar.Separator();
	bar.Add(t_("Copy"), CtrlImg::copy(), [this] { CopySelected(false); })
		.Enable(blocked.IsEmpty() && have_selection)
		.Tip(t_("Put the selected entries on the clipboard"));
	bar.Add(t_("Cut"), CtrlImg::cut(), [this] { CopySelected(true); })
		.Enable(blocked.IsEmpty() && have_selection)
		.Tip(t_("Put the selected entries on the clipboard to be moved"));
	bar.Add(t_("Paste"), CtrlImg::paste(), THISBACK(PasteClipboard))
		.Enable(blocked.IsEmpty() && !clip_names.IsEmpty())
		.Tip(t_("Paste what was copied or cut into this directory"));
	bar.Add(t_("Properties"), CtrlImg::information(), THISBACK(ShowProperties))
		.Enable(SelectedNames().GetCount() == 1)
		.Key(K_F9)
		.Tip(t_("What is known about the selected entry"));
	bar.Separator();

	bar.Add(t_("Hidden files"), CtrlImg::select_all(), THISBACK(ToggleHidden))
		.Check(ActiveModel().GetShowHidden())
		.Tip(t_("Show hidden files and directories"));
}

// Group two: the commands that act on the machine rather than on the listing.
//
// These are the ones that can leave this process, so each one reports what it
// could not do rather than appearing to have worked: a Run button that quietly
// fails to find an interpreter is worse than a button that says so.
void Explorer::ToolBar_(Bar& bar)
{
	bar.Add(t_("New window"), CtrlImg::new_doc(), THISBACK(NewWindow))
		.Tip(t_("Open another Explorer window on the same directory"));

#ifdef PLATFORM_POSIX
	// One command, not two: "ADMIN" and "ROOT" are the same user on different
	// systems, and the window does not know which name this machine uses.
	bar.Add(t_("Root window"), CtrlImg::information(), THISBACK(NewRootWindow))
		.Tip(t_("Open another window with more rights, if this system can"));
#endif

	bar.Add(t_("Run command"), CtrlImg::write(), THISBACK(RunCommand))
		.Key(K_F4)
		.Tip(t_("Run a command in this directory"));
	bar.Add(t_("Terminal"), CtrlImg::Computer(), THISBACK(RunTerminal))
		.Tip(t_("Open a terminal in this directory"));
	bar.Separator();

	bar.Add(t_("Find files"), CtrlImg::open(), THISBACK(FindFiles))
		.Tip(t_("Search this directory tree for names"));
	bar.Add(t_("Find dirs"), CtrlImg::open(), THISBACK(FindDirs))
		.Tip(t_("Search this directory tree for directories"));
	bar.Separator();

	bar.Add(t_("Attach"), CtrlImg::Add(), THISBACK(AttachPath))
		.Tip(t_("Make another directory visible here"));
	bar.Add(t_("Detach"), CtrlImg::remove(), THISBACK(DetachMount))
		.Enable(!SelectedNames().IsEmpty() || ActiveModel().GetMount() != nullptr)
		.Tip(t_("Remove a mount from the file system"));
}

// Group three: how the window is arranged. The view modes are here because they
// are the most switched of the commands on it, next to the button that changes
// the panel layout they belong to.
// The panel-mode glyphs, one per mode, borrowed at 16x16 because they are the only
// ones in CtrlLib that are. `Numbers1` would have been the obvious choice and it is
// a 40x6 strip of digits -- the same size is what a group of buttons is measured
// against, so an icon of the wrong size is a layout bug and not only a cosmetic one.
static Image PanelModeIcon(ExplorerNav::Mode mode)
{
	switch(mode) {
	case ExplorerNav::TREE_ONLY: return CtrlImg::DirSmall();
	case ExplorerNav::ONE:       return CtrlImg::menu_window();
	case ExplorerNav::TREE_ONE:  return CtrlImg::tab_wheel();
	case ExplorerNav::TWO:       return CtrlImg::Var();
	default:                     return CtrlImg::FileSmall();
	}
}

// The three listing modes. Big and small icons differ only in the number of rows
// and columns they draw, which is exactly what `Numbers1`/`Numbers2` would say --
// except that those are 40x6 and 60x8. `sort` for the detailed list, because a
// detailed list is a table and a table is what you sort.
static Image ViewModeIcon(ViewMode mode)
{
	switch(mode) {
	case VIEW_BIG_ICONS:   return CtrlImg::menu_check();
	case VIEW_SMALL_ICONS: return CtrlImg::FileSmall();
	default:               return CtrlImg::sort();
	}
}

void Explorer::ViewBar(Bar& bar)
{
	// Five modes, keyed Alt+6..0: the sort items own Alt+1..5, and one digit
	// row split between the two families beats two families fighting over the
	// same keys. Every key is always meaningful -- the modes are pane
	// visibility sets, and any set can be shown -- so there is no dead press
	// to define a fallback for.
	ExplorerNav::Mode modes[5] = { ExplorerNav::TREE_ONLY, ExplorerNav::ONE,
	                               ExplorerNav::TREE_ONE, ExplorerNav::TWO,
	                               ExplorerNav::TREE_TWO };
	for(int mi = 0; mi < 5; mi++) {
		ExplorerNav::Mode mode = modes[mi];
		bar.Add(t_(ExplorerNav::ModeName(mode)), PanelModeIcon(mode), [this, mode] {
			SetPanelMode(mode);
		})
		.Check(nav.GetMode() == mode)
		.Key(K_ALT | (mi < 4 ? '6' + mi : '0'))
		.Tip(String(t_("Sidebar layout: ")) + t_(ExplorerNav::ModeName(mode)));
	}
	bar.Separator();
	// "Panels" in these two names and their tips means the main area's listing
	// panes -- the sidebar modes above say "column", so the word cannot be read
	// as the sidebar any more. That collision is what the user's review caught:
	// "2 main panels mode is not working. It changes side-panel instead of main
	// area."
	bar.Add(t_("Two panels side by side"), CtrlImg::HorzSplit(), [this] { SetSplit(true); })
		.Check(split_on && split_dir)
		.Tip(t_("Show two listing panels side by side in the main area"));
	bar.Add(t_("Two panels stacked"), CtrlImg::HorzPos(), [this] { SetSplit(false); })
		.Check(split_on && !split_dir)
		.Tip(t_("Show two listing panels stacked in the main area"));
	bar.Separator();

	ViewMode views[3] = { VIEW_BIG_ICONS, VIEW_SMALL_ICONS, VIEW_LIST };
	const char *tips[3] = { t_("Big icons"), t_("Small icons"), t_("Detailed list") };
	for(int i = 0; i < 3; i++) {
		ViewMode v = views[i];
		bar.Add(tips[i], ViewModeIcon(v), [this, v] { SetViewMode(v); })
			.Check(ActiveModel().GetViewMode() == v)
			.Key(K_CTRL | ('1' + i));
	}
	bar.Separator();
	bar.Add(t_("Zoom in"), CtrlImg::Plus(), THISBACK(ZoomIn))
		.Enable(ActiveModel().GetViewMode() != VIEW_LIST
		        && ActiveModel().GetViewZoom() < VIEW_ZOOM_LARGEST);
	bar.Add(t_("Zoom out"), CtrlImg::Minus(), THISBACK(ZoomOut))
		.Enable(ActiveModel().GetViewMode() != VIEW_LIST
		        && ActiveModel().GetViewZoom() > VIEW_ZOOM_SMALLEST);
}

// ---------------------------------------------------------------------------
// The clipboard
// ---------------------------------------------------------------------------

// Copy and Cut do not move anything. They fill a clipboard, and Paste decides
// where the entries go -- which is what every file manager does, and the reason
// is that a copy has to be pasted somewhere and a dialog per copy asks the
// question at the wrong moment.
//
// The clipboard holds names and where they came from, not paths: a name pasted
// into another directory is the same entry, and pasting into the directory it was
// copied from is refused by the model rather than by this code.
void Explorer::CopySelected(bool move)
{
	Vector<String> names = SelectedNames();
	if(names.IsEmpty()) return;
	// A Vector's copy assignment is deleted in this fork; the clipboard is filled
	// element by element for the same reason.
	clip_names.Clear();
	for(const String& n : names)
		clip_names.Add(n);
	clip_path = ActiveModel().GetPath();
	clip_move = move;
	// Said plainly in the status bar, because a Copy that moves nothing on its
	// own looks like a command that did nothing.
	String s = move ? t_("Cut ") : t_("Copied ");
	s << names.GetCount() << (move ? t_(" entry(ies) for moving")
	                               : t_(" entry(ies) for pasting"));
	s << t_("   -- paste into another directory");
	status.Set(s);
	EINFO(ExplorerChannels::OPS, (move ? "cut " : "copy ")
	     << FormatInt((int64)names.GetCount()) << " entries from " << clip_path);
}

void Explorer::PasteClipboard()
{
	if(clip_names.IsEmpty()) {
		PromptOK(DeQtf(t_("The clipboard is empty.")));
		return;
	}
	String dest = ActiveModel().GetPath();
	// Asked, not assumed: pasting into the directory the entries came from is a
	// no-op the model would refuse anyway, and a paste that silently refuses is
	// worse than one that asked where.
	if(!AskName(t_("Paste into"), dest, dest)) return;

	String error;
	ExplorerModel& m = ActiveModel();
	bool ok = clip_move ? m.Move(clip_names, dest, error) : m.Copy(clip_names, dest, error);
	if(!ok) {
		PromptOK(DeQtf(t_("Cannot paste.\n\n") + error));
		return;
	}
	// The clipboard is emptied only after the paste worked. A failed paste that
	// also lost the entries would leave nothing to retry with.
	if(clip_move)
		clip_names.Clear();
	AfterOperation();
}

// Attaching another directory is a *mount*, not a copy: the host filesystem is
// mounted again at the current path, so both trees are browsable and neither is a
// copy of the other. That is what the mount registry is for, and it is why the
// deepest-mount resolution has to work: two host filesystems at two paths
// resolve to different relative paths for the same bytes.
bool Explorer::AttachDirectory(const String& at, const String& source, String& error)
{
	VfsPath src;
	src.Set(source);
	MountManager& mm = ActiveModel().GetMounts();
	if(!mm.IsBrowsable(src)) {
		// Asked of the registry rather than of the host: a path that no browsable
		// mount covers is not somewhere to attach, and the host's own idea of the
		// filesystem is not the question being asked.
		error = t_("No browsable filesystem covers ") + source;
		return false;
	}
	String type = mm.FindDeepest(src) ? mm.FindDeepest(src)->type : String();
	if(type.IsEmpty()) type = "system";
	if(!mm.MountOwned(at, new SystemFS, type)) {
		error = t_("the mount was refused: ") + mm.last_error;
		return false;
	}
	EINFO(ExplorerChannels::MODEL, "attached " << source << " at " << at);
	return true;
}

bool Explorer::DetachDirectory(const String& path, String& error)
{
	MountManager& mm = ActiveModel().GetMounts();
	if(!mm.Find(path)) {
		error = t_("nothing is mounted at ") + path;
		return false;
	}
	if(!mm.Unmount(path)) {
		error = mm.last_error;
		return false;
	}
	EINFO(ExplorerChannels::MODEL, "detached " << path);
	return true;
}

// ---------------------------------------------------------------------------
// The second toolbar group
// ---------------------------------------------------------------------------

// A second window on the same directory. A new process would need the path to
// this executable, which is not something a window can rely on knowing; a second
// TopWindow is the same thing without that assumption, and closing the first
// does not close the second.
void Explorer::NewWindow()
{
	Explorer *w = new Explorer(ActiveModel().GetPath());
	Rect r = GetScreenRect();
	w->SetRect(r + Rect(24, 24, 0, 0));
	// Not modal and not owned: a file manager that cannot open a second window
	// is one window with a menu item on it.
	w->Show();
}

// Run something and wait for it, with the working directory the listing is in.
//
// LocalProcess and not a shell-out helper: it takes the working directory as an
// argument, so the command cannot end up somewhere else because the window's
// current directory was never changed in the first place.
static bool RunAndWait(const String& cmd, const String& dir, String& error)
{
	LocalProcess p;
	if(!p.Start(~cmd, nullptr, ~dir)) {
		error = t_("cannot start: ") + cmd;
		return false;
	}
	// Its output goes to this process's terminal, which is where a window's
	// output can go. The exit code is what is reported: "it failed" without a
	// number says less than the number does.
	while(p.IsRunning())
		Sleep(20);
	return true;
}

// Started and forgotten: for the things that open a window of their own, which
// this window has no reason to wait for.
static bool RunDetached(const String& cmd, const String& dir)
{
	LocalProcess p;
	if(!p.Start(~cmd, nullptr, ~dir)) return false;
#ifdef PLATFORM_POSIX
	// DoubleFork so the terminal outlives this process: a terminal that dies
	// with the window it was started from is not a terminal.
	p.Detach();
#endif
	return true;
}

void Explorer::NewRootWindow()
{
#ifdef PLATFORM_POSIX
	// pkexec first, then sudo: which one exists is a property of the machine, not
	// of this window, and asking is the only honest way to find out.
	struct Helper { const char *name; const char *prefix; };
	static const Helper helpers[2] = { { "pkexec", "" }, { "sudo", "sudo -b " } };

	String exe = GetExeFilePath();
	if(exe.IsEmpty()) {
		PromptOK(DeQtf(t_("This program's own path is unknown, so it cannot be started again.")));
		return;
	}
	for(const Helper& h : helpers) {
		String probe;
		LocalProcess check;
		// `command -v` rather than a PATH search of our own: the shell's answer is
		// the one that matters, because it is the shell that would run it.
		if(!check.Start(~(String("sh -c 'command -v ") + h.name + " >/dev/null 2>&1'"))) continue;
		while(check.IsRunning()) Sleep(20);
		if(check.GetExitCode() != 0) continue;
		// Quoted with backslashes, not doubled quotes: the shell that runs this
		// line is the one LocalProcess hands it to, and " is what it understands.
		String cmd = String(h.prefix) + "\"" + exe + "\" \"" +
		             ActiveModel().GetPath() + "\"";
		if(RunDetached(cmd, ActiveModel().GetPath())) {
			EINFO(ExplorerChannels::OPS, "opened a root window with " << String(h.name));
			return;
		}
	}
	PromptOK(DeQtf(t_("This system has no pkexec or sudo, so a window with more rights cannot be opened from here.")));
#else
	PromptOK(DeQtf(t_("Windows has no separate root account, so there is no root window to open.")));
#endif
}

// A one-line prompt for a command, and then it runs.
void Explorer::RunCommand()
{
	String command;
	if(!AskName(t_("Run command"), "", command)) return;
	command = TrimLeft(TrimRight(command));
	if(command.IsEmpty()) return;
	EINFO(ExplorerChannels::OPS, "running " << command << " in " << ActiveModel().GetPath());
	String error;
	if(!RunAndWait(command, ActiveModel().GetPath(), error)) {
		PromptOK(DeQtf(error));
		return;
	}
}

void Explorer::RunTerminal()
{
#ifdef PLATFORM_POSIX
	// Which terminal exists is a property of the desktop, so it is looked for
	// rather than assumed. Reporting the failure is the point: a Terminal button
	// that does nothing when no terminal is installed is the whole complaint.
	static const char *terms[] = { "x-terminal-emulator", "gnome-terminal", "konsole",
	                               "xfce4-terminal", "xterm", nullptr };
	for(int i = 0; terms[i]; i++) {
		String cmd = String(terms[i]) + " --working-directory=\"" +
		             ActiveModel().GetPath() + "\"";
		if(RunDetached(cmd, ActiveModel().GetPath())) {
			EINFO(ExplorerChannels::OPS, "terminal " << String(terms[i])
		     << " in " << ActiveModel().GetPath());
			return;
		}
	}
	PromptOK(DeQtf(t_("No terminal program was found on this system.")));
#else
	PromptOK(DeQtf(t_("There is no terminal program to start from this window on Windows.")));
#endif
}

// Search the tree under the current directory.
//
// Through the model's mount rather than through the host filesystem: a search
// that walks /proc because the window happens to be looking at / would hang, and
// a search over an AST-backed mount has to walk the AST. Bounded in depth, in
// rows, and by a second if nothing turns up -- an unbounded walk of / is not a
// search, it is a hang with a progress bar.
static bool SearchNames(const String& root, const String& pattern, bool dirs,
                        int depth, Vector<String>& found, int limit)
{
	if(found.GetCount() >= limit || depth < 0) return true;
	Vector<ExplorerEntry> entries;
	{
		// One read per directory, and the listing already says which of these are
		// directories: a second lookup per name would be a second answer to a
		// question the first read has answered.
		ExplorerModel here;
		String error;
		if(!here.SetPath(root, error)) return false;
		// Element by element: a Vector's copy assignment is deleted in this fork,
		// and Entries() is a Vector the model owns.
		const Vector<ExplorerEntry>& listed = here.Entries();
		for(const ExplorerEntry& e : listed)
			entries.Add(e);
	}
	Sort(entries, [](const ExplorerEntry& a, const ExplorerEntry& b) { return a.name < b.name; });
	for(const ExplorerEntry& e : entries) {
		String full = root.EndsWith("/") ? root + e.name : root + '/' + e.name;
		if(dirs ? e.IsDir() : !e.IsDir())
			if(pattern == "*" || PatternMatch(~pattern, ~e.name))
				found.Add(full);
		if(e.IsDir())
			SearchNames(full, pattern, dirs, depth - 1, found, limit);
		if(found.GetCount() >= limit) return false;
	}
	return true;
}

void Explorer::FindNames(bool dirs)
{
	String pattern;
	if(!AskName(t_("Find name pattern"), "*", pattern)) return;
	pattern = TrimLeft(TrimRight(pattern));
	if(pattern.IsEmpty()) return;
	String root = ActiveModel().GetPath();
	Vector<String> found;
	// Six levels and five hundred rows: enough to find something, bounded so the
	// window cannot disappear into a walk of /.
	bool complete = SearchNames(root, pattern, dirs, 6, found, 500);

	String text;
	for(const String& p : found)
		text << p << '\n';
	if(found.IsEmpty()) {
		PromptOK(DeQtf(t_("Nothing found under ") + root + "."));
		return;
	}
	// The results in a dialog, in text the user can copy out of: a file manager
	// that navigated to the first hit would leave the rest of the search behind.
	// "Stopped at 500" is said out loud, because a truncated search that looks
	// complete is how a file gets declared missing.
	PromptOK(DeQtf(t_("Found ") + FormatInt((int64)found.GetCount()) + t_(" under ") +
	               root + (complete ? String() : String(t_(" (stopped at 500)"))) +
	               t_(":\n\n") + text));
}

void Explorer::AttachPath()
{
	String where;
	if(!AskName(t_("Attach this directory"), ActiveModel().GetPath(), where)) return;
	where = TrimLeft(TrimRight(where));
	if(where.IsEmpty()) return;
	// Mounted at the *current* path, so the attach shows up where the user is
	// looking instead of adding a mount nobody can find.
	String at = ActiveModel().GetPath();
	MountManager& mm = ActiveModel().GetMounts();
	if(mm.Find(at)) {
		PromptOK(DeQtf(t_("Something is already mounted at ") + at +
		               t_(". Unmount it first.")));
		return;
	}
	String error;
	if(!AttachDirectory(at, where, error)) {
		PromptOK(DeQtf(t_("Cannot attach.\n\n") + error));
		return;
	}
	OpenPath(at);
	RefreshNav();
}

void Explorer::DetachMount()
{
	// The selected entry if it is a mount point, otherwise the mount the listing
	// is on -- which is what "detach" means when nothing is selected: you cannot
	// detach a directory you are standing in by accident, only on purpose.
	String path;
	Vector<String> names = SelectedNames();
	if(names.GetCount() == 1) {
		String base = ActiveModel().GetPath();
		for(const ExplorerEntry& e : ActiveModel().Entries())
			if(e.name == names[0] && e.IsMount()) {
				path = base.EndsWith("/") ? base + e.name : base + '/' + e.name;
				break;
			}
	}
	if(path.IsEmpty() && ActiveModel().GetMount())
		path = ActiveModel().GetMount()->path.AsSysPath("/");
	if(path.IsEmpty()) {
		PromptOK(DeQtf(t_("Select a mount point, or be inside one, to detach it.")));
		return;
	}
	String error;
	if(!DetachDirectory(path, error)) {
		PromptOK(DeQtf(t_("Cannot detach.\n\n") + error));
		return;
	}
	RefreshNav();
	OpenPath("/");
}

void Explorer::RelayoutSoon()
{
	// Popup-menu verbs must not re-lay the window's frames inside the menu's
	// own event: on the GTK backend that silenced every later click, with no
	// error on any stream (the same class as the split's mid-loop teardown).
	// Posted with this as the id, so a window closed in between drops it.
	UPP::PostCallback([this] { RefreshLayout(); }, this);
}

void Explorer::CombinedBars(Bar& bar)
{
	// The three groups in xfe's order, each only when it is shown. "One row
	// per group" turns the gaps between the groups into line breaks; compact
	// lets the pane wrap items across rows wherever the width runs out, which
	// is the factory state the review asked for.
	bool first = true;
	auto gap = [&] {
		if(first) return;
		if(bars_rows_mode) bar.Break();
		else               bar.Separator();
	};
	first = true;
	if(bars_shown[0]) { gap(); NavBar(bar); }
	if(bars_shown[1]) { gap(); ToolBar_(bar); }
	if(bars_shown[2]) { gap(); ViewBar(bar); }
}

void Explorer::UpdateBars()
{
	// Only the bars that exist: an empty bar is zero tall, and posting to a bar
	// that is not shown would still measure it.
	if(!frames_ready) return;
	all_bars.Post(THISBACK(CombinedBars));
	// The pane re-wraps itself when the items change; the frame heights were
	// cached at AddFrame, so the new height needs one relayout -- posted,
	// because this verb is called from popup menus too and a relayout inside
	// the menu's own event took the GTK input path with it (see RelayoutSoon).
	RelayoutSoon();
}

// The toolbar's own configuration file, in the app config folder next to the
// bookmarks: which of the four groups are shown, and whether the groups stack
// one row per group or share the compact rows. Text on purpose -- the user
// should be able to read and fix it, and the bookmarks file set the precedent.
static String ToolbarConfPath()
{
	return AppendFileName(GetConfigFolder(), "toolbars.cfg");
}

void Explorer::SaveToolbarConf() const
{
	String s;
	for(int g = 0; g < 4; g++)
		s << "shown " << g << " " << (bars_shown[g] ? 1 : 0) << "\n";
	s << "rows " << (bars_rows_mode ? 1 : 0) << "\n";
	FileOut out(ToolbarConfPath());
	out.Put(s);
}

void Explorer::LoadToolbarConf()
{
	FileIn in(ToolbarConfPath());
	if(!in.IsOpen())
		return;
	while(!in.IsEof()) {
		Vector<String> t = Split(in.GetLine(), ' ');
		if(t.GetCount() < 2)
			continue;
		if(t[0] == "shown" && t.GetCount() >= 3) {
			int g = StrInt(t[1]);
			if(g >= 0 && g <= 3)
				bars_shown[g] = StrInt(t[2]) != 0;
		}
		else if(t[0] == "rows")
			bars_rows_mode = StrInt(t[1]) != 0;
	}
}

void Explorer::SetBarShown(int group, bool on)
{
	if(group < 0 || group > 3) return;
	bars_shown[group] = on;
	// The location row is the odd one out: it is a frame holding an edit field and
	// a filter, not a bar of buttons, so it is shown and hidden directly instead
	// of through the array of ToolBars. Showing it is a separate decision from
	// focusing it -- a visible location row is the only way to reach a directory
	// that is not in the sidebar, so hiding it is a choice, not a reset.
	// Both rows of the location group together: a breadcrumb with no field is a
	// place you can only leave, and a field with no breadcrumb is a path you have
	// to count the components of.
	if(group == 3) {
		// The location rows live in the panels now: one per pane, so the group
		// switch shows and hides every pane's row -- a pane without a row still
		// reaches any directory through the other pane's row or the sidebar,
		// but its own row is its own way to type a path.
		for(int i = 0; i < main_pane.panels.GetCount(); i++)
			main_pane.panels[i]->SetLocationShown(on);
		for(int i = 0; i < split_pane.panels.GetCount(); i++)
			split_pane.panels[i]->SetLocationShown(on);
	}
	else {
		// The group's items simply drop out of the one bar: the next refill,
		// posted by UpdateBars, fills the bar without them and the pane
		// re-wraps whatever remains.
	}
	// A hidden group takes no room, so the frames below it move up. The
	// relayout is *posted*: this verb runs inside a popup menu's dispatch, and
	// re-laying the window's frames within that same event took the GTK input
	// path with it -- the same lesson the split's dismissal taught, one layer
	// up. The menu closes, the loop drains, then the rectangles move.
	RelayoutSoon();
	if(frames_ready) UpdateBars();
	SaveToolbarConf();
}

void Explorer::Paint(Draw& w)
{
	// The divider position cannot be set before the splitter knows its own width:
	// Layout() clamps it away while the window is still unsized. Apply it on the
	// first paint, once the real width is known, and let the relayout repaint.
	if(!split_sized) {
		split_sized = true;
		split.SetMinPixels(0, NAV_WIDTH);
		split.SetPos(SplitUnits(NAV_WIDTH, split.GetSize().cx));
		split.Layout();
		Refresh();
	}

	TopWindow::Paint(w);

	// --dump-ui, once the window has been laid out at least once so the rectangles
	// below are the real ones. Closing is deferred to a timer: closing a window from
	// inside its own paint tears the paint down under the caller.
	if(ui_dump && !ui_dumped) {
		ui_dumped = true;
		StringStream ss, frame;
		// The language and a few strings through it. A translation table that is
		// loaded but not consulted looks exactly like one that is missing, and the
		// only difference between those two is what t_() returns -- so the check
		// has to be t_()'s own answer, not a count of entries.
		Cout() << "\nLanguage: "
		       << (ExplorerGetLanguage().IsEmpty() ? String("English (default)")
		                                          : ExplorerGetLanguage())
		       << "\n  t_(\"Bookmarks\") = " << t_("Bookmarks")
		       << "\n  t_(\"Name\")      = " << t_("Name")
		       << "\n  t_(\`x\`)        = " << t_("x");
		// The look, as numbers. The widget tree cannot show a colour, and a skin
		// that silently failed to install is otherwise indistinguishable from one
		// that installed and drew nothing: both are a flat window. The palette is
		// the cheapest part of the look to check and the one that decides whether
		// the other parts are even being asked for -- so it is the first line
		// after the language.
		Cout() << "\nSkin: " << ExplorerSkinProbe() << "\n";
		Dump(ss);
		Cout() << ss.GetResult();
		// Ctrl::Dump walks children, and a CtrlFrame is not one -- so the frames
		// it only lists by name never get to dump what is inside them. The address
		// bar is the one composite that matters here, and it is a frame.
		auto dump_location = [&](ExplorerPanel& p, const char *which) {
			Cout() << "\nLocation " << which << ": " << p.location.GetRect().ToString()
			       << " one row, path ctrl " << p.location.path_ctrl.GetRect().ToString()
			       << ", " << p.location.path_ctrl.GetButtonCount() << " component buttons:";
			for(int i = 0; i < p.location.path_ctrl.GetButtonCount(); i++)
				Cout() << " " << p.location.path_ctrl.GetButton(i).GetRect().ToString();
			Cout() << ", filter " << p.location.filter.GetRect().ToString() << "\n";
		};
		dump_location(ActivePanel(), pane == 1 ? "active split pane" : "active pane");
		if(split_on && split_pane.panels.GetCount() > 0 && pane == 0)
			dump_location(split_pane.ActivePanel(), "other pane");

		// Ctrl::Dump knows nothing about what a list *contains*, so the rows of
		// the sidebar and of the listing are printed here. They are the part of the
		// window this verification is actually about. ColumnList and ArrayCtrl are
		// separate hierarchies in this fork, so the two take different accessors.
		auto dump_rows = [](const char *title, ArrayCtrl& list) {
			Cout() << "\n" << title << " (" << list.GetCount() << " rows)\n";
			for(int i = 0; i < list.GetCount(); i++) {
				String row;
				for(int c = 0; c < list.GetColumnCount(); c++) {
					if(c) row << " | ";
					row << list.Get(i, c).ToString();
				}
				Cout() << "  " << (i == list.GetCursor() ? "> " : "  ") << row << "\n";
			}
		};
		// Both panels, and each panel's list that is on show. The three lists
		// behind a panel are all filled whatever is visible, so dumping all six
		// would be mostly rows nobody can see.
		dump_rows("Left panel", nav.left.Active());
		dump_rows("Right panel", nav.right.Active());
		// The four groups with their visibility and real rects. A toolbar that is
		// "shown" but 0x0 has been seen in this codebase: a frame that was added
		// and never parented still measures, and only the rect says which it is.
		Cout() << "\nToolbars: " << all_bars.GetRect().ToString()
		       << (bars_rows_mode ? ", one row per group" : ", compact")
		       << "\n  shown: " << (bars_shown[0] ? "nav" : "-") << " "
		       << (bars_shown[1] ? "tool" : "-") << " "
		       << (bars_shown[2] ? "view" : "-") << " "
		       << (bars_shown[3] ? "location" : "-")
		       << ", items " << all_bars.GetAutomationItemCount()
		       << ", one row is " << ToolBar::GetStdHeight() << "px, window "
		       << GetSize().cx << "x" << GetSize().cy << "\n";
		// The status strip's cells. They are the only geometry in the window that
		// this package draws itself rather than asking a control to, so nothing
		// else in the dump reports them -- and a panel drawn from a stale copy of
		// the cell widths is a panel around nothing, which looks exactly like a
		// panel around something until the window is resized.
		Cout() << "Status: " << status.GetRect().ToString()
		       << " panels painted " << status_cells.paints << " last "
		       << status_cells.last_paint.ToString() << " cells ";
		{
			int a = 2;
			for(int i = 0; i < status.GetTabCount(); i++) {
				int b = status.GetTabOffset(i);
				Cout() << (i ? ", " : "") << "[" << a << ", 0] - [" << b << ", "
				       << status.GetSize().cy << "] : (" << (b - a) << ")";
				a = b;
			}
		}
		Cout() << "\n";
		{
			// The one bar's buttons, by rect: the wrap rows are visible here as
			// jumps in the buttons' y, which is how a wrapped compact row is
			// told apart from a missing group.
			Cout() << "  " << all_bars.GetRect().ToString() << " min "
			       << all_bars.GetMinSize().ToString() << " buttons:";
			int n = all_bars.GetAutomationItemCount();
			for(int i = 0; i < n; i++) {
				const Ctrl *c = all_bars.GetAutomationItemCtrl(i);
				if(i && i % 8 == 0) Cout() << "\n           ";
				Cout() << " " << (c ? c->GetRect().ToString() : String("null"));
			}
			Cout() << "\n";
		}
		Cout() << "\nSidebar mode: " << ExplorerNav::ModeName(nav.GetMode())
		       << ", splitter " << nav.psplit.GetRect().ToString()
		       << ", panes: " << (nav.tree.IsVisible() ? "tree " : "")
		       << (nav.left.IsVisible() ? "places " : "")
		       << (nav.right.IsVisible() ? "bookmarks" : "")
		       << ", tree " << nav.tree.GetLineCount() << " lines, cursor on "
		       << (nav.SelectedTreePath().IsEmpty() ? "-" : nav.SelectedTreePath())
		       << "\n  pane rects: tree " << nav.tree.GetRect().ToString()
		       << " " << (nav.tree.IsVisible() ? "shown" : "hidden")
		       << ", places " << nav.left.GetRect().ToString()
		       << " " << (nav.left.IsVisible() ? "shown" : "hidden")
		       << ", bookmarks " << nav.right.GetRect().ToString()
		       << " " << (nav.right.IsVisible() ? "shown" : "hidden") << "\n";
		{
			ExplorerPanel& panel = ActivePanel();
			if(panel.IsIconMode()) {
				// The icon list's geometry, which is the part of the view mode that
				// no amount of reading the model would tell you.
				ColumnList& icons = panel.icons;
				Cout() << "\nIcon view: " << icons.GetCount() << " cells, "
				       << icons.GetColumns() << " columns, cell "
				       << icons.GetItemWidth() << "x" << icons.GetItemHeight() << "\n";
			}
			// The detailed listing, by column: the headings with their widths and
			// the values of every row. This is the only way to see what a column
			// shows, and a column that is on the wrong side of a wrong width looks
			// fine in a widget dump.
			ExplorerColumns& cols = panel.columns;
			ExplorerModel& m = ActiveModel();
			Vector<ListColumn> shown = m.VisibleColumns();
			int nw = cols.header.lefts.GetCount() > 1
			       ? cols.header.lefts[1] - cols.header.lefts[0] : 0;
			Cout() << "\nDetailed view: " << cols.GetCount() << " rows, "
			       << shown.GetCount() << " columns, sort "
			       << ExplorerModel::SortKeyName(m.GetSortBy()) << " "
			       << (m.GetSortDir() == ExplorerModel::ASCENDING ? "asc" : "desc")
			       << ", name width " << nw << ", strip height "
			       << cols.header.HeaderHeight() << "\n";
			// The heading strip, by column: the title it is supposed to draw, the
			// left edge it draws at and the width it gets. A heading that paints
			// into a zero-width clip looks identical in a widget dump to one that
			// was never asked for, and the titles are not readable anywhere else.
			for(int i = 0; i < shown.GetCount(); i++) {
				int w = i + 1 < cols.header.lefts.GetCount()
				      ? cols.header.lefts[i + 1] - cols.header.lefts[i] : -1;
				Cout() << "  heading [" << i << "] \"" << ListColumnTitle(shown[i])
				       << "\" left " << cols.header.lefts[i] << " width " << w << "\n";
			}
			// The whole sort state in the model's own words, so the dump and the
			// twin's listing header cannot describe the same order differently --
			// and so a toggle is verifiable in both states rather than only in the
			// one the window happened to open in.
			Cout() << "sort: " << m.DescribeSort() << "\n";
			for(int i = 0; i < cols.GetCount(); i++) {
				Cout() << "  " << (i == cols.GetCursor() ? "> " : "  ")
				       << Pad(IconKindName(EntryIconKind(m.Entries()[i])), 12);
				for(ListColumn c : shown)
					Cout() << " | " << ListColumnValue(c, m.Entries()[i]);
				Cout() << "\n";
			}
		}
		// Three probes over the verbs this window only exposes through a menu.
		// Each one exercises the command and prints what the widgets say afterwards,
		// because a menu item wired to nothing and a command that did nothing are the
		// same picture: an item that is there, and nothing else that changed.
		//
		// The selection verbs are probed on whichever list this run is drawing, so
		// the pair of runs (--view list and --view big) covers both flavours: a
		// Deselect all that worked in only one of them would look identical in the
		// other.
		{
			ExplorerPanel& p = ActivePanel();
			auto selected = [&p] {
				int n = 0;
				for(int i = 0; i < p.Count(); i++)
					if(p.IsSelected(i)) n++;
				return n;
			};
			int before = selected();
			p.SelectAll();
			int after_all = selected();
			p.DeselectAll();
			int after_none = selected();
			Cout() << "Selection: " << before << " of " << p.Count() << " rows, cursor "
			       << p.Cursor() << ", drawn as " << (p.IsIconMode() ? "icons" : "columns")
			       << "\n"
			       << "selection verbs: " << before << " -> select all " << after_all
			       << " -> deselect all " << after_none << "\n";
			// Copy names, round-tripped through the *system* clipboard and put back
			// the way it was: a diagnostic run has no business leaving the user's
			// clipboard holding a file name. A non-empty reading after the call is
			// the evidence that the verb reached the system clipboard rather than the
			// internal one, which is not readable from here at all.
			if(p.Count() > 0) {
				String was_on_clipboard = ReadClipboardText();
				p.SelectOne(0, true);
				CopyNames();
				String copied = ReadClipboardText();
				p.DeselectAll();
				Cout() << "copy names: \"" << copied << "\" from the system clipboard\n";
				if(!was_on_clipboard.IsEmpty()) WriteClipboardText(was_on_clipboard);
			}
		}
		// The tabs, because Remove all tabs and the per-tab sort settings are both
		// per tab, so a dump that shows one model cannot tell "the toggle works" from
		// "the toggle changed the tab I was looking at".
		Cout() << "Tabs: " << GetTabCount() << ", active " << ActiveTab() << "\n";
		for(int i = 0; i < main_pane.GetTabCount(); i++)
			Cout() << "  " << (i == main_pane.ActiveTab() ? "* " : "  ") << main_pane.models[i].GetPath()
			       << "  " << ViewModeName(main_pane.models[i].GetViewMode()) << "  "
			       << main_pane.models[i].DescribeSort() << "\n";
		if(split_on) {
			Cout() << "Split pane tabs: " << split_pane.GetTabCount() << ", active " << split_pane.ActiveTab() << "\n";
			for(int i = 0; i < split_pane.GetTabCount(); i++)
				Cout() << "  " << (i == split_pane.ActiveTab() ? "* " : "  ") << split_pane.models[i].GetPath()
				       << "  " << ViewModeName(split_pane.models[i].GetViewMode()) << "  "
				       << split_pane.models[i].DescribeSort() << "\n";
		}
		// The constructor's Synchronize panels probe, in its own words. Like the tab
		// probe it cannot run here: turning the split on adds a second panel, and
		// doing that during the paint loop is what crashed this dump once already.
		if(!sync_probe.IsEmpty())
			Cout() << "sync panels: " << sync_probe << "\n";
		// What the constructor's Remove all tabs probe found, in its own words. The
		// probe cannot run here: it opens tabs, and opening a tab during the paint
		// loop is what crashed this dump before the probe was moved.
		if(!tab_probe.IsEmpty())
			Cout() << "remove all tabs: " << tab_probe << "\n";
		// The overlay demonstration (phase-02 task 04), last stage of the dump.
		// The dump above is stage 1: the window was built on the host files. This
		// cycle mounts the demo VFS, re-reads the model, unmounts and re-reads
		// again -- model level only, no chrome, because adding and removing
		// controls inside the paint loop is the tab-probe crash class; the chrome
		// surface of the overlay is what --demo-overlay proves, since that window
		// is *built* on the mount. The model's own mount line in each block is
		// the evidence that the deepest mount resolved differently per stage.
		if(!ExplorerDemo::cycle_path.IsEmpty()) {
			String error;
			const String& target = ExplorerDemo::cycle_path;
			auto print_stage = [&](const char *title) {
				ExplorerModel& m = ActiveModel();
				Cout() << "\ndemo cycle, " << title << ":\n";
				Cout() << "  mount:   " << (m.GetMount() ? m.GetMount()->path.AsSysPath("/") : String("-"))
				       << (m.GetMount() && !m.GetMount()->type.IsEmpty()
				           ? String(" (") + m.GetMount()->type + ")" : String("")) << "\n";
				Cout() << "  status:  " << m.StatusText() << "\n";
				Vector<ListColumn> shown = m.VisibleColumns();
				Cout() << "  listing: " << m.Entries().GetCount() << " rows\n";
				for(int i = 0; i < m.Entries().GetCount(); i++) {
					Cout() << "    " << Pad(IconKindName(EntryIconKind(m.Entries()[i])), 12);
					for(ListColumn c : shown)
						Cout() << " | " << ListColumnValue(c, m.Entries()[i]);
					Cout() << "\n";
				}
			};
			Cout() << "\n== demo cycle, stage 2: mounting the demo overlay at '" << target << "' ==\n";
			if(!MountDemoOverlay(target, error))
				Cout() << "  error: " << error << "\n";
			else {
				if(!ActiveModel().SetPath(target, error))
					Cout() << "  error: " << error << "\n";
				else
					print_stage("stage 2: the overlay replaces the directory");
				Cout() << "\n== demo cycle, stage 3: unmounted, the host files are back ==\n";
				if(!UnmountDemoOverlay(target, error))
					Cout() << "  error: " << error << "\n";
				else {
					if(!ActiveModel().SetPath(target, error))
						Cout() << "  error: " << error << "\n";
					else
						print_stage("stage 3: host files restored");
				}
			}
		}
		// The menu, walked the way the GUI automation gate walks it: the live MenuBar,
		// with the submenu procedures filled as it goes. A submenu's items are built
		// when the user pulls it, so they are not in the widget tree at all -- an item
		// that was never added is invisible to everything else this dump prints, and a
		// missing menu item is exactly what a file manager gets reported as missing.
		{
			GuiAutomationVisitor vis;
			vis.Read(menu);
			Cout() << "\nMenu: " << menu.GetAutomationItemCount() << " menus, "
			       << vis.elements.GetCount() << " items\n";
			for(const AutomationElement& el : vis.elements)
				Cout() << "  " << el.path << (el.checked ? " [x]" : " [ ]")
				       << (el.enabled ? "" : " (disabled)") << "\n";
			// What Help > Keyboard shortcuts shows, out of this same walk, so the
			// window a user opens and the list a script reads cannot be two
			// different things. An item with no key is in neither, which is the
			// finding worth having: a verb only a toolbar can reach.
			Vector<ShortcutRow> keys = ReadShortcuts(vis);
			Cout() << "\nShortcuts: " << keys.GetCount() << " of "
			       << vis.elements.GetCount() << " items have one\n";
			for(const ShortcutRow& row : keys)
				Cout() << "  " << row.menu_path << "  " << row.keys << "\n";
		}
		// The GTK backend leaves IsWndOpen() true after a window is destroyed -- it asks
		// gtk_widget_get_window(), which keeps answering for a destroyed widget -- so
		// EventLoopIteration never sees the loop end and Close() does not return from
		// Run(). There is no working Ctrl::EndSession() on this backend either. So a
		// dump-and-exit run leaves through _exit(), which skips the atexit handlers
		// that would otherwise wait on threads this process is abandoning on purpose.
		Cout().Flush();
		_exit(0);
	}
}

void Explorer::Menu(Bar& bar)
{
	bar.Sub(t_("File"), [this](Bar& bar) {
		bar.Add(t_("Open"), THISBACK(ActivateSelection)).Key(K_ENTER);
		bar.Separator();

		// Enabled or disabled from one shared reason, so the menu can never offer a
		// command the toolbar would refuse, or the other way round.
		String blocked = ModifyBlockReason();
		bar.Add(t_("New folder"), CtrlImg::MkDir(), THISBACK(NewFolder))
			.Enable(blocked.IsEmpty()).Key(K_CTRL | K_N).Tip(t_("Create a directory here"));
		bar.Add(t_("New file"), CtrlImg::new_doc(), THISBACK(NewFile))
			.Enable(blocked.IsEmpty());
		bar.Add(t_("Create symlink..."), [this] { CreateSymlink(); })
			.Enable(blocked.IsEmpty()).Key(K_CTRL | K_L).Tip(t_("Create a symbolic link"));
		bar.Add(t_("Rename"), CtrlImg::write(), THISBACK(RenameSelected))
			.Enable(blocked.IsEmpty()).Key(K_F2);
		bar.Add(t_("Delete permanently"), CtrlImg::remove(), THISBACK(DeleteSelected))
			.Enable(blocked.IsEmpty()).Key(K_DELETE)
			.Tip(t_("Remove the selected entries for good -- there is no trash to restore them from"));
		// Contiguous with Delete and Properties, which is where xfe has it: they are
		// three ways of acting on the selection, not three kinds of command.
		bar.Add(t_("Copy names"), [this] { CopyNames(); })
			.Enable(!SelectedNames().IsEmpty())
			.Tip(t_("Put the names of the selected entries on the clipboard, one per line"));
		bar.Add(t_("Properties"), THISBACK(ShowProperties))
			.Enable(SelectedNames().GetCount() == 1);
		bar.Separator();
		bar.Add(t_("New tab"), [this] { NewTab(ActiveModel().GetPath()); }).Key(K_CTRL | 'T');
		bar.Add(t_("Close tab"), [this] { CloseTab(ActiveTab()); }).Key(K_CTRL | 'W');
		bar.Add(t_("Remove all tabs"), THISBACK(RemoveAllTabs))
			// Off with one tab, because it would close the window rather than the
			// tabs: an item that is live only when it cannot be used correctly is an
			// item that can be pressed at the wrong moment.
			.Enable(GetTabCount() > 1)
			.Tip(t_("Close every tab but the one being shown"));
		bar.Separator();
		bar.Add(t_("Copy to..."), CtrlImg::copy(), [this] { TransferSelected(false); })
			.Enable(blocked.IsEmpty());
		bar.Add(t_("Move to..."), CtrlImg::cut(), [this] { TransferSelected(true); })
			.Enable(blocked.IsEmpty());
		if(!blocked.IsEmpty())
			bar.Add(t_("(read-only: ") + blocked, [] {}).Enable(false);
		bar.Separator();
		bar.Add(t_("Refresh"), [this] { OpenPath(ActiveModel().GetPath()); });
		bar.Add(t_("Exit"), [this] { this->Close(); });
	});

	// Edit, as xfe has it. Copy and Cut were *toolbar buttons and nothing else*:
	// no menu entry, no key. A verb that exists only in a toolbar disappears for
	// anyone who switches the Tools toolbar off -- which View > Toolbars allows --
	// and for anyone driving the window from the keyboard. The enable rules are the
	// toolbar's own, computed the same way, because a menu that offers what the
	// toolbar refuses (or the other way round) is two answers to one question.
	bar.Sub(t_("Edit"), [this](Bar& bar) {
		String blocked = ModifyBlockReason();
		bool have_selection = SelectedNames().GetCount() > 0;
		bar.Add(t_("Copy"), CtrlImg::copy(), [this] { CopySelected(false); })
			.Enable(blocked.IsEmpty() && have_selection)
			.Key(K_CTRL | 'C')
			.Tip(t_("Put the selected entries on the clipboard"));
		bar.Add(t_("Cut"), CtrlImg::cut(), [this] { CopySelected(true); })
			.Enable(blocked.IsEmpty() && have_selection)
			.Key(K_CTRL | 'X')
			.Tip(t_("Put the selected entries on the clipboard to be moved"));
		bar.Add(t_("Paste"), CtrlImg::paste(), THISBACK(PasteClipboard))
			.Enable(blocked.IsEmpty() && !clip_names.IsEmpty())
			.Key(K_CTRL | 'V')
			.Tip(t_("Paste what was copied or cut into this directory"));
		bar.Separator();
		// The three verbs about the *state* of the selection, together: they were
		// in View, where they were the only things in that menu with nothing to do
		// with the view. They moved here whole rather than one at a time, and the
		// ones that had a key kept it -- a shortcut that follows its item is the
		// reason a user who learned Ctrl+A can still find it.
		bar.Add(t_("Select all"), THISBACK(SelectAll)).Key(K_CTRL | 'A');
		bar.Add(t_("Deselect all"), THISBACK(DeselectAll)).Key(K_CTRL | K_SHIFT | 'A');
		bar.Add(t_("Invert selection"), THISBACK(InvertSelection)).Key(K_CTRL | 'I');
	});

	bar.Sub(t_("View"), [this](Bar& bar) {
		bar.Sub(t_("Sort by"), [this](Bar& bar) {
			struct Entry { const char *label; ExplorerModel::SortBy key; char accel; };
			static const Entry entries[] = {
				{ t_("Name"),      ExplorerModel::NAME,      '1' },
				{ t_("Extension"), ExplorerModel::EXTENSION, '2' },
				{ t_("Size"),      ExplorerModel::SIZE,      '3' },
				{ t_("Time"),      ExplorerModel::TIME,      '4' },
				{ t_("Type"),      ExplorerModel::TYPE,      '5' },
			};
			for(const Entry& e : entries)
				bar.Add(e.label, [this, e] { SetSort(e.key); })
					.Check(ActiveModel().GetSortBy() == e.key)
					// The character's code, not the digit. A key code below 32 is
					// not a digit: GetKeyDesc() printed it as "Alt+0001" (hex of
					// the code) and no keypress could ever match it, so all five of
					// these accelerators were dead on arrival while looking bound.
					// Reading the list in Help > Keyboard shortcuts is what showed
					// it -- a shortcut nobody has ever pressed is exactly the kind
					// that a menu screenshot cannot catch.
					.Key(K_ALT | e.accel);
			bar.Separator();
			bar.Add(t_("Ascending"), [this] {
				ExplorerModel& m = ActiveModel();
				m.SetSort(m.GetSortBy(), ExplorerModel::ASCENDING);
				ActivePanel().Render(m);
				UpdateStatus();
			}).Check(ActiveModel().GetSortDir() == ExplorerModel::ASCENDING);
			bar.Add(t_("Descending"), [this] {
				ExplorerModel& m = ActiveModel();
				m.SetSort(m.GetSortBy(), ExplorerModel::DESCENDING);
				ActivePanel().Render(m);
				UpdateStatus();
			}).Check(ActiveModel().GetSortDir() == ExplorerModel::DESCENDING);
			bar.Separator();
			// The two toggles that are not a key or a direction. They live beside the
			// keys rather than at the top of the View menu because they are about the
			// *comparison*, and the whole submenu is: what the names are compared by,
			// and how.
			bar.Add(t_("Folders first"), THISBACK(ToggleDirsFirst))
				.Check(ActiveModel().GetDirsFirst())
				.Tip(t_("List the directories before the files"));
			bar.Add(t_("Ignore case"), THISBACK(ToggleIgnoreCase))
				.Check(ActiveModel().GetIgnoreCase())
				.Tip(t_("Compare names as if they were all lower case"));
		});
		bar.Separator();
		// The three listing modes, as in xfe's panel menus: big icons, small icons
		// and the detailed list. Ctrl+1..3 because they are the most switched of the
		// view commands.
		bar.Add(t_("Big icons"), [this] { SetViewMode(VIEW_BIG_ICONS); })
			.Check(ActiveModel().GetViewMode() == VIEW_BIG_ICONS)
			.Key(K_CTRL | '1');
		bar.Add(t_("Small icons"), [this] { SetViewMode(VIEW_SMALL_ICONS); })
			.Check(ActiveModel().GetViewMode() == VIEW_SMALL_ICONS)
			.Key(K_CTRL | '2');
		bar.Add(t_("Detailed list"), [this] { SetViewMode(VIEW_LIST); })
			.Check(ActiveModel().GetViewMode() == VIEW_LIST)
			.Key(K_CTRL | '3');
		bar.Separator();
		// The columns belong to the detailed list, so the commands that change
		// them live here as well as on the right-click menu over the headings.
		// A right-click menu that is the only way to reach a setting is a setting
		// most people never find.
		bar.Add(t_("Edit columns..."), THISBACK(EditColumns))
			.Tip(t_("Choose which values the detailed list shows"))
			.Enable(ActiveModel().GetViewMode() == VIEW_LIST);
		bar.Add(t_("Reset columns"), [this] {
			ActiveModel().ResetColumns();
			ActivePanel().Render(ActiveModel());
			UpdateHistoryButtons();
		}).Enable(ActiveModel().GetViewMode() == VIEW_LIST);
		bar.Separator();
		// Zoom, as its own three commands rather than a dialog: the whole point is
		// to see more names at once, which is a thing you do while looking, not a
		// thing you set up. Greyed out in the detailed list, which has no picture
		// to scale -- a live button that changes nothing reads as a broken one.
		bool zooming = ActiveModel().GetViewMode() != VIEW_LIST;
		int zoom = ActiveModel().GetViewZoom();
		bar.Add(t_("Zoom in"), THISBACK(ZoomIn))
			.Enable(zooming && zoom < VIEW_ZOOM_LARGEST)
			.Key(K_CTRL | '=');
		bar.Add(t_("Zoom out"), THISBACK(ZoomOut))
			.Enable(zooming && zoom > VIEW_ZOOM_SMALLEST)
			.Key(K_CTRL | '-');
		bar.Add(t_("Reset zoom"), [this] { SetZoom(VIEW_ZOOM_DEFAULT); })
			.Enable(zooming && zoom != VIEW_ZOOM_DEFAULT)
			.Key(K_CTRL | '0');
		bar.Separator();
		// Select all / Deselect all / Invert selection used to be here. They are in
		// Edit now, with Copy, Cut and Paste; what is left in View is what View is
		// about. The empty submenu is not an option: a menu row that opens nothing
		// is a defect, and a leftover heading over an empty list is the same thing
		// one level down.
		bar.Add(t_("Hidden files"), THISBACK(ToggleHidden))
			.Check(ActiveModel().GetShowHidden());
		bar.Separator();
		// The two verbs about the split pane. They live in View rather than in a
		// Tools menu because the split buttons are in the *view* toolbar and View
		// is the menu that already owns what that toolbar switches on and off.
		// Both are dead with one pane: there is nothing to synchronize with and
		// nothing to switch to, and an item that is live only when it cannot be
		// used correctly is an item that can be pressed at the wrong moment.
		// The split itself belongs here too: it existed only as two toolbar
		// buttons, and a user hunting the menus for a two-panel mode found
		// nothing -- the review's "there is no 2-panel mode". The menu and the
		// toolbar offer the same two items.
		bar.Add(t_("Two panels side by side"), [this] { SetSplit(true); })
			.Check(split_on && split_dir)
			.Tip(t_("Show two listing panels side by side in the main area"));
		bar.Add(t_("Two panels stacked"), [this] { SetSplit(false); })
			.Check(split_on && !split_dir)
			.Tip(t_("Show two listing panels stacked in the main area"));
		bar.Separator();
		bar.Add(t_("Synchronize panels"), THISBACK(ToggleSyncPanels))
			.Enable(split_on)
			.Check(sync_panels)
			.Key(K_CTRL | K_SHIFT | 'S')
			.Tip(t_("A navigation in one panel shows the same directory in the other"));
		bar.Add(t_("Switch panels"), THISBACK(SwitchPane))
			.Enable(split_on)
			.Key(K_CTRL_TAB)
			.Tip(t_("Act on the other panel"));
		bar.Separator();
		bar.Add(t_("Reload mounts"), THISBACK(RefreshNav));
		bar.Add(t_("Log window"), THISBACK(ShowLog));
	});

	bar.Sub(t_("Bookmarks"), [this](Bar& bar) {
		// Bookmarks submenu (from previous Go > Bookmarks)
		// Go to folder (from previous Go > Go to folder)
		bar.Add(t_("Go to folder..."), THISBACK(FocusLocationBar))
			.Key(K_CTRL | 'L')
			.Tip(t_("Type a path to go there"));
		bar.Separator();
		const Vector<String>& b = GetExplorerBookmarks().Entries();
		for(const String& p : b) {
			String target = p;
			bar.Add(target, [this, target] { OpenPath(target); });
		}
		if(b.GetCount()) bar.Separator();
		bar.Add(t_("Add current folder"), [this] { AddBookmark(); })
			.Key(K_CTRL | 'D');
		bar.Add(t_("Remove selected bookmark"), THISBACK(RemoveBookmark))
			.Enable(nav.SelectedBookmark().IsEmpty() == false);
		bar.Separator();
		// Navigation options (from previous Go > Up, Back, Forward)
		bar.Add(t_("Up one level"), [this] {
			String error;
			ActiveModel().Up(error);
			NavigateTo(pane, ActiveModel().GetPath(), false);
		}).Enable(ActiveModel().CanUp());
		bar.Add(t_("Back"), CtrlImg::go_back(), [this] {
			String error;
			if(ActiveModel().Back(error))
				NavigateTo(pane, ActiveModel().GetPath(), false);
		}).Enable(ActiveModel().CanBack());
		bar.Add(t_("Forward"), CtrlImg::go_forward(), [this] {
			String error;
			if(ActiveModel().Forward(error))
				NavigateTo(pane, ActiveModel().GetPath(), false);
		}).Enable(ActiveModel().CanForward());
		bar.Separator();
		// Mounts and History (from previous Go > Mounts and History)
		bar.Sub(t_("Mounts"), [this](Bar& bar) {
			for(MountPoint* mp : MountManager::System().Enumerate()) {
				String path = mp->path.AsSysPath("/");
				if(path.IsEmpty()) path = "/";
				String label = path;
				if(!mp->type.IsEmpty()) label << "   " << mp->type;
				String open = path;
				bar.Add(label, [this, open] { OpenPath(open); });
			}
		});
		if(ActiveModel().GetHistory().GetCount()) {
			bar.Separator();
			bar.Sub(t_("History"), [this](Bar& bar) {
				for(const String& h : ActiveModel().GetHistory()) {
					String p = h;
					bar.Add(p, [this, p] { OpenPath(p); });
				}
			});
		}
		bar.Sub(t_("Bookmarks"), [this](Bar& bar) {
			const Vector<String>& b = GetExplorerBookmarks().Entries();
			for(const String& p : b) {
				String target = p;
				bar.Add(target, [this, target] { OpenPath(target); });
			}
			if(b.GetCount()) bar.Separator();
			bar.Add(t_("Add current folder"), [this] { AddBookmark(); })
				.Key(K_CTRL | 'D');
			bar.Add(t_("Remove selected bookmark"), THISBACK(RemoveBookmark))
				.Enable(nav.SelectedBookmark().IsEmpty() == false);
		});
	});

	// The sidebar (left pane), as xfe has it: the panel modes and what each
	// places panel shows. A menu rather than a tab strip, because the strip would
	// be a second control for the same decision -- and because two panels side by
	// side cannot be tabs at all.

// The sidebar menu: the sidebar's modes and the content of its two columns.
	// Renamed from "Left pane"/"Right pane" in the vocabulary split: "panel"
	// is reserved for the main-area listing panes, and a sidebar menu that
	// says "pane" is a menu whose name promises something it does not own.
	// The sidebar is one family on one axis, so one menu covers it.
		bar.Sub(t_("Sidebar"), [this](Bar& bar) {
			bar.Sub(t_("Mode"), [this](Bar& bar) {
				// The same five modes and the same Alt+6..0 keys the View bar's
				// buttons carry: one family, one set of accelerators, and the
				// generated shortcut list shows them because it reads the menu.
				ExplorerNav::Mode modes[5] = { ExplorerNav::TREE_ONLY, ExplorerNav::ONE,
				                               ExplorerNav::TREE_ONE, ExplorerNav::TWO,
				                               ExplorerNav::TREE_TWO };
				for(int mi = 0; mi < 5; mi++) {
					ExplorerNav::Mode mode = modes[mi];
					bar.Add(t_(ExplorerNav::ModeName(mode)), [this, mode] { SetPanelMode(mode); })
						.Check(nav.GetMode() == mode)
						.Key(K_ALT | (mi < 4 ? '6' + mi : '0'));
				}
			});
			bar.Separator();
			// Which list each sidebar column shows. Two columns with a menu that
			// can only change both of them at once is one column with a scrollbar.
			// The names say "column", not "panel": the sidebar is arranged in
			// columns, and "panel" is reserved for the main-area listing panes
			// (the task-14 vocabulary split). A Finnish "kaksi paneelia" can only
			// ever mean the main-area split.
			bar.Sub(t_("Left column"), [this](Bar& bar) {
				bar.Add(t_("Places"), [this] { nav.left.ShowContent(PlacesPanel::PLACES); })
					.Check(nav.left.GetContent() == PlacesPanel::PLACES);
				bar.Add(t_("Mounts"), [this] { nav.left.ShowContent(PlacesPanel::MOUNTS); })
					.Check(nav.left.GetContent() == PlacesPanel::MOUNTS);
				bar.Add(t_("Bookmarks"), [this] { nav.left.ShowContent(PlacesPanel::BOOKMARKS); })
					.Check(nav.left.GetContent() == PlacesPanel::BOOKMARKS);
			});
			bar.Separator();
			bar.Sub(t_("Right column"), [this](Bar& bar) {
				bar.Add(t_("Places"), [this] { nav.right.ShowContent(PlacesPanel::PLACES); })
					.Check(nav.right.GetContent() == PlacesPanel::PLACES);
				bar.Add(t_("Mounts"), [this] { nav.right.ShowContent(PlacesPanel::MOUNTS); })
					.Check(nav.right.GetContent() == PlacesPanel::MOUNTS);
				bar.Add(t_("Bookmarks"), [this] { nav.right.ShowContent(PlacesPanel::BOOKMARKS); })
					.Check(nav.right.GetContent() == PlacesPanel::BOOKMARKS);
			});
		});
		// The main-area listing panes: the left (main) pane and the right (split)
		// pane. These menus own the panes themselves -- which one is active,
		// how they are arranged, and whether they stay in step -- and never
		// touch the sidebar. A menu that says "Left pane" and moves the
		// sidebar's mode is a menu whose name lies about what it does.
		bar.Sub(t_("Left pane"), [this](Bar& bar) {
			// The main pane is always active when the split is off; when it is
			// on, this makes it the pane a command acts on and moves the focus
			// with it. The focus move is the whole point: a pane switch that
			// leaves the keyboard in the pane the user just left is the version
			// of this bug that deletes the wrong thing.
			bar.Add(t_("Make active"), [this] { if(split_on) { pane = 0; main_pane.ActivePanel().FocusList(); UpdateStatus(); } })
				.Check(!split_on || pane == 0);
			bar.Separator();
			bar.Add(t_("New tab"), [this] { NewTab(ActiveModel().GetPath()); })
				.Key(K_CTRL | 'T');
			bar.Add(t_("Close tab"), [this] { CloseTab(main_pane.ActiveTab()); })
				.Key(K_CTRL | 'W')
				.Enable(main_pane.GetTabCount() > 1);
			bar.Add(t_("Remove all tabs"), [this] { main_pane.RemoveAllTabs(); })
				.Enable(main_pane.GetTabCount() > 1);
		});
		bar.Sub(t_("Right pane"), [this](Bar& bar) {
			// The split pane. Disabled entirely when the split is off: a menu
			// for a pane that is not there is a menu whose items do nothing.
			bar.Add(t_("Make active"), [this] { if(split_on) { pane = 1; split_pane.ActivePanel().FocusList(); UpdateStatus(); } })
				.Check(split_on && pane == 1)
				.Enable(split_on);
			bar.Separator();
			bar.Add(t_("New tab"), [this] { if(split_on) split_pane.NewTab(ActiveModel().GetPath()); })
				.Key(K_CTRL | 'T')
				.Enable(split_on);
			bar.Add(t_("Close tab"), [this] { if(split_on) CloseTab(split_pane.ActiveTab()); })
				.Key(K_CTRL | 'W')
				.Enable(split_on && split_pane.GetTabCount() > 1);
			bar.Add(t_("Remove all tabs"), [this] { if(split_on) split_pane.RemoveAllTabs(); })
				.Enable(split_on && split_pane.GetTabCount() > 1);
			bar.Separator();
			bar.Add(t_("Synchronize"), [this] { ToggleSyncPanels(); })
				.Check(sync_panels)
				.Enable(split_on)
				.Tip(t_("Show the same directory in both panes"));
			bar.Add(t_("Close"), [this] { SetSplitOff(); })
				.Enable(split_on)
				.Tip(t_("Remove the split and return to one pane"));
		});

	// The Tools menu with toolbar group controls and other tools
	bar.Sub(t_("Tools"), [this](Bar& bar) {
		bar.Sub(t_("Toolbars"), [this](Bar& bar) {
			struct Group { const char *label; int group; };
			static const Group groups[] = {
				{ t_("Location"),   3 },
				{ t_("Navigation"), 0 },
				{ t_("Tools"),      1 },
				{ t_("View"),       2 },
			};
			for(const Group& g : groups)
				bar.Add(g.label, [this, g] { SetBarShown(g.group, !bars_shown[g.group]); })
					.Check(bars_shown[g.group]);
			bar.Separator();
			bar.Add(t_("All toolbars"), [this] {
				for(int g = 0; g < 4; g++) SetBarShown(g, true);
			});
			bar.Separator();
			bar.Add(t_("One row per group"), [this] {
				bars_rows_mode = !bars_rows_mode;
				UpdateBars();
				SaveToolbarConf();
			})
				.Check(bars_rows_mode)
				.Tip(t_("Each toolbar group gets a row of its own, like the window used to"));
		});
		bar.Separator();
		bar.Add(t_("Sidebar visible"), [this] { nav.Show(!nav.IsVisible()); })
			.Check(nav.IsVisible());
		bar.Separator();
		bar.Add(t_("Empty Trashbin"), THISBACK(EmptyTrashbin))
			.Tip(t_("Empty the trash bin permanently"));
		bar.Add(t_("Settings..."), THISBACK(ShowSettings))
			.Tip(t_("Open the settings dialog"));
	});

	bar.Sub(t_("Help"), [this](Bar& bar) {
		// The manual first: there is one now, and a Help entry that opens
		// nothing is a defect (the AGENTS menu-bar note). F1 lands on the same
		// page, and the generated shortcut list carries the row because it
		// reads the menu.
		bar.Add(t_("Help contents"), THISBACK(ShowHelp))
			.Key(K_F1)
			.Tip(t_("Open the manual"));
		bar.Add(t_("Keyboard shortcuts"), THISBACK(ShowShortcuts));
		bar.Separator();
		bar.Add(t_("About"), [] {
			// DeQtf, and no "--" in the text. PromptOK takes *QTF*, so the parser is
			// reading markup, not prose: a "--" is a table row separator there, and
			// "Not in table" is what it says when it meets one outside a table.
			// Everything with a meaning in QTF has to go through DeQtf on the way in.
			PromptOK(DeQtf(t_("Explorer, a file manager over MountManager.\n\n"
			                  "Browsing goes through the process-wide MountManager singleton,\n"
			                  "so mounting any VFS replaces what the panels show.")));
		});
	});
}

#endif // flagGUI