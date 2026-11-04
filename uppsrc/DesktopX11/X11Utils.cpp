#ifndef _DesktopX11_X11Utils_cpp_
#define _DesktopX11_X11Utils_cpp_

#include "DesktopX11.h"

#if !defined flagX11
#error "X11Utils requires the X11 flag."
#endif

NAMESPACE_UPP

Vector< DesktopDisplay* >& OwnedDisplays()
{
	static Vector< DesktopDisplay* > owned;
	return owned;
}

XErrorHandler previous_xerror_handler = NULL;

int TolerantXErrorHandler(DesktopDisplay* dpy, XErrorEvent* error)
{
	switch(error->error_code) {
	case BadWindow:
	case BadDrawable:
	case BadMatch:
	case BadAtom: {
		Vector< DesktopDisplay* >& owned = OwnedDisplays();
		for(int i = 0; i < owned.GetCount(); i++)
			if(owned[i] == dpy)
				return 0;
		break;
	}
	}
	if(previous_xerror_handler)
		return previous_xerror_handler(dpy, error);
	return 0;
}

void InstallTolerantHandler()
{
	static bool installed = false;
	if(!installed) {
		previous_xerror_handler = XSetErrorHandler(TolerantXErrorHandler);
		installed = true;
	}
}

DesktopDisplay* OpenConnection()
{
	DesktopDisplay* dpy = (DesktopDisplay*)XOpenDisplay(NULL);
	if(!dpy)
		return NULL;
	InstallTolerantHandler();
	OwnedDisplays().Add(dpy);
	return dpy;
}

void CloseConnection(DesktopDisplay* dpy)
{
	if(!dpy)
		return;
	XSync((::Display*)dpy, False);
	Vector< DesktopDisplay* >& owned = OwnedDisplays();
	for(int i = 0; i < owned.GetCount(); i++)
		if(owned[i] == dpy) {
			owned.Remove(i);
			break;
		}
	XCloseDisplay((::Display*)dpy);
}

Atom XAtomOf(DesktopDisplay* dpy, const char* name)
{
	return XInternAtom((::Display*)dpy, name, True);
}

Vector<long> ReadPropertyLongs(DesktopDisplay* dpy, ::Window win, const char* name, Atom type)
{
	Vector<long> out;
	Atom property = XAtomOf(dpy, name);
	if(property == None)
		return out;
	unsigned char* data = NULL;
	unsigned long nitems = 0;
	unsigned long bytes_after = 0;
	int format = 0;
	int status = XGetWindowProperty((::Display*)dpy, win, property, 0, LONG_MAX, False, type, &property, &format, &nitems, &bytes_after, &data);
	if(status != Success || !data || format != 32) {
		if(data)
			XFree(data);
		return out;
	}
	long* ptr = (long*)data;
	for(unsigned long i = 0; i < nitems; i++)
		out.Add(ptr[i]);
	XFree(data);
	return out;
}

//! Decodes property text bytes to String (UTF-8 or Latin-1 kept as-is)
String DecodeText(const unsigned char* data, unsigned long items)
{
	if(!data || !items)
		return String();
	int len = (int)items;
	while(len > 0 && data[len - 1] == '\0')
		len--;
	return String((const char*)data, len);
}

//! Reads a window property as text
String ReadPropertyText(DesktopDisplay* dpy, ::Window win, const char* name, Atom type)
{
	Atom property = XAtomOf(dpy, name);
	if(property == None)
		return String();
	Atom actual_type;
	int actual_format;
	unsigned long items = 0, after = 0;
	unsigned char* data = NULL;
	if(XGetWindowProperty((::Display*)dpy, win, property, 0, 1024, False, type,
	                      &actual_type, &actual_format, &items, &after, &data) != Success)
		return String();
	if(data) {
		if(actual_format == 8)
			return DecodeText(data, items);
		XFree(data);
	}
	return String();
}

//! True when the window's property list holds atom. Used for _NET_WM_STATE and
//! _NET_WM_WINDOW_TYPE, both of which are lists of atoms rather than one value.
bool PropertyListHas(DesktopDisplay* dpy, ::Window win, const char* name, const char* wanted)
{
	Vector<long> atoms = ReadPropertyLongs(dpy, win, name, XA_ATOM);
	if(atoms.IsEmpty())
		return false;
	Atom target = XAtomOf(dpy, wanted);
	for(int i = 0; i < atoms.GetCount(); i++)
		if(atoms[i] == (long)target)
			return true;
	return false;
}

END_UPP_NAMESPACE

#endif