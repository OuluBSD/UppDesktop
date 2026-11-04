#ifndef _Explorer_Explorer_h
#define _Explorer_Explorer_h

#ifdef flagGUI

#include <CtrlLib/CtrlLib.h>
#include <GridCtrl/GridCtrl.h>

#define IMAGECLASS ExplorerImg
#define IMAGEFILE  <Explorer/Explorer.iml>
#include <Draw/iml_header.h>

#include "ExplorerModel.h"
#include "ExplorerPolicy.h"
#include "ExplorerLog.h"
#include "ExplorerIcons.h"

NAMESPACE_UPP

// Persistent settings for Explorer, shared by GUI and headless twin.
// Stored in the user's config directory as JSON.
struct ExplorerSettings {
	// Activation policy: single-click or double-click to open
	enum ActivatePolicy { SINGLE_CLICK, DOUBLE_CLICK };
	ActivatePolicy activate_policy = DOUBLE_CLICK;
	
	// Default icon zoom level (pixels)
	int default_icon_zoom = VIEW_ZOOM_DEFAULT;
	
	// Pane/tab behavior
	bool new_tab_same_dir = true;      // New tab opens same directory
	bool close_tab_confirm = false;    // Confirm before closing tab
	bool show_hidden_by_default = false;
	
	// View defaults
	int default_view_mode = VIEW_LIST;
	bool default_dirs_first = true;
	bool default_ignore_case = false;
	
	// Toolbar layout
	bool compact_toolbar = true;
	bool show_nav_bar = true;
	bool show_tool_bar = true;
	bool show_view_bar = true;
	bool show_location_bar = true;
	
	// Split pane
	bool split_horizontal = true;      // true = vertical divider (side-by-side)
	bool sync_panes = false;
	
	// Serialization
	void Jsonize(JsonIO& jio);
	
	// Load/save from config file
	static ExplorerSettings Load();
	void Save() const;
	
	// Config file path
	static String ConfigPath();
};

// The location row: xfe's fourth toolbar group, without a frame of its own.
//
// "Location:", a Clear button, the path field, a drop-list of the places already
// visited, and a Go button. A ParentCtrl whose children are placed by their own
// layout metadata, not by FrameLayout: SyncLayout recomputes a child's rect from
// the parent's, so a rect set by hand in FrameLayout is overwritten on the next
// relayout.
//
// The path is an EditField, not a Label: it is the only way to reach a directory
// that is not in the sidebar, and a file manager that can only be navigated by
// clicking a list is a browser, not a file manager.
// The location row: one PathCtrl -- one clickable button per path component,
// an edit toggle, and the editable field -- plus the filter field at the right.
// The review's ask was exactly this: use the stock ctrl instead of the
// hand-built breadcrumb + field pair. The row belongs to the content panel,
// not to the window chrome: one per listing pane (every tab, and the split
// pane), because a pane's path is that pane's state -- the task-18 hoist to
// the window frame was what hollowed out the two-pane modes (the inactive
// pane's path invisible, both panes following one navigation).
//
// The go button is gone because Enter in the field is the go, and the clear
// button is gone because the edit toggle off is the cancel: leaving edit mode
// puts the buttons -- and the current path -- back. The history drop-list is
// gone with the old field; the history itself stays on the model and in the
// tabs' Back/Forward, and a drop-list on PathCtrl is a CtrlLib enhancement for
// another day rather than a fork of the stock ctrl.
//
// Font::GetStdFontSize(), not GetStdFont().GetHeight(): the latter reads the
// per-font info, which is not realized yet while the frame is first laid out,
// and the frame then gets zero height -- a location bar one pixel tall.
struct LocationBar : public ParentCtrl {
	enum { FILTER_WIDTH = 240 };

	PathCtrl    path_ctrl;   //< buttons + edit toggle + field, in one ctrl
	SuggestCtrl filter;      //< the filter, right-anchored: it filters the listing live

	int  RowHeight() const { return Font::GetStdFontSize().cy + 8; }

	// Same face fill as every bar row: an unpainted ParentCtrl reads black on
	// the GTK backend. The PathCtrl's own ToolBarBg paints the toolbar look
	// inside its rect, so the row reads as part of the bar family.
	void Paint(Draw& w) override               { w.DrawRect(0, 0, GetSize().cx, GetSize().cy, SColorFace()); ParentCtrl::Paint(w); }

	// Puts the text in without the field grabbing the keyboard, so navigating by
	// double-click or the toolbar does not leave the caret sitting in the path
	// ready to be typed over by the next keystroke.
	void SetPathText(const String& p)          { path_ctrl.SetPath(p); }
	// Puts the text in *and* puts the caret in it, for Go to folder.
	void FocusPath(const String& p)            { path_ctrl.SetPath(p); path_ctrl.FocusPath(); }

	bool Access(Visitor& v) override;   //< gui-user-gate TOON surface

	Event<> WhenGo;              //< the typed path was entered: navigate to it

	LocationBar();
};
// The breadcrumb row: the path as a row of buttons, one per component, so that
// getting to an ancestor is one click on its name instead of "up" as many times as
// there are levels.
//
// It sits above the path field rather than replacing it. The field is the only way
// to reach a directory that is not in the sidebar, and a breadcrumb can only offer
// the ancestors of where you already are -- replacing the field with one would make
// an unknown path unreachable.

// One places panel: a caption and the three lists of somewhere-to-go, of which
// exactly one is shown.
//
// A caption and a list, not a TabCtrl with three tabs. A tab strip says "these
// are alternatives you pick between", and the Left Panel menu is what picks --
// three tabs and a menu that chooses between the same three things is two
// controls for one decision, and the strip takes the width the names need.
struct PlacesPanel : public ParentCtrl {
	enum Content { PLACES, MOUNTS, BOOKMARKS };

	LabelBox caption;
	ArrayCtrl places;
	ArrayCtrl drives;
	ArrayCtrl marks;

	PlacesPanel(const char *name, Content initial);

	//< Fill every list from the model and from the bookmark service. All three
	//< are filled whatever is showing: a panel the user switches to should not
	//< have to wait for a re-read to be usable.
	void Populate();
	void PopulateBookmarks();
	//< Show one of the three, and say which in the caption.
	void ShowContent(Content c);
	Content GetContent() const                  { return content; }
	//< The list currently on show, for the dump and for the verbs.
	ArrayCtrl& Active();

	void SelectPlace(const String& path);
	//< The bookmark the user has selected, or empty.
	String  SelectedBookmark() const;

	//< Wire every list's double click and Enter to WhenPlaceSelected, so a list
	//< cannot be added without the behaviour the others have.
	void WireOpen();

	Event<String> WhenPlaceSelected;

	bool Access(Visitor& v) override;        //< gui-user-gate TOON surface

private:
	Content content = PLACES;
};

// The sidebar: one vertical splitter owning all three panes, top to bottom --
// the directory tree, the places panel, the bookmarks panel -- with the mode
// choosing which panes are visible. The review's ask: the tree used to be a
// peer of the whole vertical splitter (a horizontal split of its own), which
// put sidebar things on two axes; now every sidebar thing is one family on one
// axis, and the horizontal space belongs to the sidebar/listing split alone.
//
// TREE_ONLY is the default because the classic Windows Explorer sidebar is the
// folders tree and nothing else; places and bookmarks come back with one click
// or one key. The widgets are created once and shown or hidden, so a mode
// change is a Show() and a relayout rather than a rebuild, and the tree keeps
// whatever the user had open.
struct ExplorerNav : public ParentCtrl {
	enum Mode { TREE_ONLY, ONE, TREE_ONE, TWO, TREE_TWO };

	Mode        mode = TREE_ONLY;
	Splitter    psplit;   //< tree | left(places) | right(bookmarks), top to bottom
	TreeCtrl    tree;
	PlacesPanel left, right;
	//< The divider positions per mode, in the splitter's 10000-unit space, so
	//< a mode switch does not snap the sidebar back to factory proportions.
	//< Two dividers for three panes; a hidden pane's divider is clamped.
	//< Plain ints: the fork's containers are move-only, and a 2-int row is
	//< not worth a Vector.
	int  remembered_pos[5][2];
	bool remembered_set[5] = { false, false, false, false, false };

	ExplorerNav();

	//< Change the mode. The widgets are created once and shown or hidden, so a
	//< mode change is a Show() and a relayout rather than a rebuild, and the
	//< tree keeps whatever the user had open.
	void SetMode(Mode m);
	Mode GetMode() const                       { return mode; }
	//< The name of a mode, for the menu, the tip and the dump.
	static const char *ModeName(Mode m);
	static bool ModeTree(Mode m)               { return m == TREE_ONLY || m == TREE_ONE || m == TREE_TWO; }
	static bool ModeRight(Mode m)              { return m == TWO || m == TREE_TWO; }

	//< The splitter's layout shows every child it lays out -- the mode's hidden
	//< panes come back on the next relayout -- so the mode is re-asserted after
	//< it, here, on every layout.
	void Layout() override;

	//< Put the current path on the tree, with its ancestors, and open it. Called
	//< from every navigation: a tree that does not follow the listing is a
	//< picture of somewhere else.
	void SyncTree(const String& path);
	//< Fill a node's children with the directories of `path`. One level at a
	//< time, on expansion: a tree built all at once from / is tens of thousands
	//< of rows, most of them never looked at.
	void FillTreeNode(int id, const String& path);

	void Populate();
	void PopulateBookmarks();
	void SelectPlace(const String& path);
	String  SelectedBookmark() const;
	//< The path of the tree row the user has selected, or empty.
	String  SelectedTreePath() const;

	Event<String> WhenPlaceSelected;

	bool Access(Visitor& v) override;        //< gui-user-gate TOON surface
};

// One cell of the icon view: a picture over a centred, wrapped name.
//
// This is CtrlLib::IconCell, not a private display: the cell is a Display rather
// than a widget (ColumnList in RowMode already lays items out as a grid of
// fixed-size cells and already handles the cursor, selection, keyboard
// navigation and the scroll bar), and nothing about the picture-over-a-wrapped-
// name layout is Explorer-specific. See uppsrc/CtrlLib/IconCell.h.

// One row of the detailed listing.
//
// A Display rather than a widget: ColumnList lays a list of these out as rows and
// already does the cursor, the selection, the keyboard and the scroll bar. ColumnList
// keeps a *pointer* to a row's display, so the rows are One<>d and kept in a vector
// -- see RenderIcons for the same reason.
struct ExplorerRow : Display {
	Vector<String> values;
	//< Left edge of each column plus one past the last, owned by the columns
	//< widget. A pointer to the vector, not a copy: the columns are the same for
	//< every row and are re-measured when one is shown or hidden.
	const Vector<int> *lefts = nullptr;

	Size GetStdSize(const Value& q) const override;
	void Paint(Draw& w, const Rect& r, const Value& q, Color ink, Color paper,
	           dword style) const override;
};

// The strip of column headings above the detailed listing.
//
// A Ctrl of its own rather than a row of the list: it is not part of the listing,
// it decides the sort, and it is the only place a column can be shown or hidden.
// Drawn with the same left edges the rows use, so a heading is above its own
// values and not near them.
struct ExplorerColumnHeader : Ctrl {
	//< Left edge of each column plus one past the last, shared with the rows.
	Vector<int> lefts;
	//< Which column the listing is sorted by, or -1. The arrow is drawn here, not
	//< in the rows: an arrow repeated on every line is noise.
	int  sort_column = -1;
	bool ascending = true;
	int  hover = -1;
	bool inside = false;   //< the pointer is over the strip, so the heading under it is hot
	bool pressed = false;

	void SetColumns(const Vector<int>& l, int sort_col, bool asc);
	//< Height for the headings, from the font rather than from a constant, so a
	//< larger system font does not put the title through the first row.
	int  HeaderHeight() const;
	int  ColumnAt(Point p) const;

	void Paint(Draw& w) override;
	void MouseMove(Point p, dword flags) override;
	void MouseLeave() override;
	void LeftDown(Point p, dword flags) override;
	void LeftUp(Point p, dword flags) override;
	void RightDown(Point p, dword flags) override;
	Image CursorImage(Point p, dword flags) override;

	//< Index into the visible column list, which is what the panel knows.
	Event<int> WhenSortClick;
	Event<>    WhenColumnMenu;
};

// The detailed listing: a heading strip, the rows, and the field that renames a
// row in place.
// An EditField that reports Escape.
//
// EditField swallows K_ESCAPE and offers no event for it, so a rename field that
// is a plain EditField can only be left with Enter -- which accepts whatever is
// in it. Five lines of subclass is cheaper than that for the rest of the program.
struct ExplorerRenameField : EditField {
	Event<> WhenCancel;

	bool Key(dword key, int count) override
	{
		if(key == K_ESCAPE) {
			WhenCancel();
			return true;
		}
		return EditField::Key(key, count);
	}
};

struct ExplorerColumns : public ParentCtrl {
	ExplorerColumnHeader  header;
	ColumnList            rows;
	ExplorerRenameField   edit;   //< a sibling of the list, see the constructor

	ExplorerColumns();

	// A background, not a decoration: the listing's ViewFrame border is painted
	// by its own frame (a sunken 98 client edge, correct as it is), but every
	// region of this widget that nothing paints reads black on the GTK backend
	// (no backing store) -- during resize and transient states, first of all.
	// The face fill underneath guarantees there is no such region.
	void Paint(Draw& w) override             { w.DrawRect(0, 0, GetSize().cx, GetSize().cy, SColorFace()); ParentCtrl::Paint(w); }

	//< Fill from the model, and re-measure the column edges.
	void Build(ExplorerModel& model);
	//< The visible columns, and where each one starts. Public because the header
	//< and the rows are siblings that both need them.
	Vector<int> MeasureColumns(const ExplorerModel& model) const;

	// --- the verbs, all view-neutral ------------------------------------------
	int    GetCount() const                  { return rows.GetCount(); }
	int    GetCursor() const                 { return rows.GetCursor(); }
	bool   IsSel(int i) const                { return rows.IsSel(i); }
	void   SelectOne(int i, bool on)         { rows.SelectOne(i, on); }
	void   SetCursor(int i)                  { rows.SetCursor(i); }
	String NameAt(int i) const               { return i >= 0 && i < rows.GetCount()
	                                              ? rows.Get(i).ToString() : String(); }
	void   SelectAll();
	void   InvertSelection();
	//< Leave nothing selected. The counterpart of SelectAll, and on the same menu
	//< row, so a user who has just selected everything has one command to undo it
	//< with rather than having to shift-click their way back.
	void   DeselectAll();
	//< Put an edit field over the name of the cursor row. Nothing happens
	//< without a cursor, and the field hides itself on every rebuild, so a
	//< rename cannot outlive the row it is renaming.
	void   BeginRename();

	//< Where the rename field goes: the name cell of `row`, in this widget's
	//< coordinates.
	Rect RenameRect(int row) const;

	bool Access(Visitor& v) override;        //< gui-user-gate TOON surface

	//< Right-click on rows (not header). Fires with screen position.
	virtual void RightDown(Point p, dword keyflags) override;

	//< Get item at client position (forwards to rows).
	int GetItem(Point p)              { return rows.GetItem(p); }


Event<int> WhenSort;      //< index into the visible columns
	Event<>    WhenColumnMenu;
	Event<>    WhenActivate;        //< backward compatibility: fires for both
	Event<>    WhenActionActivate;  //< Enter key in detailed view
	Event<>    WhenDoubleClickActivate; //< double-click in detailed view
	Event<const String&> WhenRename;
	Event<Point> WhenRightClick;    //< right-click on rows (not header)

	//< Set activation policy for this columns widget.
	void SetActivatePolicy(ExplorerModel::ActivatePolicy p);

private:
	void EndRename(bool accept);
	//< One display per row, for the same pointer reason as the icon cells.
	Vector<One<ExplorerRow>> row_cells;
	bool editing = false;
};

// A ColumnList subclass for icon view that supports drag rectangle selection.
struct ExplorerIconList : ColumnList {
	ExplorerIconList() {
		RowMode().MultiSelect().NoRoundSize();
	}

	// Drag rectangle selection state
	bool    drag_selecting = false;
	Point   drag_start;
	Rect    drag_rect;

	// Zoom event: fired on Ctrl+wheel. Positive zdelta = zoom in, negative = zoom out.
	Event<int> WhenZoom;
	// Right-click event: fires with (screen_position, item_index). item_index = -1 for background.
	Event<Point, int> WhenRightClick;

	virtual void LeftDown(Point p, dword keyflags) override;
	virtual void LeftUp(Point p, dword keyflags) override;
	virtual void MouseMove(Point p, dword keyflags) override;
	virtual void LeftDrag(Point p, dword keyflags) override;
	virtual void CancelMode() override;
	virtual void Paint(Draw& w) override;
	virtual bool Key(dword key, int count) override;
	virtual void MouseWheel(Point p, int zdelta, dword keyflags) override;
	virtual void RightDown(Point p, dword keyflags) override;
};

// A file panel: a small caption, and the listing in whichever view mode its model
// asks for. All state lives in ExplorerModel; this renders it and reports
// interaction.
//
// Two lists because ColumnList and FileList are separate hierarchies in this fork:
// FileList draws the detailed columns and knows how to rename in place, ColumnList
// in RowMode is the icon grid. Everything above the panel goes through the
// view-neutral accessors below, so a command does not have to know which one is
// showing.
struct ExplorerPanel : public ParentCtrl {
	LabelBox          caption;
	LocationBar       location;   //< this pane's path row: buttons + field + filter
	ExplorerColumns   columns;   //< detailed: one column per value, with headings
	ExplorerIconList  icons;     //< icon mode: one picture and a name per cell

	ExplorerPanel(const char *name);

	// This pane's row is asked to navigate or filter it. The owner resolves
	// which pane the panel is (tab or split) -- the panel itself does not know,
	// and must not: tabs re-index when one closes.
	Event<const String&> WhenPathEnter;
	Event<String>        WhenFilterText;

	// Puts this pane's path and filter text in its own row, without the field
	// taking the keyboard. A pane's row reflects that pane's model and nothing
	// else; activating the other pane does not copy paths between rows.
	void SyncLocation(const String& path, const String& filter_text) {
		location.SetPathText(path);
		location.filter.SetText(WString(filter_text));
	}
	void SetLocationShown(bool on)           { location.Show(on); }

	void Render(ExplorerModel& model);
	//< Move the keyboard focus to whichever list this panel is drawing. Switching
	//< panes without this leaves the focus in the pane the user just left, so the
	//< next key press goes somewhere they are no longer looking at.
	void FocusList();
	//< Entry name at a row, or empty.
	String   NameAt(int row) const;

	// --- view-neutral accessors ------------------------------------------------
	//
	// Selection, the cursor and the row count are asked for without naming a view.
	// A command that reached for p.list directly would silently act on a list that
	// is not even visible the moment icon mode is switched on.

	int  Count() const;
	int  Cursor() const;
	bool IsSelected(int row) const;
	void SelectOne(int row, bool select);
	void SetCursor(int row);
	//< True when the panel is drawing icons rather than columns.
	bool IsIconMode() const                  { return icon_mode; }
	//< The mode currently being drawn, or VIEW_LIST in the detailed mode.
	ViewMode GetViewMode() const             { return view_mode; }

	// ColumnList offers SelectOne but no whole-list verbs, and xfe's Edit and
	// Select menus expect all three.
	void SelectAll();
	void InvertSelection();
	void DeselectAll();

Event<int> WhenActivated;
	//< Any click in the listing, whatever the mode. The window uses it to decide
	//< which pane a command acts on.
	Event<> WhenPicked;

	// --- activation policy -----------------------------------------------------
	//
	// Single-click or double-click to open. Controlled by the model's setting.
	void SetActivatePolicy(ExplorerModel::ActivatePolicy p);
	ExplorerModel::ActivatePolicy GetActivatePolicy() const { return activation_policy; }

	// --- zoom ------------------------------------------------------------------
	//< Fired on Ctrl+wheel in icon mode. Positive = zoom in, negative = zoom out.
	Event<int> WhenZoom;

	// --- context menu ----------------------------------------------------------
	//< Fired on right-click with selection. Point is screen coordinates.
	Event<Point> WhenEntryContextMenu;
	//< Fired on right-click on background (no selection). Point is screen coordinates.
	Event<Point> WhenBackgroundContextMenu;

	bool Access(Visitor& v) override;        //< gui-user-gate TOON surface

private:
	void RenderIcons(ExplorerModel& model);
	//< One display per icon-mode row, kept alive for ColumnList, which stores a
	//< pointer rather than a copy.
	Vector<One<IconCell>> cells;
	//< A second ExplorerModel, for the split pane. A member rather than a pointer
	//< so the pane cannot exist without a model behind it.
	friend struct Explorer;
	//< Cells per row at the current width. Recomputed on every render and on
	//< layout, because the answer is a division by the panel's own width.
	int  IconColumns() const;

	bool     icon_mode = false;
	ViewMode view_mode = VIEW_LIST;
	int      icon_size = VIEW_ZOOM_DEFAULT;
	ExplorerModel::ActivatePolicy activation_policy = ExplorerModel::DOUBLE_CLICK;
};

// A pane that owns its own tabs, models, and location chrome.
// Both the main tabbed area and the split panel are instances of this.
// The split pane may have just one tab (the default), but the abstraction
// allows multiple tabs per pane if needed.
struct ExplorerPane {
	TabCtrl                tabs;        //< tab strip + content panes (slaves)
	Vector<ExplorerModel>  models;      //< one model per tab
	Vector<ExplorerPanel*> panels;      //< one panel per tab (slaves of tabs)

	ExplorerPane() = default;

	// Adds a new tab with the given path, makes it active. Returns the tab index.
	int NewTab(const String& path, const String& name = "Files");
	// Closes a tab. If it's the last tab, returns false (caller should close window).
	bool CloseTab(int tab);
	// Removes all tabs except the active one. Returns the kept tab index.
	int RemoveAllTabs();
	// Gets the active tab index.
	int ActiveTab() const                { return tabs.Get(); }
	// Gets the number of tabs.
	int GetTabCount() const              { return models.GetCount(); }
	// Gets the model for the active tab.
	ExplorerModel& ActiveModel();
	// Gets the panel for the active tab.
	ExplorerPanel& ActivePanel();
	// Renders the active tab's model into its panel.
	void RenderActive();
	// Syncs the active tab's location chrome.
	void SyncLocationActive();
	// Focuses the active tab's listing.
	void FocusListActive();

	// Called when the active tab changes.
	Event<> WhenTabChange;

	// Wire a panel's verbs (called for each new panel).
	void WirePanel(ExplorerPanel& p, std::function<void(const String&)> on_path_enter,
	               std::function<void(String)> on_filter_text,
	               std::function<void()> on_activated,
	               std::function<void(int)> on_sort,
	               std::function<void()> on_column_menu,
	               std::function<void()> on_picked,
	               std::function<void(const String&)> on_rename);

private:
	// Creates a new ExplorerPanel and adds it as a tab slave.
	ExplorerPanel* CreatePanel(const String& name);
};

// The 98 status strip's cells: a raised panel around each one.
//
// StatusBar draws its cells' *text* and its own top edge, but a panel around a
// cell is not in any style -- StatusBar::Style has one `Value look` for the whole
// strip, and InfoCtrl's own Left/Right frames install a single frame for the
// whole control, so a strip with two panels in it has to draw them. Which is
// what this does, from the cell geometry InfoCtrl already computes and publishes
// (GetTabOffset), so the panels follow the cells rather than guessing at them: a
// panel drawn from a hand-kept copy of the widths is a panel that is one
// relayout behind its text.
//
// Nothing is invented here about *where* the cells are. InfoCtrl::Paint walks
// `tab` with the same arithmetic this uses, and it is that walk, not this, that
// decides the text lands inside the panel.
struct ExplorerStatusCells : public CtrlFrame {
	explicit ExplorerStatusCells(InfoCtrl& bar) : bar(bar) {}

	// Neither FrameLayout nor FrameAddSize changes anything, and that is the point.
	// A frame's view is what is *left* of the rect after its FrameLayout runs, and
	// the frame is painted with the view left by the one before it. So a frame that
	// insets nothing draws over the whole strip without taking any of it away --
	// and, because it leaves the running rect alone, the size grip behind it still
	// gets the view it would have got. A frame that claimed a rect here would take
	// the grip's corner.
	//
	// FrameAdd and FrameRemove are deliberately *not* overridden. Ctrl::AddFrame()
	// already calls FrameAdd() on the frame it is given, so an override that calls
	// parent.AddFrame(*this) back is infinite recursion, not parenting. (The
	// AddressBar frames override it because they add themselves as a *child Ctrl*,
	// which is a different call.)
	void FrameLayout(Rect&) override         {}
	void FrameAddSize(Size&) override         {}
	void FramePaint(Draw& w, const Rect& r) override;

	InfoCtrl& bar;

	//< Evidence that the frame is in the paint path at all, and that it sees the
	//< geometry it expects. --dump-ui reports these. A frame that was added but
	//< never reached by the paint loop draws nothing and reports nothing, which
	//< is the failure mode that is invisible in the widget tree: the strip is
	//< there, its cells are there, and the panels are simply not.
	int  paints = 0;
	Rect last_paint;

	//< The gap inside a panel, so the text is not touching its border. And
	//< InfoCtrl's own left margin, which is a literal 2 in InfoCtrl::Paint and
	//< has no accessor: the first cell starts there, not at the frame's edge.
	enum { CELL_PAD = 4, CELL_MARGIN = 2 };
};

// Main window: composition and wiring only. Every listing decision and every
// authorization decision belongs to the headless layer.
class Explorer : public TopWindow {
	MenuBar     menu;
	// One ToolBar for the three button groups (nav, tool, view), in xfe's
	// order, separated where the groups meet. The groups are no longer ctrls
	// of their own: the pane wraps items across rows by itself, so compact --
	// groups sharing rows, wrapping what does not fit -- is what a single bar
	// gives, and the per-group toggles decide which group's *items* the fill
	// puts in. The location group stays its own frame: it is two rows tall
	// (breadcrumb + field) and cannot merge into a button row at all.
	ToolBar     all_bars;
	//< The other layout policy: every shown group on a row of its own (the
	//< xfe/Win98 look), done with a Bar::Break between the groups. Off by
	//< default; a View > Toolbars option.
	bool        bars_rows_mode = false;
	//< A bar is only as tall as its tallest button, and an empty bar is zero tall,
	//< so a group with nothing in it takes no room rather than a blank row.
	//< Four groups, in xfe's order and all four independently switchable. The
	//< index is the group number, and the last one is the location bar rather than
	//< a ToolBar, so it cannot go in the same array as the other three: a
	//< FrameCtrl has no ButtonKind and no Set(bar filler).
	bool        bars_shown[4] = { true, true, true, true };

	//< nav | panes_split
	Splitter    split;
	//< main_pane tabs | split_pane tabs. The split pane is an ExplorerPane that
	//< may have its own tabs (default 1), not a lone panel.
	Splitter    panes_split;
	ExplorerPane main_pane;
	ExplorerPane split_pane;
	ExplorerNav nav;

	StatusBar   status;
	//< The panels drawn around the status strip's cells. A member, not a temporary:
	//< a frame is used by address, and Ctrl::~Ctrl frees the frame list without
	//< calling FrameRemove, so a frame member outliving its Ctrl is safe here.
	ExplorerStatusCells status_cells;

	String filter_text;
	String home_path;      //< Resolved once at startup; empty when there is no home directory
	bool  split_sized = false;   //< divider applied once the splitter has a real width
	//< Set once the frames exist. The first OpenPath() happens in the constructor
	//< before the menu and toolbar are added, and posting to a frame that has not
	//< been added yet is how an empty toolbar comes about.
	bool  frames_ready = false;
	//< --dump-ui: print the widget tree once the window has been laid out and
	//< close. KWin on this machine refuses synthetic input, so the widget tree is
	//< the only part of the window a run can check without a human looking at it.
	bool  ui_dump = false, ui_dumped = false;
	//< What the constructor's Remove all tabs probe found, for the dump to print.
	//< Empty in an ordinary run, where nothing is probed.
	String tab_probe;
	//< The same for the Synchronize panels probe.
	String sync_probe;

	//< The pane a command acts on: 0 is main_pane, 1 is split_pane.
	//< A click in either one makes it the pane, because a file manager that acts
	//< on the left pane while the user is clicking in the right one is a bug that
	//< deletes the wrong thing.
	int  pane = 0;
	bool split_on = false;
	//< Which arrangement the split shows: sideways (a vertical divider) or
	//< stacked. Remembered while the window lives, so "Two panels" comes back
	//< in the arrangement it was left in; the View bar's two direction items
	//< set it, and clicking the active one dismisses the split.
	bool split_dir = true;
	//< Synchronize panels, off by default. A navigation in one pane then shows the
	//< same directory in the other; Back, Forward and Up never do, so each model
	//< keeps its own history. Reading (1) -- one path *and* one history -- would
	//< make the split pane useless for comparing two directories, which is what a
	//< split pane is for, and would leave "Back" ambiguous: back from where?
	//< A window member rather than a model field, so it survives the split being
	//< turned off and on again.
	bool sync_panels = false;
	void SetSplit(bool sideways);
	//< The one bar's fill: the shown groups in xfe's order, gaps between them
	//< (separators in compact, line breaks in one-row-per-group).
	void CombinedBars(Bar& bar);
	//< Re-layout, but out of the current event: popup-menu verbs re-laying
	//< frames inside the menu's own dispatch took the GTK input with them.
	void RelayoutSoon();
	//< The split goes away and the tabbed listing returns to the main splitter.
	//< Its own verb because SetSplit names arrangements, not an on/off state --
	//< the old wiring made "stacked" do this job under a name that promised an
	//< arrangement nobody could reach.
	void SetSplitOff();

	//< The open tabs in the active pane, in menu order.
	int  GetTabCount();
	//< The active tab index in the active pane.
	int  ActiveTab();
	//< Attach a panel's verbs. Every panel gets the same wiring, and every verb
	//< is expressed against the *active* tab, so a command never has to know
	//< which tab's panel it was pressed in.
	void WirePanel(ExplorerPanel& p);
	//< The right-click menu on the column headings: show or hide each column, and
	//< the dialog that does the same thing in one place.
	void ShowColumnMenu();
	void EditColumns();
	//< Context menus for listing: selected entries vs background
	void ShowEntryContextMenu(Point screen_pos);
	void ShowBackgroundContextMenu(Point screen_pos);
	//< Sort the active tab by a visible column, or by nothing.
	void SortByColumn(int visible_index);
	//< Settings modal dialog
	void ShowSettings();
	//< How many places panels the sidebar has, and whether the tree is beside
	//< them. See ExplorerNav::Mode.
	void SetPanelMode(ExplorerNav::Mode m);
	//< The Go verb, shared by the button, Enter in the field and the drop-list.
	void GoToTypedPath();

	// --- the clipboard --------------------------------------------------------
	//< Names and where they came from, not paths: a name pasted into another
	//< directory is the same entry. Cleared by a successful move-paste.
	Vector<String> clip_names;
	String  clip_path;
	bool    clip_move = false;
	void    CopySelected(bool move);
	void    PasteClipboard();

	// --- the second toolbar group ---------------------------------------------
	void NewWindow();
	void NewRootWindow();
	void RunCommand();
	void RunTerminal();
	void FindNames(bool dirs);
	void FindFiles()                               { FindNames(false); }
	void FindDirs()                                { FindNames(true); }
	//< Mount another directory at `at`, and unmount a path. Both report through
	//< `error` rather than prompting: the command above decides how to say it.
	bool AttachDirectory(const String& at, const String& source, String& error);
	bool DetachDirectory(const String& path, String& error);
	void AttachPath();
	void DetachMount();
	ExplorerModel& ActiveModel();
	ExplorerPanel& ActivePanel();

	void OpenPath(const String& path);
	void OpenPathAt(int tab, const String& path);
	//< The one navigation funnel. `target` is the pane to navigate: 0 the main
	//< pane, 1 the split pane. Every navigation in the window goes through
	//< here, which is the only way the synchronize-panels rule can be true of all
	//< of them -- a click, the Go menu, the address bar, the breadcrumb, a bookmark
	//< and a sidebar row are six routes, and a rule applied in five of them is a rule
	//< that surprises someone.
	//< `follow` is false for Back, Forward and Up, which walk one pane's history
	//< and must not drag the other pane with them -- that is the whole difference
	//< between a shared path and a shared session.
	void NavigateTo(int target, const String& path, bool follow = true);
	//< The breadcrumb, the address bar, the filter field, the places panel, the
	//< history list, the tree and the status cells, all from one model. Seven things
	//< that are one decision; a sixth place that updates the breadcrumb is a
	//< breadcrumb that disagrees with the address bar.
	void SyncChrome(ExplorerPanel& p, ExplorerModel& m);
	void GoToTypedPath(ExplorerPanel& p);
	void ApplyFilter(ExplorerPanel& p, const String& text);
	//< Make the other pane the one commands act on, and move the focus with it.
	void SwitchPane();
	void ToggleSyncPanels();
	//< Adds a tab showing `path` in the active pane, and makes it the active one. Returns the new tab.
	int  NewTab(const String& path);
	//< Closes a tab in the active pane. The last remaining tab closes the window.
	void CloseTab(int tab);
	//< Closes every tab but the active one in the active pane, and keeps the window.
	void RemoveAllTabs();
	void ActivateSelection();
	void ApplyFilter(const String& text);
	void UpdateStatus();
	void UpdateHistoryButtons();
	void ShowLog();
	void RefreshNav();
	void ToggleHidden();
	//< Bookmarks: the current directory, or the one selected in the sidebar.
	void AddBookmark();
	void RemoveBookmark();
	void AddBookmark(const String& path);

	// Selected rows, in listing order. The cursor row is included when nothing is
	// selected, because "act on what I am pointing at" is what a single click
	// means; an explicit multi-selection is honoured as given.
	Vector<int>    SelectedRows();
	Vector<String> SelectedNames();

	// Asks for a name. Returns false when the user cancels or submits nothing.
	bool AskName(const char *title, const char *initial, String& value);
	void NewFolder();
	void NewFile();
	void RenameSelected();
	void DeleteSelected();
	void TransferSelected(bool move);
	//< Re-reads the panel and the status bar, then re-evaluates which commands are
	//< available: an operation that empties the selection or hits a read-only mount
	//< has to change what the menu offers, not just what it shows.
	void AfterOperation();
	void SelectAll();
	void InvertSelection();
	void DeselectAll();
	//< The selected names on the *system* clipboard, one per line. Deliberately
	//< not the internal clipboard: that one holds entries to be pasted as files,
	//< and a list of names in it would paste files that do not exist. See the
	//< "Clipboards" section of AGENTS.md.
	void CopyNames();
	//< Switches the listing between detailed, big icons and small icons. Per tab,
	//< because it lives in the model with the sort order.
	void SetViewMode(ViewMode mode);
	//< Icon edge length for the active tab, in the model's zoom levels.
	void SetZoom(int pixels);
	void ZoomIn();
	void ZoomOut();
	//< Sorts the active model and re-renders. Clicking the active key again flips
	//< the direction, which is what a repeated click on a column header means.
	void SetSort(ExplorerModel::SortBy by);
	void ToggleSortDir();
	//< The two sort toggles, per tab like the sort itself: directories before files,
	//< and case ignored or not. Both re-render the active pane, because the order the
	//< rows are in is the only thing they change.
	void ToggleDirsFirst();
	void ToggleIgnoreCase();
	//< Go > Go to folder: drops the caret in the address bar with the current path
	//< selected, ready to be typed over.
	void FocusLocationBar();
	//< File > Properties on the single selected entry.
	void ShowProperties();
	//< File > Create symlink: creates a symbolic link
	void CreateSymlink();
	//< Tools > Empty Trashbin: empties the trash bin
	void EmptyTrashbin();
	//< Help > Keyboard shortcuts: every menu item that has one, read out of the
	//< live menu rather than out of a table written beside it.
	void ShowShortcuts();
	//< Help > Contents, F1: the manual in one scrolled QTF page.
	void ShowHelp();
	//< Empty when editing is possible, otherwise the reason to show in place of a
	//< working command. Keeping this in one place is what stops the toolbar and the
	//< menu from disagreeing about whether an operation is available.
	String ModifyBlockReason();

public:
	typedef Explorer CLASSNAME;
	//< initial_path is opened instead of the working directory when not empty.
	//< dump_ui prints the widget tree after the first layout and closes.
	//< initial_view is the listing mode the first tab opens in; the view is per tab
	//< afterwards, but there is nothing to change it yet at construction time.
	//< folders_first and ignore_case are the same idea for the two sort toggles, and
	//< exist so that --dump-ui can open a window in a chosen sort state: a toggle
	//< that can only be checked in the state the window happens to start in is a
	//< toggle whose off state no run can verify.
	Explorer(const String& initial_path = Null, bool dump_ui = false,
	         ViewMode initial_view = VIEW_LIST, bool folders_first = true,
	         bool ignore_case = false);

	void Menu(Bar& bar);
	void Paint(Draw& w) override;
	// The four groups. One builder each, rather than one builder that fills every
	// bar: each group is rebuilt on its own when only its own enable state has
	// changed, and a group whose buttons all belong to another group would end up
	// rebuilt four times over for one changed button.
	void NavBar(Bar& bar);
	void ToolBar_(Bar& bar);
	void ViewBar(Bar& bar);
	//< Repost every group's builder. The enable state of a button depends on the
	//< model and on the selection, so every navigation has to recompute it.
	void UpdateBars();
	//< The one place that says where the window is. Both rows go through it,
	//< because two rows that are updated from different places are two rows that
	//< will eventually disagree -- and a breadcrumb saying /home while the field
	//< says /home/user is a window that cannot be trusted about either.
	//< Show or hide a whole group, for Show > Toolbars.
	void SetBarShown(int group, bool on);
	// The toolbar layout (which groups, stacked rows or compact) is the user's
	// configuration: loaded before the bars are first filled, saved on change,
	// in the app config folder next to the bookmarks.
	void SaveToolbarConf() const;
	void LoadToolbarConf();
	bool IsBarShown(int group) const          { return bars_shown[group]; }
	//< Keyboard verbs for the commands that have no other route to them. The list
	//< handles Enter itself; delete, select-all and invert are handled here so they
	//< work no matter which panel has the focus.
	bool Key(dword key, int count) override;

	//< The gui-user-gate surface (ai/docs/gates/gui-user-gate.md): the live
	//< window as TOON. The menu bar, the toolbars and the composites below all
	//< expose what the user can reach, and only what is *shown*: a hidden
	//< toolbar group or a hidden sidebar must not look like a reachable one.
	//< The gate flags in main.cpp run this surface structurally (--gui-toon,
	//< --gui-access-gate), rendered (--gui-render-gate) and as a planner
	//< drive-through (--gui-llm-gate).
	bool Access(Visitor& v) override;

	//< The rendered half of the gate (visibility, frame overlaps, contrast) and
	//< the named window state its structural half walks through. Members rather
	//< than free helpers because the frames they check are private; ExplorerGate
	//< is the only consumer.
	void CollectGateRenderFailures(ValueArray& failures);
	void GateApplyState(const String& name);

	//< Gives the policy a confirmation channel. The headless twin has none, so
	//< there anything needing the user resolves to DENY.
	void WirePolicyConfirmation();
};

END_UPP_NAMESPACE

#endif // flagGUI

#endif