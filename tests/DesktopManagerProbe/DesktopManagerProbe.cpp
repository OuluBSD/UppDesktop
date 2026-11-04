#include <Core/Core.h>
#include <DesktopModel/DesktopModel.h>

// Headless twin of the DesktopManager view, task DM2-011 and DM2-012.
//
//	bin/build -m CLANG -M HEADLESS DesktopManagerProbe && ./bin/CLANG/DesktopManagerProbe
//
// It drives the same model calls that uppsrc/DesktopManager/DesktopManager.cpp
// drives, in the same order, and prints the state after each one, so the two
// paths can be read side by side. What the view adds on top -- the root window,
// one TopWindow per workspace, the handle-to-pointer table -- cannot exist here,
// because it needs a display. Everything the view asks the model is here.
//
// Printing is printf, not LOG: the models must stay silent, and LOG does not
// reliably reach stdout in these builds. A printed value that contradicts what
// the model was told sets the exit code, so this is a check and not only a dump.
//
// The workspaces probe prints the WorkspaceSwitch pair of every switch, because
// that pair is what the view acts on and nothing else reports it.

using namespace Upp;

static int failures = 0;

static void Report(bool ok, const char* what, const String& detail)
{
	if(ok)
		printf("%-5s %s\n", "ok", what);
	else {
		printf("%-5s %s\n     got: %s\n", "FAIL", what, detail.Begin());
		failures++;
	}
}

static void Show(const char* step, const WorkspaceModel& ws)
{
	printf("     %-34s %s\n", step, (const char *)ws.ToString());
}

static void Show(const char* step, const WindowListModel& windows)
{
	printf("     %-34s %s\n", step, (const char *)windows.ToString());
}

// The view's side of a Sync(): three fake windows, in the order
// Ctrl::GetTopCtrls() would have reported them. Handles start at 1 because
// WindowListModel::NoHandle is 0.
static Vector<WindowEntry> ThreeWindows(bool middle_active)
{
	Vector<WindowEntry> seen;
	WindowEntry a; a.handle = 1; a.title = "Desktop";    a.active = false;
	WindowEntry b; b.handle = 2; b.title = "Terminal";   b.active = middle_active;
	WindowEntry c; c.handle = 3; c.title = "(untitled)"; c.active = false;
	seen.Add(a);
	seen.Add(b);
	seen.Add(c);
	return seen;
}

static void ProbeWorkspaces()
{
	printf("\n-- DesktopManager workspaces: WorkspaceModel\n");

	// The view's Initialize() creates the root and then one workspace.
	WorkspaceModel ws;
	Show("fresh", ws);

	int first = ws.CreateWorkspace();
	Show("Initialize -> CreateWorkspace", ws);

	ws.CreateWorkspace();
	ws.CreateWorkspace();
	Show("CreateWorkspace twice more", ws);
	Report(first == 0 && ws.Current() == 0,
	       "the first workspace the view opened is current", ws.ToString());
	Report(ws.GetCount() == 3, "the model holds three workspaces", ws.ToString());

	// SetCurrentWorkspace(2): the view closes the window at from and opens the
	// one at to, and never recomputes either index.
	WorkspaceSwitch to2 = ws.SwitchTo(2);
	printf("     switch to 2                      changed %d from %d to %d\n",
	       (int)to2.changed, to2.from, to2.to);
	Show("SetCurrentWorkspace(2)", ws);
	Report(to2.changed && to2.from == 0 && to2.to == 2,
	       "the pair the view acts on is reported", ws.ToString());

	WorkspaceSwitch same = ws.SwitchTo(2);
	printf("     switch to 2 again                changed %d from %d to %d\n",
	       (int)same.changed, same.from, same.to);
	Report(!same.changed, "switching to the current workspace is a no-op", ws.ToString());

	// NextWorkspace() from the last workspace: no wrap, as before the split.
	WorkspaceSwitch past = ws.SwitchToRelative(1);
	printf("     NextWorkspace() at the end       changed %d from %d to %d\n",
	       (int)past.changed, past.from, past.to);
	Show("NextWorkspace at the end", ws);
	Report(!past.changed && ws.Current() == 2, "NextWorkspace does not wrap", ws.ToString());

	ws.SwitchTo(0);
	WorkspaceSwitch before = ws.SwitchToRelative(-1);
	printf("     PrevWorkspace() at the start     changed %d from %d to %d\n",
	       (int)before.changed, before.from, before.to);
	Report(!before.changed && ws.Current() == 0, "PrevWorkspace does not wrap", ws.ToString());

	// RemoveWorkspace(): the view passes the current index and does not clamp.
	// The clamp happens after the removal, which is the defect this fixes.
	ws.SwitchTo(2);
	int before_count = ws.GetCount();
	bool removed = ws.RemoveWorkspace(ws.Current());
	Show("RemoveWorkspace while on the last", ws);
	Report(removed && ws.GetCount() == before_count - 1,
	       "the last workspace was removed", ws.ToString());
	Report(ws.Current() == 1, "current stays inside the shorter list", ws.ToString());

	// The view refuses to take the last workspace, and so does the model.
	ws.RemoveWorkspace(1);
	Report(ws.GetCount() == 1, "one workspace is left", ws.ToString());
	Report(!ws.RemoveWorkspace(0), "the last workspace cannot be removed", ws.ToString());
	Show("after removing down to one", ws);
}

static void ProbeWindowList()
{
	printf("\n-- DesktopManager window list and focus cycling: WindowListModel\n");

	// Paint() and CycleFocus() both call SyncWindows() first, so the model is
	// never read stale.
	WindowListModel windows;
	Show("fresh", windows);

	windows.Sync(ThreeWindows(false));
	Show("Sync, nothing foreground", windows);
	Report(windows.GetCount() == 3, "three windows listed", windows.ToString());

	int forward = windows.NextFocus(1);
	printf("     FocusNext() from no focus       handle %d\n", forward);
	Report(forward == 1, "forwards from no focus lands on the first window",
	       windows.ToString());

	int backward = windows.NextFocus(-1);
	printf("     FocusPrev() from no focus       handle %d\n", backward);
	Report(backward == 3, "backwards from no focus lands on the last window",
	       windows.ToString());

	// One of them takes the focus; the next Sync brings the real flag back,
	// because the model does not mark what it handed out.
	windows.Sync(ThreeWindows(true));
	Show("Sync, middle window foreground", windows);
	Report(windows.ActiveIndex() == 1 && windows.ActiveHandle() == 2,
	       "the focused window is reported from the source", windows.ToString());
	Report(windows.NextFocus(1) == 3 && windows.NextFocus(-1) == 1,
	       "cycling steps from the focused window in both directions", windows.ToString());
	Report(!windows.IsActiveAt(2), "cycling does not mark the entry it returned",
	       windows.ToString());

	// A kept window keeps its place whatever order the source reports next time.
	Vector<WindowEntry> shuffled;
	WindowEntry c; c.handle = 3; c.title = "(untitled)"; c.active = false;
	WindowEntry a; a.handle = 1; a.title = "Desktop"; a.active = false;
	WindowEntry b; b.handle = 2; b.title = "Terminal - edited"; b.active = false;
	shuffled.Add(c);
	shuffled.Add(a);
	shuffled.Add(b);
	windows.Sync(shuffled);
	Show("Sync, source order changed", windows);
	Report(windows.HandleAt(0) == 1 && windows.HandleAt(2) == 3,
	       "the order is stable across a changed source order", windows.ToString());
	Report(windows.TitleAt(1) == "Terminal - edited",
	       "a surviving window gets its refreshed title", windows.ToString());

	// The window that went away is dropped, and a new one lands at the end.
	Vector<WindowEntry> after;
	WindowEntry a2; a2.handle = 1; a2.title = "Desktop"; a2.active = false;
	WindowEntry b2; b2.handle = 2; b2.title = "Terminal - edited"; b2.active = false;
	WindowEntry d; d.handle = 4; d.title = "New"; d.active = false;
	after.Add(a2);
	after.Add(b2);
	after.Add(d);
	windows.Sync(after);
	Show("Sync, one window closed one opened", windows);
	Report(windows.GetCount() == 3 && windows.HandleAt(0) == 1 && windows.HandleAt(2) == 4,
	       "the gone handle is dropped and the new one appended", windows.ToString());

	// Edge cases the view can reach: no windows, and one window.
	WindowListModel empty;
	Report(empty.NextFocus(1) == WindowListModel::NoHandle,
	       "an empty list has nothing to focus", empty.ToString());
	Report(empty.NextIndex(-1) == -1, "an empty list has no next index", empty.ToString());

	Vector<WindowEntry> only;
	WindowEntry solo; solo.handle = 7; solo.title = "Only"; solo.active = false;
	only.Add(solo);
	empty.Sync(only);
	Report(empty.NextFocus(1) == 7 && empty.NextFocus(-1) == 7,
	       "a single window is the answer in both directions", empty.ToString());
}

// SyncWindows() as DM5-004 drives it: the snapshot first, whose read also
// refreshed the source's answer for the current workspace, then the model is
// told which workspace it answers for, then the filtered sync. The int here
// stands in for what source.CurrentDesktop() would have reported.
static int SyncWindows(WindowListModel& windows, const Vector<WindowEntry>& snapshot,
                       int current_desktop)
{
	windows.SetWorkspace(current_desktop);
	windows.Sync(snapshot);
	return windows.GetCount();
}

// The desktop window's list answers for the current workspace, with the sticky
// window present on every one of them, and the set follows the published
// current workspace both ways.
static void ProbeDesktopWorkspace()
{
	printf("\n-- DesktopManager workspace filter: the desktop list follows the switch\n");

	WindowListModel windows;
	Vector<WindowEntry> seen;
	WindowEntry a; a.handle = 1; a.title = "Desktop";  a.active = false; a.desktop = 0;
	WindowEntry b; b.handle = 2; b.title = "own";      b.active = false; b.desktop = 0;
	WindowEntry c; c.handle = 3; c.title = "Foreign";  c.active = false; c.desktop = 1;
	WindowEntry s; s.handle = 4; s.title = "Sticky";   s.active = true;
	seen.Add(a);
	seen.Add(b);
	seen.Add(c);
	seen.Add(s);

	// The desktop window is one of the entries the real view filters out, but
	// the probe keeps it so the count arithmetic is readable: on workspace 0
	// the visible set is Desktop, own and the sticky one; on workspace 1 only
	// Foreign and the sticky one.
	Report(SyncWindows(windows, seen, 0) == 3 && windows.HandleAt(2) == 4,
	       "current workspace 0 lists its windows plus the sticky one",
	       windows.ToString());
	Report(SyncWindows(windows, seen, 1) == 2 && windows.HandleAt(0) == 3
	       && windows.HandleAt(1) == 4,
	       "a switch to workspace 1 lists the foreign window and the sticky one",
	       windows.ToString());
	Report(SyncWindows(windows, seen, 0) == 3 && windows.HandleAt(2) == 4,
	       "and the switch back lists the workspace-0 set again", windows.ToString());
	Report(SyncWindows(windows, seen, 7) == 1 && windows.HandleAt(0) == 4,
	       "an out-of-range current workspace leaves only the sticky one",
	       windows.ToString());
	Report(SyncWindows(windows, seen, WindowSource::AllDesktops) == 4,
	       "a bare server passes everything", windows.ToString());
}

int main()
{
	// No DISPLAY is needed or consulted: nothing here touches a window system.
	ProbeWorkspaces();
	ProbeWindowList();
	ProbeDesktopWorkspace();

	printf("\nDesktopManagerProbe: %s\n", failures ? "FAILED" : "all checks passed");
	NoMemoryLeaksCheck = true;
	SetExitCode(failures ? 1 : 0);
	return GetExitCode();
}
