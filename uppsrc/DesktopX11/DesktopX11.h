#ifndef _DesktopX11_DesktopX11_h_
#define _DesktopX11_DesktopX11_h_

//! X11 window sources for the desktop packages, over their own connection
/*!
  This package exists so the X11 side of the desktop is reachable from a caller
  that has no GUI and no display of its own. It `uses Core` and nothing else, and
  it builds with `flagX11` alone -- no `GUI` flag -- linking `libX11` directly
  from its manifest.

  That combination is the point. `X11ScreenSource` lives in `uppsrc/DisplayManager`,
  which is a MAIN package, so no check package can link it and its dead-server path
  had to be verified by hand through a temporary console `main`. A source here can
  be linked by `upptst` and run against a live server, which is how the window
  source is tested.

  `X11/Xlib.h` is included here, at file scope, before the namespace opens.
  Xlib defines names like `Window`, `Display`, `None` and `Status`, and pulling
  those into `namespace Upp` would be a mess to unpick; the headers below refer to
  them as `::Window` and `::Display` instead.

  Both production sources live here: `X11WindowSource` for the window list and
  `X11ScreenSource` for screen geometry. `X11ScreenSource` used to live in
  `uppsrc/DisplayManager`, which is a MAIN package, so no check package could link
  it and its dead-server path was verified by hand through a temporary console
  `main`. Moving it here is what lets a check ask it about a live server, a dead
  display and an unset `DISPLAY` as ordinary checks.

  The contract for what a source reports is frozen in
  `ai/info/desktop-model-view-split.md`, under "The X11 screen source" and "The
  system-wide window source".
*/

#include <DesktopModel/DesktopModel.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#ifdef Complex
#undef Complex
#endif


NAMESPACE_UPP

//! The X connection type, whichever name Xlib ended up with
/*!
  This is not paranoia, it is two different real answers. `uppsrc/CtrlCore/X11Gui.h`
  does `#define Display XDisplay` before including `X11/Xlib.h`, because U++
  already has `Upp::Display` from `uppsrc/Draw/Display.h`, and undefines the macro
  afterwards. The rename is permanent on the typedef, though, so in any
  translation unit that went through CtrlCore the type is `XDisplay` and there is
  no `::Display` to name. In this package built on its own, or in a check package
  that never includes CtrlCore, nothing renamed it and the type is `Display`.

  `GUI_X11` is defined once at the top of `X11Gui.h` and never undefined, so it
  says exactly which of the two worlds this translation unit is in. The first
  build of the window source in a GUI package failed with "no type named
  'Display' in the global namespace", which is the cost of guessing wrong.

  `Window` needs no such treatment: Xlib's window id is a complete integer type
  and nothing in U++ claims that name, so `::Window` works either way.
*/
#ifdef GUI_X11
typedef XDisplay DesktopDisplay;
#else
typedef ::Display DesktopDisplay;
#endif

#include "X11WindowSource.h"
#include "X11ScreenSource.h"
#include "X11WindowManager.h"
#include "X11FrameManager.h"

END_UPP_NAMESPACE

#endif // _DesktopX11_DesktopX11_h_