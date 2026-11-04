#ifndef _DisplayManager_DisplayManager_h_
#define _DisplayManager_DisplayManager_h_

#include <CtrlLib/CtrlLib.h>
#include <DesktopModel/DesktopModel.h>
#include <DesktopX11/DesktopX11.h>

NAMESPACE_UPP


//! X11 screen geometry, work area, and DPI, rendered from the headless model
/*!
  All screen state lives in `ScreenInfoModel` from `uppsrc/DesktopModel`. This
  class is the view that draws it: it owns no geometry, no DPI and no strip
  heights of its own, and it never queries Xlib. The queries live in
  `X11ScreenSource`, which opens its own X connection and hands raw metrics to
  the model.

  The accessors below stay because they are the view's published surface, but
  each one forwards to the model without keeping local state. That is what keeps
  a second source of truth from creeping back in.
*/
class DisplayManager : public TopWindow {
public:
	DisplayManager();
	virtual ~DisplayManager();

	//! Read every screen through `X11ScreenSource`. Returns false when the source
	//! reaches no screen, leaving GetCount() at 0.
	bool Initialize();

	int  GetScreenCount() const;
	Rect GetScreenGeometry(int index = 0) const;
	Rect GetWorkArea(int index = 0) const;
	//! DPI derived from the screen's pixel and millimetre metrics, 0 if unknown
	int  GetDPI(int index = 0) const;
	//! Horizontal and vertical DPI
	void GetDPI(int index, int& horizontal, int& vertical) const;

	//! Re-read one screen from the X server. Returns false if the index is
	//! invalid or the backend is not X11.
	bool HandleScreenChange(int screen);

	//! Height reserved at the bottom of each screen for the taskbar
	void SetTaskbarHeight(int h)   { screen.SetTaskbarHeight(h); }
	int  GetTaskbarHeight() const  { return screen.TaskbarHeight(); }

	//! Height reserved at the top of each screen for a panel/menu bar
	void SetPanelHeight(int h)     { screen.SetPanelHeight(h); }
	int  GetPanelHeight() const    { return screen.PanelHeight(); }

	//! The model behind this view, for callers that want the state itself
	ScreenInfoModel& GetModel()             { return screen; }
	const ScreenInfoModel& GetModel() const { return screen; }

protected:
	virtual void Paint(Draw& w) override;

private:
	ScreenInfoModel screen;
	X11ScreenSource source;
};

END_UPP_NAMESPACE

#endif
