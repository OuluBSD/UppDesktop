#ifndef _DesktopX11_X11FrameManager_cpp_
#define _DesktopX11_X11FrameManager_cpp_

#include "DesktopX11.h"
#include <DesktopModel/FrameGeometry.h>

#if !defined flagX11
#error "X11FrameManager requires the X11 flag."
#endif

NAMESPACE_UPP

X11FrameManager::X11FrameManager() {}

::Window X11FrameManager::CreateFrame(DesktopDisplay* dpy, ::Window client)
{
	if(!dpy || !client)
		return None;

	// Get client window attributes so we can place the frame correctly
	XWindowAttributes attr;
	if(!XGetWindowAttributes((::Display*)dpy, client, &attr))
		return None;

	// Use model-side geometry for frame insets
	FrameInsets insets = FrameGeometry::DefaultInsets();
	Size frame_size = FrameGeometry::FrameSizeForClient(Size(attr.width, attr.height), insets);

	// Create the frame window as a child of the root
	::Window root = DefaultRootWindow((::Display*)dpy);
	unsigned long frame_border = 0;
	unsigned long frame_value_mask = CWOverrideRedirect | CWEventMask | CWSaveUnder;
	XSetWindowAttributes frame_attrs;
	frame_attrs.override_redirect = False;
	frame_attrs.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
	                         ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
	                         EnterWindowMask | LeaveWindowMask | ExposureMask;
	frame_attrs.save_under = True;

	::Window frame = XCreateWindow((::Display*)dpy, root, attr.x, attr.y,
	                              frame_size.cx, frame_size.cy, frame_border,
	                              CopyFromParent, (unsigned int)InputOutput,
	                              CopyFromParent, frame_value_mask, &frame_attrs);

	if(!frame)
		return None;

	// Insert client window into the save-set so it is reparented back to root
	// when this process exits
	XChangeSaveSet((::Display*)dpy, client, SetModeInsert);

	// Set client border to 0 - the frame provides the border
	XSetWindowBorderWidth((::Display*)dpy, client, 0);

	// Reparent the client into the frame's client area at correct position
	// Client area starts at (insets.left, insets.top) within the frame
	XReparentWindow((::Display*)dpy, client, frame, insets.left, insets.top);

	// Map the client (its own map request was redirected to us, so it is still
	// unmapped) and then the frame, so both become viewable.
	XMapWindow((::Display*)dpy, client);
	XMapWindow((::Display*)dpy, frame);

	XFlush((::Display*)dpy);

	frame_for_client.Add(client);
	client_for_frame.Add(frame);

	return frame;
}

void X11FrameManager::RemoveFrame(DesktopDisplay* dpy, ::Window client)
{
	if(!dpy || !client)
		return;

	::Window frame = GetFrameForClient(client);
	if(frame == None)
		return;

	// Reparent the client back to the root
	::Window root = DefaultRootWindow((::Display*)dpy);
	XReparentWindow((::Display*)dpy, client, root, 0, 0);

	// Remove the client from the save-set
	XChangeSaveSet((::Display*)dpy, client, SetModeDelete);

	// Destroy the frame window
	XDestroyWindow((::Display*)dpy, frame);

	// Remove from our bookkeeping
	for(int i = 0; i < frame_for_client.GetCount(); i++) {
		if(frame_for_client[i] == client) {
			frame_for_client.Remove(i);
			client_for_frame.Remove(i);
			break;
		}
	}

	XFlush((::Display*)dpy);
}

// Remove frame by frame window (for DestroyNotify on frame)
void X11FrameManager::RemoveFrameByFrame(DesktopDisplay* dpy, ::Window frame)
{
	if(!dpy || !frame)
		return;

	::Window client = GetClientForFrame(frame);
	if(client == None)
		return;

	RemoveFrame(dpy, client);
}

::Window X11FrameManager::GetFrameForClient(::Window client) const
{
	for(int i = 0; i < frame_for_client.GetCount(); i++) {
		if(frame_for_client[i] == client)
			return client_for_frame[i];
	}
	return None;
}

::Window X11FrameManager::GetClientForFrame(::Window frame) const
{
	for(int i = 0; i < client_for_frame.GetCount(); i++) {
		if(client_for_frame[i] == frame)
			return frame_for_client[i];
	}
	return None;
}

END_UPP_NAMESPACE

#endif