#include <CtrlLib/CtrlLib.h>
#include "DesktopManager.h"

GUI_APP_MAIN
{
	using namespace Upp;

	// Run() blocks until the window closes, keeping dm alive for the event loop.
	DesktopManager dm;
	dm.Initialize();
	dm.Sizeable();
	// (left, top, right, bottom): placed off DisplayManager's corner, because
	// without a window manager nothing separates two windows opened at the
	// default centered position. Found in the first visual run.
	Rect screen = Ctrl::GetVirtualScreenArea();
	dm.SetRect(Rect(720, 80, 720 + 520, 80 + 620));
	dm.Run();
}