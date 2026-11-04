#ifndef _DesktopX11_X11ScreenSource_h_
#define _DesktopX11_X11ScreenSource_h_

//! Screen metrics read from the X11 server through its own connection
/*!
  The production `ScreenSource` for `ScreenInfoModel`. It cannot live in
  `uppsrc/DesktopModel` because it opens a connection, and the model package must
  keep building with no GUI flag and no display, which is the property DM2-016
  proves. It used to live in `uppsrc/DisplayManager` instead, but that package is a
  MAIN package: linking it from a check package collides `main`, so its
  dead-server path could only be verified by hand. It lives here now, beside
  `X11WindowSource` and reachable by `upptst` like every other source.

  It calls `XOpenDisplay()` itself instead of reusing the global `Xdisplay`
  declared at `uppsrc/CtrlCore/X11Gui.h:30`. Two reasons, in order of
  importance:

  - A source that depends on a GUI global is a deferred refactor, not a source.
    `Xdisplay` is unreachable from a translation unit that does not include
    `CtrlCore`, so the screen queries could not be reused headlessly and
    DM2-017 would have nothing to hand over.
  - The global belongs to `Ctrl`'s connection and its event loop. Screen
    metrics are read on a different schedule from the one that connection
    pumps, so sharing it couples two lifetimes that have no reason to be equal.

  The connection is opened per query rather than held for the lifetime of the
  object. Screen queries happen on Initialize and on a screen-change event, not
  in a hot loop, so the round trip is irrelevant next to the correctness win:
  there is no connection to go stale, so a server that disappears mid-session
  makes `GetCount()` report 0 and `ReadScreen()` report false instead of
  handing back geometry read from a dead socket. It also means there is no
  `Display*` to close in a destructor, so the class cannot leak an X connection
  when a caller forgets to clean up.

  The cost of that choice is a second connection to the same server while
  `Ctrl` holds its own. X11 allows any number of connections per client, so
  this is normal rather than exceptional.

  `X11WindowSource` in this package keeps a second, lifetime-held connection for
  events, because a connection closed between queries cannot receive any. This
  source does not: it is only ever read, so per-query is enough and per-query is
  what keeps it unable to go stale.
*/
class X11ScreenSource : public ScreenSource
{
public:
	virtual int  GetCount() override;
	virtual bool ReadScreen(int index, ScreenMetrics& out) override;
};

#endif // _DesktopX11_X11ScreenSource_h_
