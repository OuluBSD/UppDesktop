#ifndef _DesktopX11_X11WindowSource_cpp_
#define _DesktopX11_X11WindowSource_cpp_

#include "DesktopX11.h"
#include "X11Utils.h"

// Without this flag the package would build and link -lX11 with no X11 code
// reachable, and ReadWindows() would report an empty desktop for a reason that has
// nothing to do with there being no windows.
#if !defined flagX11
#error "X11WindowSource requires the X11 flag: build with a mainconfig that passes X11, for example \"X11\" = \"X11\"."
#endif

NAMESPACE_UPP

//! The first window the source sees, in the order it stacks them
Vector<::Window> X11WindowSource::Enumerate(DesktopDisplay* dpy) const
{
	Vector<::Window> out;
	::Window root = DefaultRootWindow(dpy);

	// _NET_CLIENT_LIST is the authoritative list and it is in stacking order,
	// bottom to top. It only exists when an EWMH-compliant window manager is
	// running: on a bare Xvfb, `xprop -root _NET_CLIENT_LIST` answers "no such
	// atom on any window". Reporting nothing there would make every headless test
	// pass for the wrong reason, so the tree walk below is the real path on a
	// developer machine and _NET_CLIENT_LIST is the better one when it exists.
	Vector<long> clients = ReadPropertyLongs(dpy, root, "_NET_CLIENT_LIST", XA_WINDOW);
	for(int i = 0; i < clients.GetCount(); i++)
		out.Add((::Window)clients[i]);

	if(!out.IsEmpty())
		return out;

	::Window returned_root, returned_parent, *children = NULL;
	unsigned int child_count = 0;
	if(!XQueryTree(dpy, root, &returned_root, &returned_parent, &children, &child_count))
		return out;
	if(children) {
		for(unsigned int i = 0; i < child_count; i++)
			out.Add(children[i]);
		XFree(children);
	}
	return out;
}

bool X11WindowSource::IsManaged(DesktopDisplay* dpy, ::Window win)
{
	XWindowAttributes attr;
	if(!XGetWindowAttributes(dpy, win, &attr))
		return false;
	// A window that is not on screen is not a task. XQueryTree lists every child
	// of the root, mapped or not, and a desktop that lists unmapped windows gets
	// entries the user cannot see or click: on Xvfb, running only
	// DesktopManager, the server had three mapped windows and the tree walk found
	// four. `xdotool search --onlyvisible` reported the three, which is what made
	// the difference visible. IsViewable excludes both unmapped windows and
	// windows clipped away by an ancestor.
	if(attr.map_state != IsViewable)
		return false;
	// An override-redirect window never participates in stacking or focus, so it
	// is not a window the desktop manages. Menus and tooltips are the usual ones.
	if(attr.override_redirect)
		return false;
	// A minimized window is still a managed window, but it is not somewhere focus
	// can go, so listing it would produce a taskbar entry that does nothing.
	if(PropertyListHas(dpy, win, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN"))
		return false;
	// Tooltips and docks are decoration, not tasks. The desktop's own windows
	// carry _NET_WM_WINDOW_TYPE_DESKTOP and are excluded here rather than by the
	// view, so a bare source is already sane without the caller filtering.
	if(PropertyListHas(dpy, win, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_TOOLTIP"))
		return false;
	if(PropertyListHas(dpy, win, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK"))
		return false;
	if(PropertyListHas(dpy, win, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP"))
		return false;
	return true;
}

String X11WindowSource::ReadTitle(DesktopDisplay* dpy, ::Window win)
{
	// _NET_WM_NAME is a UTF8_STRING property; WM_NAME is the legacy STRING that
	// predates UTF-8 titles and is still what some toolkits set. Reading only the
	// legacy one loses non-ASCII titles, reading only the new one loses titles
	// from clients that never adopted it.
	String title = ReadPropertyText(dpy, win, "_NET_WM_NAME", AnyPropertyType);
	if(title.IsEmpty())
		title = ReadPropertyText(dpy, win, "WM_NAME", XA_STRING);
	return title;
}

int X11WindowSource::ReadDesktop(DesktopDisplay* dpy, ::Window win)
{
	Vector<long> desktop = ReadPropertyLongs(dpy, win, "_NET_WM_DESKTOP", XA_CARDINAL);
	if(desktop.IsEmpty())
		return WindowSource::AllDesktops;
	// EWMH gives 0xFFFFFFFF for a window on every desktop, a sticky window. It
	// must not be read as a workspace index.
	if(desktop[0] == 0xFFFFFFFFL || desktop[0] < 0)
		return WindowSource::AllDesktops;
	return (int)desktop[0];
}

X11WindowSource::X11WindowSource() {}

X11WindowSource::~X11WindowSource()
{
	// The watch connection is the one piece of Xlib state this object owns, and
	// it is closed here rather than left to a caller that has no reason to know
	// it exists.
	if(watch_dpy)
		CloseConnection(watch_dpy);
}

bool X11WindowSource::Watch()
{
	if(watch_dpy)
		return false;
	watch_dpy = (DesktopDisplay*)OpenConnection();
	if(!watch_dpy)
		return false;
	// PropertyChange is selected as well as SubstructureNotify because a window
	// can change its title, its workspace or its state without being created,
	// mapped or destroyed, and a taskbar that only watched for open and close
	// would show a stale title until something else happened to move the mouse.
	XSelectInput(watch_dpy, DefaultRootWindow(watch_dpy),
	             SubstructureNotifyMask | PropertyChangeMask);
	XFlush(watch_dpy);
	return true;
}

bool X11WindowSource::Poll()
{
	if(!watch_dpy)
		return dirty;
	// XPending never blocks: it reports what is already in the socket buffer. An
	// idle tick is one syscall, where the old code opened a connection, walked the
	// tree and re-read five properties per window.
	while(XPending(watch_dpy) > 0) {
		XEvent ev;
		XNextEvent(watch_dpy, &ev);
		switch(ev.type) {
		case CreateNotify:
		case DestroyNotify:
		case MapNotify:
		case UnmapNotify:
		case ReparentNotify:
		case ConfigureNotify:
		case PropertyNotify:
			dirty = true;
			break;
		default:
			break;
		}
	}
	return dirty;
}

int X11WindowSource::HandleOf(::Window native)
{
	if(!native)
		return WindowListModel::NoHandle;
	for(int i = 0; i < natives.GetCount(); i++)
		if(natives[i] == native)
			return handles[i];
	int handle = next_handle++;
	natives.Add(native);
	handles.Add(handle);
	return handle;
}

void X11WindowSource::Forget(::Window native)
{
	for(int i = 0; i < natives.GetCount(); i++)
		if(natives[i] == native) {
			natives.Remove(i);
			handles.Remove(i);
			return;
		}
}

::Window X11WindowSource::NativeOf(int handle) const
{
	for(int i = 0; i < handles.GetCount(); i++)
		if(handles[i] == handle)
			return natives[i];
	return None;
}

void X11WindowSource::Clear()
{
	natives.Clear();
	handles.Clear();
}

Vector<WindowEntry> X11WindowSource::ReadWindows()
{
	Vector<WindowEntry> out;
	// Handles are interned here and survive the query, which is what
	// WindowListModel::Sync() needs: it drops a handle the source stopped
	// reporting, so a handle that changed identity between two queries would
	// reorder the list and break focus cycling.
	DesktopDisplay* dpy = (DesktopDisplay*)OpenConnection();
	if(!dpy)
		return out;

	::Window active = None;
	Vector<long> active_prop = ReadPropertyLongs(dpy, DefaultRootWindow(dpy), "_NET_ACTIVE_WINDOW", XA_WINDOW);
	if(!active_prop.IsEmpty())
		active = (::Window)active_prop[0];

	// The manager's current workspace, read on this same connection, per the
	// contract's first per-workspace decision: read per query, never carried
	// across queries. Absent on a bare server -- `ReadPropertyLongs` returns
	// empty, because the atom does not exist -- and AllDesktops is the honest
	// answer there, the one every workspace filter degenerates to pass-all on.
	current_desktop = WindowSource::AllDesktops;
	Vector<long> current_prop = ReadPropertyLongs(dpy, DefaultRootWindow(dpy), "_NET_CURRENT_DESKTOP", XA_CARDINAL);
	if(!current_prop.IsEmpty() && current_prop[0] != 0xFFFFFFFFL && current_prop[0] >= 0)
		current_desktop = (int)current_prop[0];

	Vector<::Window> wins = Enumerate(dpy);

	// Release handles for windows the server no longer has. Without this the
	// intern table grows by one entry for every window the session has ever
	// opened, which on a long desktop is a slow leak that never shows up as a
	// failing test. The comparison is against the raw enumeration and not
	// against the filtered list below, because a window that is still there but
	// currently filtered out -- minimized, say -- has not gone anywhere and must
	// keep its handle or it would be dropped and re-appended, reordering the list.
	for(int i = 0; i < natives.GetCount(); i++) {
		bool still_there = false;
		for(int j = 0; j < wins.GetCount(); j++)
			if(wins[j] == natives[i]) {
				still_there = true;
				break;
			}
		if(!still_there) {
			natives.Remove(i);
			handles.Remove(i);
			i--;
		}
	}

	for(int i = 0; i < wins.GetCount(); i++) {
		if(!IsManaged(dpy, wins[i]))
			continue;
		WindowEntry entry;
		entry.handle = HandleOf(wins[i]);
		String title = ReadTitle(dpy, wins[i]);
		// The substitution is the same one DesktopManager::SnapshotWindows()
		// makes, kept here so the fallback text is decided in one place rather
		// than once per view.
		entry.title = title.IsEmpty() ? "(untitled)" : title;
		entry.active = wins[i] == active;
		// The workspace snapshot the model's filter reads. A sticky window gets
		// AllDesktops here and passes every filter; no desktop property at all,
		// the bare-server case, is AllDesktops too.
		entry.desktop = ReadDesktop(dpy, wins[i]);
		out.Add(entry);
	}

	CloseConnection(( ::Display*)dpy);
	// The flag meant "the server changed since you last read", and this is that
	// read. Clearing it here rather than in the caller means a view cannot forget
	// to and then spin re-reading for the rest of the session.
	dirty = false;
	return out;
}

int X11WindowSource::DesktopOf(int handle) const
{
	if(!handle)
		return WindowSource::AllDesktops;
	::Window native = NativeOf(handle);
	if(native == None)
		return WindowSource::AllDesktops;
	DesktopDisplay* dpy = (DesktopDisplay*)OpenConnection();
	if(!dpy)
		return WindowSource::AllDesktops;
	int desktop = ReadDesktop(dpy, native);
	CloseConnection(dpy);
	return desktop;
}

bool X11WindowSource::Raise(int handle)
{
	if(!handle)
		return false;
	::Window native = NativeOf(handle);
	if(native == None)
		return false;
	DesktopDisplay* dpy = (DesktopDisplay*)OpenConnection();
	if(!dpy)
		return false;
	XRaiseWindow(dpy, native);
	XFlush(dpy);
	CloseConnection(dpy);
	return true;
}

void X11WindowSource::RaiseNative(::Window native)
{
	if(!native)
		return;
	DesktopDisplay* dpy = (DesktopDisplay*)OpenConnection();
	if(!dpy)
		return;
	XRaiseWindow(dpy, native);
	XFlush(dpy);
	CloseConnection(dpy);
}

bool X11WindowSource::Focus(int handle)
{
	if(!handle)
		return false;
	::Window native = NativeOf(handle);
	if(native == None)
		return false;
	DesktopDisplay* dpy = (DesktopDisplay*)OpenConnection();
	if(!dpy)
		return false;
	// Focus is requested rather than set. XSetInputFocus would override the
	// window manager's own decision, and on a desktop with a running WM that is
	// how a client ends up fighting the WM for the keyboard. _NET_ACTIVE_WINDOW
	// with source indication 2 is the request the EWMH defines for a pager, and
	// this desktop is one.
	XEvent ev;
	Zero(ev);
	ev.xclient.type = ClientMessage;
	ev.xclient.window = native;
	ev.xclient.message_type = XAtomOf(dpy, "_NET_ACTIVE_WINDOW");
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 2;            // source indication: pager
	ev.xclient.data.l[1] = CurrentTime;
	ev.xclient.data.l[2] = 0;
	Status sent = XSendEvent(dpy, DefaultRootWindow(dpy), False,
	                         SubstructureNotifyMask | SubstructureRedirectMask, &ev);
	XFlush(dpy);
	CloseConnection(dpy);
	return sent != 0;
}

END_UPP_NAMESPACE

#endif // _DesktopX11_X11WindowSource_cpp_