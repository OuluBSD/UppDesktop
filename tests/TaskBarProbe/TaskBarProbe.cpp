#include <Core/Core.h>
#include <DesktopModel/DesktopModel.h>

// Headless twin of the TaskBar view, task DM2-011 and DM2-012.
//
//	bin/build -m CLANG -M HEADLESS TaskBarProbe && ./bin/CLANG/TaskBarProbe
//
// It drives the same model calls that uppsrc/TaskBar/TaskBar.cpp drives, in the
// same order, and prints the state after each one. The view keeps only the Rect
// of each item and the handle-to-pointer table; neither can exist without a
// display, and neither is model state, so the list itself is what this proves.
//
// The host-window filter, the 500 ms timer and the `live` registry are view
// wiring and are not here on purpose.
//
// Printing is printf, not LOG: the models must stay silent, and LOG does not
// reliably reach stdout in these builds. A printed value that contradicts what
// the model was told sets the exit code.

using namespace Upp;

namespace Upp { extern bool NoMemoryLeaksCheck; }

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

static void Show(const char* step, const WindowListModel& windows)
{
	printf("     %-34s %s\n", step, (const char *)windows.ToString());
}

// One window as TaskBar::SnapshotWindows() builds it. The "Window" fallback for
// an empty title happens in the view, so it is already in place here: the model
// is handed the text the taskbar paints and never asked to invent any. The
// desktop is what the source filled into the entry; the default is the sticky
// value, which is what a window with no _NET_WM_DESKTOP reports.
static void Add(Vector<WindowEntry>& seen, int handle, const char* title, bool active,
                int desktop = WindowSource::AllDesktops)
{
	WindowEntry entry;
	entry.handle = handle;
	entry.title = title;
	entry.active = active;
	entry.desktop = desktop;
	seen.Add(entry);
}

static void ProbeTaskBarSync()
{
	printf("\n-- TaskBar Sync: what the 500 ms timer drives\n");

	WindowListModel windows;
	Show("fresh", windows);

	// First timer tick: three windows, none focused.
	Vector<WindowEntry> seen;
	Add(seen, 1, "Terminal", false);
	Add(seen, 2, "Desktop", false);
	Add(seen, 3, "Window", false);
	windows.Sync(seen);
	Show("Sync, three windows", windows);
	Report(windows.GetCount() == 3, "three items listed", windows.ToString());

	// A window closes and another opens in the same tick.
	seen.Clear();
	Add(seen, 1, "Terminal", false);
	Add(seen, 3, "Window", true);
	Add(seen, 4, "Editor - notes.md", false);
	windows.Sync(seen);
	Show("Sync, one closed one opened", windows);
	Report(windows.GetCount() == 3 && windows.HandleAt(0) == 1 && windows.HandleAt(1) == 3
	       && windows.HandleAt(2) == 4,
	       "the closed handle is dropped and the new one appended", windows.ToString());
	Report(windows.IsActiveAt(1), "the active flag follows the source", windows.ToString());

	// The title of a window that stayed is refreshed, which is why the view does
	// not cache the text itself any more.
	seen.Clear();
	Add(seen, 1, "Terminal - zsh", false);
	Add(seen, 3, "Window", true);
	Add(seen, 4, "Editor - notes.md", false);
	windows.Sync(seen);
	Show("Sync, a title changed", windows);
	Report(windows.TitleAt(0) == "Terminal - zsh",
	       "a surviving window gets its new title", windows.ToString());

	// Every window closed.
	seen.Clear();
	windows.Sync(seen);
	Show("Sync, no windows", windows);
	Report(windows.IsEmpty(), "an empty tick clears the list", windows.ToString());
	Report(windows.ToString() == "", "and leaves nothing behind to paint", windows.ToString());
}

static void ProbeTaskBarClick()
{
	printf("\n-- TaskBar LeftDown and CycleWindows: handle to window\n");

	// LeftDown hit-tests a rect, then asks the model for the handle at that index.
	// The rects are presentation and are not modelled, so the index is the input.
	WindowListModel windows;
	Vector<WindowEntry> seen;
	Add(seen, 11, "Terminal", false);
	Add(seen, 12, "Desktop", true);
	Add(seen, 13, "Window", false);
	windows.Sync(seen);
	Show("Sync, three windows", windows);

	int hit_index = 2;
	int handle = windows.HandleAt(hit_index);
	printf("     click at item %d                 handle %d title %s\n",
	       hit_index, handle, (const char *)windows.TitleAt(hit_index));
	Report(handle == 13, "the handle at the clicked index is the one to raise",
	       windows.ToString());
	Report(windows.HandleAt(99) == WindowListModel::NoHandle,
	       "a click outside every item has no handle", windows.ToString());

	// CycleWindows(): the model answers with the next index, the view raises.
	int next_index = windows.NextIndex(1);
	printf("     CycleWindows()                   index %d handle %d\n",
	       next_index, windows.HandleAt(next_index));
	Report(next_index == 2 && windows.HandleAt(next_index) == 13,
	       "cycling from the focused item steps to the next one", windows.ToString());

	// LeftDown re-syncs afterwards, so the focus flag comes from the source and
	// not from the click.
	Vector<WindowEntry> after;
	Add(after, 11, "Terminal", false);
	Add(after, 12, "Desktop", false);
	Add(after, 13, "Window", true);
	windows.Sync(after);
	Show("Sync after the click", windows);
	Report(windows.ActiveIndex() == 2, "the raised window is now the active one",
	       windows.ToString());
}

static void ProbeTaskBarEdges()
{
	printf("\n-- TaskBar edges: one window and no window\n");

	// The old early return did nothing for a single listed window. The model
	// returns it in both directions instead, which is a deliberate change.
	WindowListModel windows;
	Vector<WindowEntry> seen;
	Add(seen, 1, "Only", false);
	windows.Sync(seen);
	Show("Sync, one window", windows);
	Report(windows.NextIndex(1) == 0, "one window cycles to itself forwards",
	       windows.ToString());
	Report(windows.NextIndex(-1) == 0, "one window cycles to itself backwards",
	       windows.ToString());

	windows.Clear();
	Report(windows.NextIndex(1) == -1 && windows.NextIndex(-1) == -1,
	       "no window has no next index", windows.ToString());
	Report(windows.ActiveIndex() == -1 && windows.ActiveHandle() == WindowListModel::NoHandle,
	       "no window has no active one", windows.ToString());
}

// The workspace filter as the taskbar drives it: the view names the workspace
// the source reported and then syncs, and the list it paints is the filtered
// one. The desktop values here stand in for what _NET_WM_DESKTOP filled in.
static void ProbeTaskBarWorkspace()
{
	printf("\n-- TaskBar sync with a workspace: the current workspace's windows only\n");

	WindowListModel windows;
	Vector<WindowEntry> seen;
	Add(seen, 1, "Terminal", false, 0);
	Add(seen, 2, "Sticky", true);
	Add(seen, 3, "Other", false, 1);

	// The view pair, in Sync()'s order: name the workspace, then sync.
	windows.SetWorkspace(0);
	windows.Sync(seen);
	Show("Sync, workspace 0", windows);
	Report(windows.GetCount() == 2 && windows.HandleAt(0) == 1 && windows.HandleAt(1) == 2,
	       "the panel lists this workspace's windows and the sticky one", windows.ToString());

	windows.SetWorkspace(1);
	windows.Sync(seen);
	Show("Sync, workspace 1", windows);
	Report(windows.GetCount() == 2 && windows.HandleAt(0) == 2 && windows.HandleAt(1) == 3,
	       "after a switch the panel lists the other workspace, sticky included",
	       windows.ToString());

	// A window that closes on another workspace must not resurrect the list:
	// the view syncs whatever the source reports, and the filter answers.
	seen.Clear();
	Add(seen, 3, "Other", false, 1);
	windows.Sync(seen);
	Show("Sync, workspace 1 again", windows);
	Report(windows.GetCount() == 1 && windows.HandleAt(0) == 3,
	       "the panel still answers for workspace 1 across further syncs", windows.ToString());
}

int main()
{
	ProbeTaskBarSync();
	ProbeTaskBarClick();
	ProbeTaskBarWorkspace();
	ProbeTaskBarEdges();

	printf("\nTaskBarProbe: %s\n", failures ? "FAILED" : "all checks passed");
	NoMemoryLeaksCheck = true;
	SetExitCode(failures ? 1 : 0);
	return GetExitCode();
}
