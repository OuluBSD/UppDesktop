#include "Explorer.h"
#include "ExplorerLang.h"
#include "ExplorerModel.h"
#include "ExplorerPolicy.h"
#include "ExplorerGate.h"
#include "ExplorerDemoVfs.h"

#ifndef flagGUI

NAMESPACE_UPP
namespace ExplorerHeadless {
	int Main(const Vector<String>& args);
}
END_UPP_NAMESPACE

// The headless twin. Same model, same policy, same log as the window; no
// confirmation channel, so anything needing the user fails closed.
CONSOLE_APP_MAIN
{
	using namespace Upp;
	// SetExitCode, not `return rc`: CONSOLE_APP_MAIN expands to a void function
	// whose caller does `return GetExitCode()`. Dropping the return value made the
	// twin exit 0 for a bad option, a missing directory, a denied operation and a
	// missing entry alike, so every failure looked like success to a script.
	SetExitCode(ExplorerHeadless::Main(CommandLine()));
}

#else

#define IMAGECLASS ExplorerImg
#define IMAGEFILE <Explorer/Explorer.iml>
#include <Draw/iml_source.h>

#include "ExplorerLogWindow.h"
#include "ExplorerSkin.h"

using namespace Upp;
namespace Upp { extern bool NoMemoryLeaksCheck; }

// Shared startup: mount the host filesystem if nobody else has, and open the
// rotating log. Both binaries do this so their view of the namespace is identical.
namespace ExplorerStartup {
	void Prepare()
	{
		ExplorerLog::SetupFile();
		// The look, before anything that can call t_() and before the window is
		// built. Ctrl::SetSkin() rather than a direct ChClassicSkin() call,
		// because ReSkin() re-runs the installed function between ChReset() and
		// ChFinish(): a skin installed any other way is undone by the next
		// display change, silently, and the window comes back half-skinned.
		Ctrl::SetSkin(ExplorerSkin);
		// Before anything that can call t_(), and before the window is built: a
		// language chosen after the first string has been rendered leaves half a
		// window in one language and half in another. It honours --lang and
		// $EXPLORER_LANG itself, then $LANG, so it is called here and nowhere else.
		ExplorerSetLanguage();
		EINFO(ExplorerChannels::MOUNT, "explorer starting");

		// MountManager is a process-wide singleton and may already carry mounts
		// from an embedding host, so only mount a root when there is none.
		if(!MountManager::System().HasRoot()) {
			MountManager::System().MountOwned("/", new SystemFS, "sysfs");
			EINFO(ExplorerChannels::MOUNT, "mounted host filesystem at /");
		} else {
			EINFO(ExplorerChannels::MOUNT, Sprintf("host filesystem already mounted; reusing %d mounts",
			                                       MountManager::System().GetMountCount()));
		}
	}
}

// The first bare argument is the location to open; options are ignored by the
// window, the headless twin is the one that acts on them.
static String InitialPath(const Vector<String>& args)
{
	for(const String& a : args)
		if(!a.StartsWith("-")) return a;
	return Null;
}

// --dump-ui prints the widget tree and closes, so the window's own state can be
// checked without a human looking at pixels. KWin refuses synthetic input on this
// machine, which leaves the widget tree as the one thing about the window that can
// still be verified mechanically: rows present, columns correct, controls enabled.
// It is printed after the first paint, so the rects are real.
GUI_APP_MAIN
{
	ExplorerStartup::Prepare();
	// The gui-user-gate runs build their own window and never hand control back,
	// so they are dispatched before this process opens one of its own.
	for(const String& a : CommandLine())
		if(a == "--gui-toon" || a == "--gui-access-gate"
		   || a == "--gui-render-gate" || a.StartsWith("--gui-llm-gate")) {
			ExplorerGate::Main(CommandLine());
			return; // never reached: the gate leaves through _exit()
		}
	// The command line is read twice and CommandLine() hands back a reference to a
	// process-wide vector that is not copyable here, so each use takes its own view
	// of it rather than storing one.
	bool dump_ui = false;
	for(const String& a : CommandLine())
		if(a == "--dump-ui") dump_ui = true;
	// The listing mode, so that --dump-ui can show the icon view the same way a user
	// would see it after choosing it from the View menu.
	ViewMode view = VIEW_LIST;
	bool folders_first = true, ignore_case = false;
	// The overlay demonstration (phase-02 task 04). --demo-overlay mounts the
	// synthetic VFS before the window is built, so the whole window -- listing,
	// tree, breadcrumb, status -- is built on top of it, which is what a user
	// browsing into a mounted path sees. --demo-cycle leaves the host files in
	// place and lets the dump run the mount/re-read/unmount cycle, so one run
	// holds all three stages.
	String demo_overlay, demo_cycle;
	{
		const Vector<String>& args = CommandLine();
		for(int i = 0; i < args.GetCount(); i++) {
			if(args[i] == "--view" && i + 1 < args.GetCount()) {
				if(args[i + 1] == "big" || args[i + 1] == "big-icons")   view = VIEW_BIG_ICONS;
				else if(args[i + 1] == "small" || args[i + 1] == "small-icons") view = VIEW_SMALL_ICONS;
				else if(args[i + 1] == "list") view = VIEW_LIST;
			}
			// The same two flags the headless twin takes, and for the same reason: a
			// sort toggle that can only be checked in the state the window opened in
			// is one whose other state no run can reach.
			else if(args[i] == "--no-folders-first") folders_first = false;
			else if(args[i] == "--ignore-case")     ignore_case = true;
			// Two plain arms, not a conditional: the conditional-as-lvalue form
			// bound to a temporary here and both flags stayed empty, silently.
			else if(args[i] == "--demo-overlay" && i + 1 < args.GetCount())
				demo_overlay = args[++i];
			else if(args[i] == "--demo-cycle" && i + 1 < args.GetCount())
				demo_cycle = args[++i];
		}
	}
	if(demo_overlay.GetCount() && demo_cycle.GetCount()) {
		Cerr() << "error: --demo-overlay and --demo-cycle are two forms of the same demo; give one\n";
		return;
	}
	if(demo_overlay.GetCount()) {
		String error;
		if(!MountDemoOverlay(demo_overlay, error)) {
			Cerr() << "error: " << error << "\n";
			return;
		}
	}
	else if(demo_cycle.GetCount())
		ExplorerDemo::cycle_path = demo_cycle;
	if(dump_ui)
		NoMemoryLeaksCheck = true;
	Explorer explorer(InitialPath(CommandLine()), dump_ui, view, folders_first, ignore_case);
	explorer.Run();
#ifdef flagDEBUG
	// The log window is a singleton allocated on first use, so it outlives Run().
	ExplorerLogWindow::Shutdown();
#endif
}

#endif
