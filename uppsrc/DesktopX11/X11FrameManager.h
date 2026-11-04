#ifndef _DesktopX11_X11FrameManager_h_
#define _DesktopX11_X11FrameManager_h_

#include "DesktopX11.h"

//! Frame manager: creates and manages frame windows for client windows
/*!
  The frame is an X window that contains the client window and its decorations
  (titlebar, buttons, border, resize handles). The frame owns the client
  (SaveSet insert, reparent with border=0) and carries the event mask that
  intercepts the client's configure requests.
*/
class X11FrameManager {
public:
	//! Default constructor
	X11FrameManager();

	//! Creates a frame for the given client window.
	//! Returns the frame window id, or None on failure.
	::Window CreateFrame(DesktopDisplay* dpy, ::Window client);

	//! Removes the frame for the given client window.
	//! Reparents the client back to the root window.
	void RemoveFrame(DesktopDisplay* dpy, ::Window client);

	//! Removes the frame by frame window id (for DestroyNotify on frame).
	void RemoveFrameByFrame(DesktopDisplay* dpy, ::Window frame);

	//! Returns the frame window for a given client, or None if not framed.
	::Window GetFrameForClient(::Window client) const;

	//! Returns the client window for a given frame, or None if not a frame.
	::Window GetClientForFrame(::Window frame) const;

private:
	Vector< ::Window > frame_for_client;
	Vector< ::Window > client_for_frame;
};

#endif // _DesktopX11_X11FrameManager_h_