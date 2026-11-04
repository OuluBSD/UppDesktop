#ifndef _DesktopModel_ScreenInfoModel_h_
#define _DesktopModel_ScreenInfoModel_h_

//! Raw metrics of one screen, as a source reports them
/*!
  The model derives DPI and the work area from this, so a source only has to
  hand over what the window system knows and nothing else. `width_mm` and
  `height_mm` are 0 when the server reports no physical size, which is what
  virtual machines usually do; the model turns that into a DPI of 0 rather than
  a made-up number.
*/
struct ScreenMetrics : Moveable<ScreenMetrics>
{
	int x, y, width, height;
	int width_mm, height_mm;
};

//! Narrow source of screen metrics
/*!
  Deliberately smaller than what a display manager might want: a screen count
  and one screen's metrics. The X11 implementation lives with the view that needs
  it, in `uppsrc/DisplayManager`, because it opens its own X connection and this
  package must keep compiling with no GUI flag and no display. A test supplies a
  fake and gets the same numbers a real server would give.

  The method is `GetCount()`, matching `Vector::GetCount()` and the rest of the
  codebase, so writing `Get` and letting Assist complete it lands here. It is
  deliberately not `ScreenCount()`: `X11/Xlib.h` defines `ScreenCount(dpy)` as a
  function-like macro, and a macro cannot be dodged by qualification. Any
  translation unit that includes both Xlib and this header turns
  `screen.ScreenCount()` into a macro call and the model becomes unspellable from
  the one place that has to spell it. `WorkspaceModel` and `WindowListModel` use
  the same name, so all three read alike.
*/
class ScreenSource
{
public:
	virtual ~ScreenSource() {}

	//! How many screens there are, 0 when none can be reached
	virtual int GetCount() = 0;

	//! Metrics of the screen at index, false when the index does not exist
	virtual bool ReadScreen(int index, ScreenMetrics& out) = 0;
};

//! Screen geometry, work area and DPI, read through a source
/*!
  Each screen's geometry, work area and DPI live in one record rather than in
  four parallel vectors, which is what used to let the work areas run ahead of
  the geometries when a screen count changed.

  The work area is the screen minus the panel strip on top and the taskbar strip
  at the bottom, mirroring fluxbox's slit/root split, and it is recomputed
  whenever either strip or a screen changes.
*/
class ScreenInfoModel
{
public:
	enum { DefaultTaskbarHeight = 24 };

	ScreenInfoModel() : taskbar_height(DefaultTaskbarHeight), panel_height(0) {}

	//! Re-read every screen through the source. False when the source has none.
	bool Update(ScreenSource& source);

	//! Re-read one screen and recompute its work area. False for an index the
	//! model does not have, or when the source cannot read that screen.
	bool RefreshScreen(int index, ScreenSource& source);

	//! Recompute every work area from the current geometries and strips
	void RecalcWorkAreas();

	int  GetCount() const { return screens.GetCount(); }
	Rect Geometry(int index) const;
	Rect WorkArea(int index) const;

	//! Horizontal DPI, 0 when the server reported no physical size
	int  DPI(int index) const;
	//! Horizontal and vertical DPI, both 0 when the size is unknown
	void DPI(int index, int& horizontal, int& vertical) const;

	//! Strip reserved at the bottom of every screen for the taskbar
	void SetTaskbarHeight(int h) { taskbar_height = h > 0 ? h : 0; RecalcWorkAreas(); }
	int  TaskbarHeight() const    { return taskbar_height; }

	//! Strip reserved at the top of every screen for a panel
	void SetPanelHeight(int h)     { panel_height = h > 0 ? h : 0; RecalcWorkAreas(); }
	int  PanelHeight() const      { return panel_height; }

	String ToString() const;

private:
	struct ScreenRecord : Moveable<ScreenRecord>
	{
		Rect geometry;
		Rect work_area;
		int  dpi_x, dpi_y;
	};

	void StoreScreen(int index, const ScreenMetrics& metrics);
	void CheckInvariants() const;

	Vector<ScreenRecord> screens;
	int taskbar_height;
	int panel_height;
};

#endif // _DesktopModel_ScreenInfoModel_h_
