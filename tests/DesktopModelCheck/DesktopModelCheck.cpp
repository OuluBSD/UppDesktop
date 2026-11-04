#include <Core/Core.h>
#include <DesktopModel/DesktopModel.h>

// Gate check for the three models of uppsrc/DesktopModel, run with no display,
// no GUI flag and no event loop:
//
//	bin/build -m CLANG -M HEADLESS DesktopModelCheck && ./bin/CLANG/DesktopModelCheck
//
// It prints with printf rather than LOG on purpose: the models must stay silent,
// and LOG does not reliably reach stdout in these builds.
//
// DM2-016 added the reachability proof for DM2-005, DM2-006 and DM2-007.
// DM2-013 added CheckRegressions(): the four defects that were found in the GUI
// packages and were invisible until each had a check, plus what the rewire must
// not break. The three per-view probes under upptst/ (DesktopManagerProbe,
// TaskBarProbe, DisplayManagerProbe) cover the same models from the view's side.

using namespace Upp;

namespace Upp { extern bool NoMemoryLeaksCheck; }

static int failures = 0;
static int checks = 0;

//! Report one check and return whether it passed, so a caller can stop a loop on
//! the first failure instead of running every step after it.
static bool Report(bool ok, const char* what, const String& detail)
{
	checks++;
	// State is printed only when a check fails: the point of the detail is to
	// explain the failure, and printing it on every line buries the report.
	if(ok)
		printf("%-5s %s\n", "ok", what);
	else {
		printf("%-5s %s\n     got: %s\n", "FAIL", what, detail.Begin());
		failures++;
	}
	return ok;
}

static void CheckWorkspace()
{
	printf("\n-- WorkspaceModel: create, remove, switch\n");

	WorkspaceModel ws;
	Report(ws.GetCount() == 0 && ws.Current() == -1, "empty model", ws.ToString());

	WorkspaceSwitch none = ws.SwitchTo(0);
	Report(!none.changed, "switch on an empty model is a no-op", ws.ToString());

	int first = ws.CreateWorkspace();
	Report(first == 0 && ws.Current() == 0, "the first workspace becomes current", ws.ToString());

	ws.CreateWorkspace();
	ws.CreateWorkspace();
	Report(ws.GetCount() == 3, "three workspaces", ws.ToString());

	WorkspaceSwitch to2 = ws.SwitchTo(2);
	Report(to2.changed && to2.from == 0 && to2.to == 2, "switch reports leaving and arriving", ws.ToString());

	WorkspaceSwitch same = ws.SwitchTo(2);
	Report(!same.changed && same.to == -1, "switching to the current one changes nothing", ws.ToString());

	// Past either end is a no-op, as NextWorkspace()/PrevWorkspace() behave today.
	WorkspaceSwitch past = ws.SwitchToRelative(1);
	Report(!past.changed, "step past the last workspace does not wrap", ws.ToString());
	ws.SwitchTo(0);
	WorkspaceSwitch before = ws.SwitchToRelative(-1);
	Report(!before.changed, "step before the first workspace does not wrap", ws.ToString());

	// The defect this model fixes: removing the workspace that is current while
	// it is the last one used to leave Current() one past the end of the list.
	ws.SwitchTo(2);
	Report(ws.RemoveWorkspace(2), "remove the current last workspace", ws.ToString());
	Report(ws.Current() >= 0 && ws.Current() < ws.GetCount(),
	       "current index stays inside the shorter list", ws.ToString());

	// Removing a workspace before the current one shifts the current index down
	// so it still names the same workspace.
	ws.SwitchTo(1);
	Report(ws.Current() == 1, "on workspace 1 of two", ws.ToString());
	ws.RemoveWorkspace(0);
	Report(ws.Current() == 0 && ws.GetCount() == 1, "removing an earlier workspace keeps the current one", ws.ToString());

	Report(!ws.RemoveWorkspace(7), "removing an out-of-range workspace is refused", ws.ToString());
	Report(!ws.RemoveWorkspace(0), "the last workspace is never removed", ws.ToString());
	Report(ws.GetCount() == 1, "one workspace survives", ws.ToString());
}

static WindowEntry Entry(int handle, const char* title, bool active = false,
                         int desktop = WindowSource::AllDesktops)
{
	WindowEntry entry;
	entry.handle = handle;
	entry.title = title;
	entry.active = active;
	entry.desktop = desktop;
	return entry;
}

static void SyncThree(WindowListModel& model, bool middle_active)
{
	Vector<WindowEntry> seen;
	seen.Add(Entry(11, "Terminal"));
	seen.Add(Entry(22, "Editor", middle_active));
	seen.Add(Entry(33, "Browser"));
	model.Sync(seen);
}

//! What a view does after raising a window: read the list again and report the
//! new focus. Cycling itself is a query, so this is how a caller walks the cycle.
static void FocusOnly(WindowListModel& model, int handle)
{
	Vector<WindowEntry> seen;
	for(int i = 0; i < model.GetCount(); i++)
		seen.Add(Entry(model.HandleAt(i), model.TitleAt(i), model.HandleAt(i) == handle));
	model.Sync(seen);
}

static void CheckWindowList()
{
	printf("\n-- WindowListModel: order and focus cycling\n");

	WindowListModel model;
	Report(model.IsEmpty() && model.NextFocus() == WindowListModel::NoHandle,
	       "an empty list has nothing to focus", model.ToString());
	Report(model.NextIndex() == -1, "an empty list has no next index", model.ToString());

	SyncThree(model, false);
	Report(model.GetCount() == 3, "three windows listed", model.ToString());
	// The order is the one the source handed in, and it is inspectable.
	Report(model.HandleAt(0) == 11 && model.HandleAt(1) == 22 && model.HandleAt(2) == 33,
	       "the order is the source order, exposed", model.ToString());

	// With nothing focused, forwards starts at the first entry and backwards at
	// the last. The old code defined only the first of the two.
	Report(model.NextFocus(1) == 11, "nothing focused, forward lands on the first", model.ToString());
	Report(model.NextFocus(-1) == 33, "nothing focused, backward lands on the last", model.ToString());

	model.Clear();
	Vector<WindowEntry> one;
	one.Add(Entry(7, "Only"));
	model.Sync(one);
	Report(model.NextFocus(1) == 7 && model.NextFocus(-1) == 7,
	       "a single window cycles onto itself in both directions", model.ToString());

	model.Clear();
	SyncThree(model, false);
	Report(model.NextFocus(1) == 11, "forward with nothing focused lands on the first", model.ToString());
	FocusOnly(model, 11);
	Report(model.NextFocus(1) == 22, "forward steps off the focused window", model.ToString());
	FocusOnly(model, 22);
	Report(model.NextFocus(1) == 33, "forward keeps stepping through the list", model.ToString());
	FocusOnly(model, 33);
	Report(model.NextFocus(1) == 11, "forward wraps around the list", model.ToString());
	FocusOnly(model, 11);
	Report(model.NextFocus(-1) == 33, "backward wraps the other way", model.ToString());
	FocusOnly(model, 33);
	Report(model.NextFocus(-1) == 22, "backward steps off the focused window", model.ToString());

	// Cycling continues from the window the source reported as focused.
	model.Clear();
	SyncThree(model, true);
	Report(model.ActiveIndex() == 1 && model.ActiveHandle() == 22,
	       "the focused window is the one the source marked", model.ToString());
	Report(model.NextFocus(1) == 33, "forward leaves the focused window", model.ToString());
	Report(model.NextFocus(-1) == 11, "backward leaves it the other way", model.ToString());

	// The model does not mark the entry it returned: X focus is the source's
	// report, and a model that guessed would be claiming a raise it never made.
	Report(model.ActiveHandle() == 22, "cycling does not invent a focus change", model.ToString());

	// A window that keeps its handle holds its place, whatever order the source
	// listed this time, and a newly seen window is appended.
	Vector<WindowEntry> seen;
	seen.Add(Entry(33, "Browser renamed"));
	seen.Add(Entry(22, "Editor"));
	seen.Add(Entry(44, "Mail"));
	model.Sync(seen);
	Report(model.HandleAt(0) == 22 && model.HandleAt(1) == 33 && model.HandleAt(2) == 44,
	       "a kept window holds its place, a new one is appended", model.ToString());
	Report(model.TitleAt(1) == "Browser renamed", "titles are refreshed on sync", model.ToString());
	Report(model.ActiveHandle() == WindowListModel::NoHandle,
	       "a window that lost the focus no longer reports it", model.ToString());
	Report(model.HandleAt(9) == WindowListModel::NoHandle && model.TitleAt(9).IsEmpty(),
	       "an out-of-range entry has neither handle nor title", model.ToString());
}

//! Fake source, so the screen data can be reached with no X display at all
class FakeScreenSource : public ScreenSource
{
public:
	FakeScreenSource() {}

	Vector<ScreenMetrics> metrics;
	int  fail_on = -1;

	virtual int GetCount() override { return metrics.GetCount(); }

	virtual bool ReadScreen(int index, ScreenMetrics& out) override
	{
		if(index < 0 || index >= metrics.GetCount() || index == fail_on)
			return false;
		out = metrics[index];
		return true;
	}
};

//! The four defects that were invisible until these checks existed. Each one was
//! found in the GUI packages, where the only symptom was that nothing happened,
//! and each is reproduced here as the exact shape the old code had.
static void CheckRegressions()
{
	printf("\n-- Regressions: the four defects behind DM2-008 and DM2-009\n");

	// 1. FocusNext()/FocusPrev() did nothing when no window held the focus. The old
	// code searched for IsForeground(), found nothing, and used the -1 it started
	// from. Forwards it happened to work out; backwards it computed
	// (-1 - 1 + n) % n.
	{
		WindowListModel model;
		Vector<WindowEntry> seen;
		seen.Add(Entry(1, "A"));
		seen.Add(Entry(2, "B"));
		seen.Add(Entry(3, "C"));
		model.Sync(seen);
		Report(model.ActiveIndex() == -1, "nothing is focused to begin with", model.ToString());
		Report(model.NextFocus(1) == 1,
		       "defect 1: forward with no focus answers, it does nothing", model.ToString());
		Report(model.NextFocus(-1) == 3,
		       "defect 1: backward with no focus answers too", model.ToString());

		// Two entries is where the old arithmetic was most obviously wrong:
		// (-1 - 1 + 2) % 2 == 0, which is the first entry, not the last.
		WindowListModel two;
		Vector<WindowEntry> pair;
		pair.Add(Entry(4, "A"));
		pair.Add(Entry(5, "B"));
		two.Sync(pair);
		Report(two.NextFocus(-1) == 5,
		       "defect 1: backward with no focus on two windows lands on the last",
		       two.ToString());

		// A single window is the degenerate case of both ends.
		WindowListModel one;
		Vector<WindowEntry> single;
		single.Add(Entry(6, "Only"));
		one.Sync(single);
		Report(one.NextFocus(1) == 6 && one.NextFocus(-1) == 6,
		       "defect 1: one window is the answer in both directions", one.ToString());
	}

	// 2. TaskBar::CycleWindows() had an early return that made the whole method a
	// no-op for a single listed window, so with one application on screen the
	// button did nothing when it was pressed.
	{
		WindowListModel model;
		Vector<WindowEntry> single;
		single.Add(Entry(9, "Only"));
		model.Sync(single);
		Report(model.NextIndex(1) == 0,
		       "defect 2: the single-window fast path answers instead of returning",
		       model.ToString());

		model.Clear();
		Report(model.NextIndex(1) == -1 && model.NextFocus(1) == WindowListModel::NoHandle,
		       "defect 2: an empty list still has nothing to answer with", model.ToString());
	}

	// 3. DesktopManager::RemoveWorkspace() clamped current_workspace before the
	// removal, so removing the last workspace while it was current left the index
	// one past the end of the shorter list.
	{
		WorkspaceModel ws(3);
		ws.SwitchTo(2);
		Report(ws.Current() == 2 && ws.GetCount() == 3, "on the last of three", ws.ToString());
		ws.RemoveWorkspace(2);
		Report(ws.GetCount() == 2 && ws.Current() == 1,
		       "defect 3: the index lands on the new last, not one past it", ws.ToString());
		// And the consequence that followed from it: the next switch request was
		// out of range for a reason no caller could see.
		WorkspaceSwitch next = ws.SwitchToRelative(1);
		Report(!next.changed && ws.Current() == 1,
		       "defect 3: the next switch is a plain no-op at the end", ws.ToString());
		Report(ws.IsValid(ws.Current()), "defect 3: current always names a workspace",
		       ws.ToString());

		// Repeated removal from the end must not drift either.
		WorkspaceModel walk(4);
		for(int i = 0; i < 3; i++) {
			walk.SwitchTo(walk.GetCount() - 1);
			walk.RemoveWorkspace(walk.Current());
			if(!Report(walk.Current() >= 0 && walk.Current() < walk.GetCount(),
			           "defect 3: repeated removal keeps current inside the list",
			           walk.ToString()))
				break;
		}
	}

	// 4. Backward cycling with nothing focused landed on the second-to-last entry.
	// Reproduced with three entries, where the old formula gave index 1.
	{
		WindowListModel model;
		Vector<WindowEntry> seen;
		seen.Add(Entry(1, "A"));
		seen.Add(Entry(2, "B"));
		seen.Add(Entry(3, "C"));
		model.Sync(seen);
		int landed = model.NextIndex(-1);
		Report(landed == 2,
		       "defect 4: backward with no focus lands on the last, not the second-to-last",
		       Format("index %d of %s", landed, (const char *)model.ToString()));

		// Four entries: the old formula gave index 2, the new one index 3.
		Vector<WindowEntry> four;
		four.Add(Entry(1, "A"));
		four.Add(Entry(2, "B"));
		four.Add(Entry(3, "C"));
		four.Add(Entry(4, "D"));
		model.Sync(four);
		Report(model.NextIndex(-1) == 3,
		       "defect 4: the same holds with four windows", model.ToString());
	}

	// What the rewire must not break: the order survives a Sync, whatever the
	// source reports, and a window that stayed keeps its place.
	printf("\n-- Regressions: what the rewire must not break\n");
	{
		WindowListModel model;
		SyncThree(model, false);

		Vector<WindowEntry> rotated;
		rotated.Add(Entry(22, "Editor"));
		rotated.Add(Entry(33, "Browser"));
		rotated.Add(Entry(11, "Terminal"));
		model.Sync(rotated);
		model.Sync(rotated);
		Report(model.HandleAt(0) == 11 && model.HandleAt(1) == 22 && model.HandleAt(2) == 33,
		       "the order is unchanged by a source that reports a different order",
		       model.ToString());

		SyncThree(model, true);
		Report(model.HandleAt(0) == 11 && model.HandleAt(2) == 33,
		       "the order is unchanged when only the focus moved", model.ToString());
		Report(model.ActiveIndex() == 1, "and the focus still follows the source",
		       model.ToString());
	}

	// SetTaskbarHeight and SetPanelHeight must recompute the work area of every
	// screen, not only the first one.
	{
		FakeScreenSource source;
		ScreenMetrics a;
		a.x = 0; a.y = 0; a.width = 1280; a.height = 800; a.width_mm = 338; a.height_mm = 212;
		ScreenMetrics b;
		b.x = 1280; b.y = 0; b.width = 1920; b.height = 1080; b.width_mm = 508; b.height_mm = 286;
		source.metrics.Add(a);
		source.metrics.Add(b);

		ScreenInfoModel screens;
		screens.Update(source);
		screens.SetTaskbarHeight(40);
		screens.SetPanelHeight(24);
		Report(screens.WorkArea(0) == Rect(0, 24, 1280, 760) &&
		       screens.WorkArea(1) == Rect(1280, 24, 3200, 1040),
		       "both strips recompute the work area of every screen", screens.ToString());
		screens.SetTaskbarHeight(24);
		Report(screens.WorkArea(0) == Rect(0, 24, 1280, 776) &&
		       screens.WorkArea(1) == Rect(1280, 24, 3200, 1056),
		       "and again when a strip goes back down", screens.ToString());
	}
}

static void CheckScreenInfo()
{
	printf("\n-- ScreenInfoModel: fake source, no display\n");

	FakeScreenSource source;
	ScreenMetrics small_screen;
	small_screen.x = 0; small_screen.y = 0; small_screen.width = 1280; small_screen.height = 800;
	small_screen.width_mm = 338; small_screen.height_mm = 212;
	ScreenMetrics second;
	second.x = 1280; second.y = 0; second.width = 1920; second.height = 1080;
	// A virtual machine reporting no physical size: the model must answer 0
	// rather than invent a DPI.
	second.width_mm = 0; second.height_mm = 0;
	source.metrics.Add(small_screen);
	source.metrics.Add(second);

	ScreenInfoModel screens;
	Report(screens.GetCount() == 0, "a model that was never updated has no screens", screens.ToString());

	Report(screens.Update(source), "the source is read with no display open", screens.ToString());
	Report(screens.GetCount() == 2, "two screens", screens.ToString());
	Report(screens.Geometry(0) == Rect(0, 0, 1280, 800), "screen 0 geometry", screens.ToString());
	Report(screens.Geometry(1) == Rect(1280, 0, 3200, 1080), "screen 1 geometry is absolute", screens.ToString());
	Report(screens.Geometry(9) == Rect(), "an out-of-range screen has no geometry", screens.ToString());

	int dpi_x = 0, dpi_y = 0;
	screens.DPI(0, dpi_x, dpi_y);
	Report(dpi_x == 96 && dpi_y == 96, "DPI follows the reported physical size", Format("%d x %d", dpi_x, dpi_y));
	Report(screens.DPI(1) == 0 && screens.DPI(9) == 0, "no physical size means DPI 0", screens.ToString());

	// The work area is the screen minus the panel on top and the taskbar below.
	Report(screens.WorkArea(0) == Rect(0, 0, 1280, 776), "work area with a 24px taskbar", screens.ToString());
	screens.SetPanelHeight(30);
	Report(screens.WorkArea(0) == Rect(0, 30, 1280, 776), "work area follows the panel", screens.ToString());
	screens.SetTaskbarHeight(900);
	Report(screens.WorkArea(0).GetHeight() == 0, "strips taller than the screen give an empty work area", screens.ToString());
	screens.SetTaskbarHeight(24);

	// A changed screen is re-read in place, and the work area follows.
	ScreenMetrics resized = small_screen;
	resized.width = 2560;
	source.metrics[0] = resized;
	Report(screens.RefreshScreen(0, source), "screen 0 is re-read", screens.ToString());
	Report(screens.Geometry(0) == Rect(0, 0, 2560, 800), "the new geometry is stored", screens.ToString());
	Report(screens.WorkArea(0) == Rect(0, 30, 2560, 776), "the work area is recomputed", screens.ToString());
	Report(!screens.RefreshScreen(9, source), "an out-of-range screen is not re-read", screens.ToString());

	source.fail_on = 1;
	Report(!screens.RefreshScreen(1, source), "a source that fails reports it", screens.ToString());
	Report(screens.GetCount() == 2, "a failed re-read leaves the screen list alone", screens.ToString());
}

//! The per-workspace filter, DM5-001: the model answers for one workspace,
//! sticky windows pass every filter, AllDesktops passes everything
static void CheckWorkspaceFilter()
{
	printf("\n-- WindowListModel: per-workspace filter\n");

	WindowListModel model;
	Vector<WindowEntry> seen;
	seen.Add(Entry(11, "one", false, 0));
	seen.Add(Entry(22, "sticky", true));
	seen.Add(Entry(33, "two", false, 1));

	model.SetWorkspace(0);
	model.Sync(seen);
	Report(model.GetCount() == 2 && model.HandleAt(0) == 11 && model.HandleAt(1) == 22,
	       "workspace 0 lists its windows plus the sticky one, in server order",
	       model.ToString());
	Report(model.GetWorkspace() == 0, "the filter is the workspace that was set",
	       model.ToString());

	model.SetWorkspace(1);
	model.Sync(seen);
	Report(model.GetCount() == 2 && model.HandleAt(0) == 22 && model.HandleAt(1) == 33,
	       "after a switch the list is the new workspace in server order, with the "
	       "sticky window where the server put it, not where the old list had it",
	       model.ToString());

	model.SetWorkspace(7);
	model.Sync(seen);
	Report(model.GetCount() == 1 && model.HandleAt(0) == 22,
	       "a current index larger than any named workspace matches only the sticky "
	       "window -- no invented set, no clamping",
	       model.ToString());

	model.SetWorkspace(WindowSource::AllDesktops);
	model.Sync(seen);
	Report(model.GetCount() == 3, "AllDesktops passes everything", model.ToString());

	// Between switches the stable-order rule holds, and a window that changes
	// workspace leaves the list on the next sync.
	model.SetWorkspace(0);
	model.Sync(seen);
	Report(model.GetCount() == 2, "the filter keeps answering across further syncs",
	       model.ToString());
	seen.Clear();
	seen.Add(Entry(11, "one", false, 1));
	seen.Add(Entry(22, "sticky", true));
	model.Sync(seen);
	Report(model.GetCount() == 1 && model.HandleAt(0) == 22,
	       "a window that moved to another workspace leaves the list on the next sync",
	       model.ToString());

	// Cycling works on the filtered list: focus never lands on a window the
	// filter hides.
	seen.Clear();
	seen.Add(Entry(11, "one", false, 0));
	seen.Add(Entry(22, "sticky", true));
	seen.Add(Entry(44, "four", false, 0));
	model.Sync(seen);
	Report(model.NextFocus() == 11, "cycling within the filtered list stays inside it",
	       model.ToString());
	Report(model.NextFocus(3) == 22 && model.NextFocus(4) == 11,
	       "the cycle wraps over the sticky window like any other entry",
	       model.ToString());
}

//! The frame arithmetic DM6-003 froze as model-side: frame rect from client rect
//! and decoration insets, and the reverse. No display, no Xlib, so the geometry
//! the manager applies on screen is checked here rather than by a live run.
static void CheckFrameGeometry()
{
	printf("\n-- FrameGeometry: frame <-> client arithmetic\n");

	FrameInsets insets = FrameGeometry::DefaultInsets();
	Report(insets.left == 4 && insets.right == 4 && insets.top == 24 && insets.bottom == 4,
	       "default insets reserve a titlebar on top and a border on the other sides",
	       Format("%d,%d,%d,%d", insets.left, insets.right, insets.top, insets.bottom));
	Report(insets.Horz() == 8 && insets.Vert() == 28,
	       "total insets are the sums of the two axes", Format("%d,%d", insets.Horz(), insets.Vert()));

	Rect client(100, 200, 500, 400);
	Rect frame = FrameGeometry::ClientToFrame(client, insets);
	Report(frame == Rect(96, 176, 504, 404),
	       "client rect grows by the insets on every side",
	       Format("%d,%d,%d,%d", frame.left, frame.top, frame.right, frame.bottom));

	Rect back = FrameGeometry::FrameToClient(frame, insets);
	Report(back == client, "frame -> client is the exact inverse of client -> frame",
	       Format("%d,%d,%d,%d", back.left, back.top, back.right, back.bottom));

	// The size pair is the same arithmetic without a position, and the two
	// directions must agree with the rect form.
	Size frame_size = FrameGeometry::FrameSizeForClient(client.GetSize(), insets);
	Report(frame_size == frame.GetSize(),
	       "frame size matches the rect form for the same client",
	       Format("%d,%d", frame_size.cx, frame_size.cy));
	Report(FrameGeometry::ClientSizeInFrame(frame_size, insets) == client.GetSize(),
	       "client size in a frame is the inverse of frame size for a client",
	       Format("%d,%d", FrameGeometry::ClientSizeInFrame(frame_size, insets).cx,
	              FrameGeometry::ClientSizeInFrame(frame_size, insets).cy));

	// Where the manager places the client inside the frame: the top-left of the
	// client area is offset by the left and top insets, not by the border pair.
	Rect area = FrameGeometry::ClientRectInFrame(frame.TopLeft(), frame_size, insets);
	Report(area == client, "the client area inside the frame equals the client rect",
	       Format("%d,%d,%d,%d", area.left, area.top, area.right, area.bottom));

	// Zero insets leave the geometry alone, so an unframed client would map to
	// itself rather than to a rectangle one border smaller.
	FrameInsets none;
	Report(FrameGeometry::ClientToFrame(client, none) == client,
	       "zero insets leave the rect unchanged",
	       Format("%d,%d,%d,%d", FrameGeometry::ClientToFrame(client, none).left,
	              FrameGeometry::ClientToFrame(client, none).top,
	              FrameGeometry::ClientToFrame(client, none).right,
	              FrameGeometry::ClientToFrame(client, none).bottom));
}

CONSOLE_APP_MAIN
{
	printf("DesktopModel headless gate check (no display, no GUI flag)\n");

	CheckWorkspace();
	CheckWindowList();
	CheckWorkspaceFilter();
	CheckScreenInfo();
	CheckRegressions();
	CheckFrameGeometry();

	printf("\n%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "PASSED",
	       checks, failures);
	NoMemoryLeaksCheck = true;
	SetExitCode(failures ? 1 : 0);
}
