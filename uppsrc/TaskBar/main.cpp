#include <CtrlLib/CtrlLib.h>
#include "TaskBar.h"

GUI_APP_MAIN
{
	using namespace Upp;

	// TaskBar is a plain Ctrl, so it needs a host window. A frameless
	// TopWindow pinned to the bottom of the screen mirrors fluxbox's toolbar.
	// Run() blocks until the window closes, keeping tb alive for the event loop.
	TopWindow host;
	host.Title("TaskBar");
	host.FrameLess().NoSizeable().NoZoomable().NoCloseBox();

	TaskBar tb;
	// SizePos is not optional: a child Ctrl added without it stays 0x0, Layout()
	// bails out on cx <= 4, and the panel paints as a uniform strip with no
	// entries -- which is exactly what the second visual run showed, with the
	// log reading "model 4, rects 4, ctrl 0x0".
	host.Add(tb.SizePos());

	Size screen = Ctrl::GetVirtualScreenArea().GetSize();
	int bar_h = 28;
	// Rect is (left, top, right, bottom), not (x, y, width, height): the first
	// version passed bar_h as the bottom edge, which made top > bottom, an
	// inverted rect TopWindow refuses, and the taskbar opened as a default
	// two-thirds window in the middle of the screen. Found in the first visual
	// run, where the panel was a floating 853x533 window nobody had docked.
	host.SetRect(Rect(0, screen.cy - bar_h, screen.cx, screen.cy));
	host.Run();
}