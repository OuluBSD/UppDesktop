#ifndef _TaskBar_TaskBar_h_
#define _TaskBar_TaskBar_h_

#include <CtrlLib/CtrlLib.h>
#include <DesktopModel/DesktopModel.h>
#include <DesktopX11/DesktopX11.h>

NAMESPACE_UPP

//! Taskbar listing open top-level windows
/*!
  A plain Ctrl that shows one button per open top-level window and raises the
  clicked one. It must be hosted inside a window, typically a frameless
  TopWindow pinned to the screen edge.

  The window list itself lives in `WindowListModel` from `uppsrc/DesktopModel`.
  This class keeps only what is presentation: the `Rect` of each item, which
  `Layout()` computes. Handles are integers the model understands; the pointer
  each one stands for stays here, because a model that could name a `TopWindow`
  could not be tested without one.
*/
class TaskBar : public Ctrl {
public:
	TaskBar();
	virtual ~TaskBar();

	virtual void Paint(Draw& w) override;
	virtual void LeftDown(Point p, dword keyflags) override;
	virtual void Layout() override;

	//! Rebuild the window list from the open top-level windows
	void Sync();

	//! Raise the next window in Sync() order, wrapping. Ignores the host
	//! window. This is the instance method to use from a hosted taskbar.
	void CycleWindows();

	//! Raise the next open top-level window, ignoring any host window.
	//! Static so a global shortcut can cycle windows with no taskbar present.
	static void CycleAllTopWindows();

	int  GetItemCount() const { return rects.GetCount(); }
	String GetItemTitle(int i) const { return windows.TitleAt(i); }

	//! The window list model, for callers and tests that want the list itself
	WindowListModel& GetWindows()             { return windows; }
	const WindowListModel& GetWindows() const { return windows; }

private:
	//! What the X server reports, minus the windows this process owns
	Vector<WindowEntry> SnapshotWindows();

	//! Raises the host window above everything, after raising a client or
	//! learning that the server's stacking changed. With no window manager
	//! running, a raised client covers the panel, and nothing else puts it back.
	void RestackSelf();

	//! True when a native window belongs to this process, so it is a window the
	//! user did not open and must not get a taskbar button. This covers the window
	//! hosting the taskbar, which the old code filtered by comparing against
	//! GetTopWindow() before it looked at anything else.
	bool IsOwnWindow(::Window native) const;

	//! Live taskbar instances, so CycleAllTopWindows() can find the tracked
	//! list without being handed one
	static Vector<TaskBar*> live;

	WindowListModel  windows;

	//! Item geometry, parallel to the model's list. Presentation only, and
	//! Layout() is the only thing that writes it.
	Vector<Rect>     rects;

	//! The production window source. A member so its interned handles survive
	//! between syncs: the model drops a handle the source stops reporting, so a
	//! source that reallocated on every query would reorder the list each read.
	X11WindowSource  source;
};

END_UPP_NAMESPACE

#endif // _TaskBar_TaskBar_h_
