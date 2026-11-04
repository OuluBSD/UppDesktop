#ifndef _DesktopX11_X11ScreenSource_cpp_
#define _DesktopX11_X11ScreenSource_cpp_

#include "DesktopX11.h"

#if !defined flagX11
#error "X11ScreenSource requires the X11 flag: build with a mainconfig that passes X11, for example \"X11\" = \"X11\"."
#endif

NAMESPACE_UPP

int X11ScreenSource::GetCount()
{
	// Opened and closed per query on purpose; see the header for why a held
	// connection is the wrong trade here. A dead server makes XOpenDisplay
	// return NULL, which becomes 0 screens instead of a stale geometry set
	// that looks live.
	_XDisplay* display = XOpenDisplay(NULL);
	if(!display)
		return 0;
	int count = XScreenCount(display);
	XCloseDisplay(display);
	return count;
}

bool X11ScreenSource::ReadScreen(int index, ScreenMetrics& out)
{
	if(index < 0)
		return false;

	_XDisplay* display = XOpenDisplay(NULL);
	if(!display)
		return false;

	// XScreenOfDisplay does not range check: it indexes the screen array
	// directly, so a bad index is a wild read rather than a NULL return.
	if(index >= XScreenCount(display)) {
		XCloseDisplay(display);
		return false;
	}

	Screen* xs = XScreenOfDisplay(display, index);
	if(!xs) {
		XCloseDisplay(display);
		return false;
	}

	// WidthMMOfScreen/HeightMMOfScreen are the public accessors; the Screen
	// struct itself is documented as public to extensions and not to callers.
	// Reporting the raw millimetres rather than a derived DPI is the point:
	// ScreenInfoModel owns the DPI arithmetic, so a source stays a source.
	out.x = 0;
	out.y = 0;
	out.width = xs->width;
	out.height = xs->height;
	out.width_mm = WidthMMOfScreen(xs);
	out.height_mm = HeightMMOfScreen(xs);

	XCloseDisplay(display);
	return true;
}

END_UPP_NAMESPACE

#endif // _DesktopX11_X11ScreenSource_cpp_
