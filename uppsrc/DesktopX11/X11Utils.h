#ifndef _DesktopX11_X11Utils_h_
#define _DesktopX11_X11Utils_h_

#include "DesktopX11.h"

NAMESPACE_UPP

//! The connections this source has open, so its X errors can be told from others'
/*!
  Xlib error handlers are process-global, so this source can only swallow an
  error it can prove came from a connection it opened. Inside a GUI process the
  previous handler is CtrlCore's, which panics on any error, and it must keep
  doing so for CtrlCore's own windows; a static that scopes by connection keeps
  both properties.
*/
Vector< DesktopDisplay* >& OwnedDisplays();

//! Errors a source expects from a desktop that does not stand still
/*!
  A stale `_NET_CLIENT_LIST` entry for a window that died under the query, a
  raise or focus request that lost the race with a close, a property of a type a
  vanished window used to have. Swallowed, these are a skipped row; unswallowed,
  they are the desktop ending -- CtrlCore's handler panics on any error, and the
  first live run of this source against a real desktop died on exactly this
  class of error.
*/
int TolerantXErrorHandler(DesktopDisplay* dpy, XErrorEvent* error);

void InstallTolerantHandler();

//! Opens a connection and registers it, so the handler knows whose errors these are
DesktopDisplay* OpenConnection();

//! Closes a connection and unregisters it
void CloseConnection(DesktopDisplay* dpy);

//! Returns atom for name, or None if not found
Atom XAtomOf(DesktopDisplay* dpy, const char* name);

//! Reads a property as array of longs. Empty when the property is
//! absent, when the server has no such atom, or when it has the wrong format,
//! which is how "this window does not say" is distinguished from "this window
//! says zero".
Vector<long> ReadPropertyLongs(DesktopDisplay* dpy, ::Window win, const char* name, Atom type);

//! Decodes property text bytes to String (UTF-8 or Latin-1 kept as-is)
String DecodeText(const unsigned char* data, unsigned long items);

//! Reads a window property as text
String ReadPropertyText(DesktopDisplay* dpy, ::Window win, const char* name, Atom type);

//! True when the window's property list holds atom. Used for _NET_WM_STATE and
//! _NET_WM_WINDOW_TYPE, both of which are lists of atoms rather than one value.
bool PropertyListHas(DesktopDisplay* dpy, ::Window win, const char* name, const char* wanted);

//! Previous X error handler (before tolerant handler was installed)
//! Exposed so manager can temporarily override during takeover
extern XErrorHandler previous_xerror_handler;

END_UPP_NAMESPACE

#endif // _DesktopX11_X11Utils_h_