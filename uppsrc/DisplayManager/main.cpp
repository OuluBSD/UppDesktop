#include <CtrlLib/CtrlLib.h>
#include "DisplayManager.h"

GUI_APP_MAIN
{
	using namespace Upp;

	// Run() blocks until the window closes, keeping dm alive for the event loop.
	DisplayManager dm;
	dm.Initialize();
	dm.Sizeable();
	// (left, top, right, bottom): the default two-thirds window opens centered,
	// exactly on top of DesktopManager's shell window, and without a window
	// manager there is no way to move either one. Found in the first visual run.
	Rect screen = Ctrl::GetVirtualScreenArea();
	dm.SetRect(Rect(40, 60, 40 + 620, 60 + 460));
	dm.Run();
}