#ifndef _DisplayManager_DisplayManager_cpp_
#define _DisplayManager_DisplayManager_cpp_

#include "DisplayManager.h"

// X11-only package. The old shape of this file was one big
// `#if defined flagPOSIX && defined flagX11` block with an `#else` full of stub
// implementations that reported "needs the X11 backend" and returned false. A
// build without the flag therefore produced a working-looking binary that
// reported zero screens. Screen enumeration is the whole point of the package,
// so the build stops here instead.
#if !defined flagX11
#error "DisplayManager requires the X11 backend: build with a mainconfig that passes X11, for example \"X11\" = \"GUI X11\". Without flagX11 CtrlCore selects the GTK backend and the screen queries have no declarations."
#endif

NAMESPACE_UPP

DisplayManager::DisplayManager()
{
	Title("DisplayManager");
	Sizeable().Zoomable();
}

DisplayManager::~DisplayManager()
{
}

bool DisplayManager::Initialize()
{
	// The "no X display connection" branch that used to sit here checked the
	// global Ctrl connection. It is gone: the source owns its own connection, so
	// reaching no screen is now the source reporting zero of them, which
	// ScreenInfoModel::Update turns into an empty model and a false return.
	bool screens_found = screen.Update(source);
	if(!screens_found)
		LOG("DisplayManager: no X screen reachable through X11ScreenSource");
	return screens_found;
}

bool DisplayManager::HandleScreenChange(int screen_index)
{
	// Out-of-range indices still return false here, now because the model has no
	// such screen rather than because the view counted its own vector.
	return screen.RefreshScreen(screen_index, source);
}

int DisplayManager::GetScreenCount() const
{
	return screen.GetCount();
}

Rect DisplayManager::GetScreenGeometry(int index) const
{
	return screen.Geometry(index);
}

Rect DisplayManager::GetWorkArea(int index) const
{
	return screen.WorkArea(index);
}

int DisplayManager::GetDPI(int index) const
{
	return screen.DPI(index);
}

void DisplayManager::GetDPI(int index, int& horizontal, int& vertical) const
{
	screen.DPI(index, horizontal, vertical);
}

void DisplayManager::Paint(Draw& w)
{
	Size sz = GetSize();
	w.DrawRect(sz, SColorFace());

	int count = screen.GetCount();
	if(count == 0) {
		w.DrawText(12, 10, "No screens: X11 backend unavailable or not initialized.",
		           StdFont(), SColorText());
		return;
	}

	int y = 8;
	w.DrawText(12, y, Format("Screens: %d", count), StdFont(14), SColorText());
	y += 26;

	for(int i = 0; i < count; i++) {
		Rect g = screen.Geometry(i);
		Rect a = screen.WorkArea(i);
		int hx, hy;
		screen.DPI(i, hx, hy);
		w.DrawText(20, y, Format("Screen %d", i), StdFont(12), SColorHighlight());
		y += 18;
		w.DrawText(32, y, Format("geometry  %d x %d at %d, %d", g.GetWidth(), g.GetHeight(),
		                         g.left, g.top), StdFont(), SColorText());
		y += 16;
		w.DrawText(32, y, Format("work area  %d x %d at %d, %d", a.GetWidth(), a.GetHeight(),
		                         a.left, a.top), StdFont(), SColorText());
		y += 16;
		w.DrawText(32, y, Format("dpi        %d x %d%s", hx, hy,
		                         hx ? "" : "  (server reports no physical size)"),
		           StdFont(), SColorText());
		y += 24;
	}
}

END_UPP_NAMESPACE

#endif // _DisplayManager_DisplayManager_cpp_
