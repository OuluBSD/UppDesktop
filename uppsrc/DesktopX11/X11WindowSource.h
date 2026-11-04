#ifndef _DesktopX11_X11WindowSource_h_
#define _DesktopX11_X11WindowSource_h_

//! Every window the X server knows about, over its own connection
/*!
  The production `WindowSource`, and the replacement for
  `Ctrl::GetTopCtrls()`, which can only ever return windows this process created:
  it walks the static `Ctrl::topctrl` at `uppsrc/CtrlCore/TuiCtrl.cpp:116`, which
  is only appended from this process's own `TopWindow` construction. This source
  asks the server instead, so the desktop can see windows it did not make.

  It lives in its own package rather than in `uppsrc/DesktopModel` because it
  opens a connection and links Xlib. `DesktopModel` must keep building with no GUI
  flag and no display. This package keeps the GUI flag out too -- it builds with
  `flagX11` alone and links `libX11` directly -- so a check package can link it
  and run it against a live server with no `Ctrl` in the process. That is the
  difference between this source and `X11ScreenSource`, which still sits in a MAIN
  package and so cannot be linked by a test.

  What it reads, and the contract that fixes it, is in
  `ai/info/desktop-model-view-split.md` under "The system-wide window source":
  `_NET_CLIENT_LIST` for the set and its stacking order, `_NET_WM_NAME` with a
  `WM_NAME` fallback, `_NET_WM_DESKTOP`, `_NET_WM_STATE` to skip hidden windows,
  and `_NET_WM_WINDOW_TYPE` to skip tooltips and docks.

  Two decisions are load-bearing and easy to get wrong:

  - `_NET_CLIENT_LIST` is absent on a bare X server with no EWMH window manager,
    and Xvfb is exactly that: `xprop -root _NET_CLIENT_LIST` on a fresh Xvfb
    answers "no such atom on any window". Reporting an empty list there would
    make every headless test pass for the wrong reason, so `Enumerate()` falls
    back to `XQueryTree()` on the root and keeps the children whose
    override-redirect bit is clear.
  - Handles are interned here, not derived from the X window id, and stay stable
    for the life of the source. `WindowListModel::Sync()` drops a handle the
    source stopped reporting, so a handle that changed identity between two
    queries would silently reorder the list and break focus cycling.

  The connection is opened per query and closed on the way out, as in
  `X11ScreenSource`: there is no connection to go stale, so a server that
  disappears makes `ReadWindows()` return empty instead of handing back a list
  read from a dead socket, and there is no `Display*` to leak.
*/
class X11WindowSource : public WindowSource
{
public:
	X11WindowSource();
	virtual ~X11WindowSource();

	//! Reuse an interned handle for a native window, allocating one on first
	//! sight. `Forget()` releases handles whose window is gone.
	int HandleOf(::Window native);

	//! Release the handle interned for a native window
	void Forget(::Window native);

	//! The native window behind a handle this source issued, or None
	::Window NativeOf(int handle) const;

	//! Release every interned handle
	void Clear();

	//! The workspace the manager reports as current, or AllDesktops when no
	//! manager says. Refreshed by every ReadWindows() on the connection that
	//! query already opened -- read per query, never carried across queries.
	int CurrentDesktop() const { return current_desktop; }

	//! Raises one native window above every other window
	/*!
	  `Raise(int)` works on interned handles, and a panel's own host window has
	  none: handles are interned for windows the source listed, and the panel is
	  one window the view deliberately leaves out of the list. A panel that
	  raises a foreign window is then covered by it, because with no window
	  manager running nothing keeps a docked strip above the raised client, so
	  the panel restacks itself last, after every raise it performed.
	*/
	void RaiseNative(::Window native);

	//! Open the watch connection and select `SubstructureNotify` on the root.
	//! False when there is no display, or when the call is made twice.
	/*!
	  Events need a connection that stays open, and the reads deliberately do
	  not have one, so this is a second connection held for the life of the
	  object. It exists because CtrlCore selects no root event mask at all --
	  `uppsrc/CtrlCore/X11Wnd.cpp:554` selects per-window input and nothing ever
	  selects `SubstructureNotify` on the root -- so there is no way to be told
	  through Ctrl that another client opened a window.
	*/
	bool Watch();

	//! Drain the watch connection and report whether anything changed since the
	//! last `ReadWindows()`. Never blocks: with nothing queued it costs one
	//! non-blocking check of the socket buffer and no round trip to the server.
	bool Poll();

	//! True when the server reported a change this source has not read yet.
	//! `ReadWindows()` clears it, because that is the read it refers to.
	bool Dirty() const { return dirty; }

	virtual Vector<WindowEntry> ReadWindows() override;
	virtual int DesktopOf(int handle) const override;
	virtual bool Raise(int handle) override;
	virtual bool Focus(int handle) override;

private:
	//! Windows the server reports, in stacking order. Reads `_NET_CLIENT_LIST`
	//! and falls back to `XQueryTree()` on the root when that atom is absent.
	Vector<::Window> Enumerate(DesktopDisplay* dpy) const;

	//! True when the window is one a taskbar should list: viewable, not
	//! override-redirect, not hidden, and not a tooltip, dock or desktop window.
	static bool IsManaged(DesktopDisplay* dpy, ::Window win);

	static String ReadTitle(DesktopDisplay* dpy, ::Window win);
	static int ReadDesktop(DesktopDisplay* dpy, ::Window win);

	Vector<::Window> natives;
	Vector<int> handles;
	int next_handle = 1;

	//! The event connection, opened by `Watch()` and closed on destruction.
	//! Never used for reading: the reads go through a connection opened per
	//! query, so a dead server cannot leave a stale cache behind.
	DesktopDisplay* watch_dpy = nullptr;

	//! Set by `Poll()` when the server reported a change, cleared by
	//! `ReadWindows()`. Starts true, because the views re-read only when this
	//! says so: started false, the first sync is skipped, and a screen where
	//! nothing has happened yet shows an empty desktop until some unrelated
	//! window event arrives. Found in the first visual run, where the taskbar
	//! sat empty for minutes on a static screen.
	bool dirty = true;

	//! The manager's current workspace as the last read saw it, or AllDesktops
	int current_desktop = WindowSource::AllDesktops;
};

#endif // _DesktopX11_X11WindowSource_h_