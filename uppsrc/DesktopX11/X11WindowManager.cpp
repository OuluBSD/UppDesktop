#ifndef _DesktopX11_X11WindowManager_cpp_
#define _DesktopX11_X11WindowManager_cpp_

#include "DesktopX11.h"
#include "X11Utils.h"

#include <X11/Xutil.h>

#if !defined flagX11
#error "X11WindowManager requires the X11 flag: build with a mainconfig that passes X11."
#endif

NAMESPACE_UPP

static bool anotherWMRunning = true;

static int AnotherWMErrorHandler(DesktopDisplay* dpy, XErrorEvent* error)
{
    if(error->error_code == BadAccess) {
        anotherWMRunning = true;
        return 0;
    }
    if(previous_xerror_handler)
        return previous_xerror_handler(dpy, error);
    return 0;
}

//! What CtrlCore stamps as res_name and res_class on everything it opens
/*!
  `TopWindow::Open` sets both halves of `WM_CLASS` to `GetExeTitle()`
  (uppsrc/CtrlCore/X11Top.cpp:317-321). Matching either half says the window was
  made by this process, and unlike the view's registered ids this check answers
  also for a window the view has not registered yet, which is every window
  between XCreateWindow and the view's next SyncWindows().
*/
static const String& OwnClassName()
{
    static String name = GetExeTitle().ToString();
    return name;
}

X11WindowManager& X11WindowManager::Create()
{
    static X11WindowManager instance;
    return instance;
}

X11WindowManager::X11WindowManager()
    : _connection_(NULL), _stop(true), _wake_window(None), _wake_atom(None) {}

X11WindowManager::~X11WindowManager()
{
    StopWorker();
    if(_connection_) {
        XSync((::Display*)_connection_, False);
        CloseConnection(_connection_);
        _connection_ = NULL;
    }
}

void X11WindowManager::StopWorker()
{
    if(!_thread_.IsOpen())
        return;
    _stop = true;
    // XNextEvent has no timeout, so the wait is ended by injecting an event: one
    // ClientMessage to the never-mapped wake window, read by the worker as "stop".
    if(_connection_ && _wake_atom != None) {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.display = (::Display*)_connection_;
        ev.xclient.window = _wake_window;
        ev.xclient.message_type = _wake_atom;
        ev.xclient.format = 32;
        XSendEvent((::Display*)_connection_, _wake_window, False, NoEventMask, &ev);
        XFlush((::Display*)_connection_);
    }
    _thread_.Wait();
    if(_connection_ && _wake_window != None)
        XDestroyWindow((::Display*)_connection_, _wake_window);
    _wake_window = None;
}

void X11WindowManager::SetOwnWindows(const Vector< ::Window >& own)
{
    RWMutex::WriteLock __(_own_mutex);
    _own.Clear();
    _own.Append(own);
}

bool X11WindowManager::IsOwnWindow(::Window win) const
{
    if(win == None)
        return false;
    {
        RWMutex::ReadLock __(_own_mutex);
        for(int i = 0; i < _own.GetCount(); i++)
            if(_own[i] == win)
                return true;
    }
    // The id check above needs the view to have synced. This one does not: the
    // window class was set on the X window before its map request could be
    // redirected, so it is already true when the frame decision runs.
    XClassHint hint;
    if(XGetClassHint((::Display*)_connection_, win, &hint)) {
        bool own = false;
        String expect = OwnClassName();
        if(hint.res_name && expect == hint.res_name)
            own = true;
        if(hint.res_class && expect == hint.res_class)
            own = true;
        if(hint.res_name)
            XFree(hint.res_name);
        if(hint.res_class)
            XFree(hint.res_class);
        if(own)
            return true;
    }
    return false;
}

//! Check if a window should be framed: not own, not DOCK-typed.
bool X11WindowManager::ShouldFrameWindow(::Window win) const
{
    if(IsOwnWindow(win))
        return false;
    if(PropertyListHas(_connection_, win, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK"))
        return false;
    return true;
}

bool X11WindowManager::Takeover()
{
    _connection_ = XOpenDisplay(NULL);
    if(!_connection_)
        return false;

    OwnedDisplays().Add(_connection_);
    InstallTolerantHandler();

    ::Window root = DefaultRootWindow((::Display*)_connection_);

    anotherWMRunning = false;
    // Save the handler we are displacing in a local, not in the shared
    // previous_xerror_handler: that global belongs to the tolerant handler
    // chain (X11Utils), and overwriting it here would make TolerantXErrorHandler
    // chain to itself and lose CtrlCore's original handler.
    XErrorHandler saved = XSetErrorHandler(AnotherWMErrorHandler);

    XSelectInput((::Display*)_connection_, root, SubstructureRedirectMask | SubstructureNotifyMask | PropertyChangeMask);
    XMapRaised((::Display*)_connection_, root);

    XSync((::Display*)_connection_, False);

    XSetErrorHandler(saved);
    if(anotherWMRunning) {
        Vector< DesktopDisplay* >& owned = OwnedDisplays();
        for(int i = 0; i < owned.GetCount(); i++)
            if(owned[i] == _connection_) {
                owned.Remove(i);
                break;
            }
        CloseConnection(_connection_);
        _connection_ = NULL;
        return false;
    }

    // The wake channel must exist before the worker starts, because the worker's
    // XNextEvent is the thing that has to be woken.
    _wake_atom = XInternAtom((::Display*)_connection_, "_UPP_WM_WAKE", False);
    _wake_window = XCreateSimpleWindow((::Display*)_connection_, root, 0, 0, 1, 1, 0, 0, 0);
    _stop = false;
    if(!_thread_.Run(callback(this, &X11WindowManager::EventLoop))) {
        // No worker means no one answers client requests: without a thread the
        // redirect would hold every map forever. Give the role back.
        _stop = true;
        XDestroyWindow((::Display*)_connection_, _wake_window);
        _wake_window = None;
        Vector< DesktopDisplay* >& owned = OwnedDisplays();
        for(int i = 0; i < owned.GetCount(); i++)
            if(owned[i] == _connection_) {
                owned.Remove(i);
                break;
            }
        CloseConnection(_connection_);
        _connection_ = NULL;
        return false;
    }

    return true;
}

void X11WindowManager::EventLoop()
{
    while(!_stop) {
        XEvent ev;
        // Blocks, and releases the display lock while it waits in threaded Xlib,
        // which is what lets the main thread inject the wake message.
        XNextEvent((::Display*)_connection_, &ev);
        if(_stop)
            break;
        if(ev.type == ClientMessage && ev.xclient.message_type == _wake_atom)
            break;
        HandleEvent(ev);
    }
}

void X11WindowManager::HandleEvent(const XEvent& xev)
{
    XEvent ev = xev;

    switch(ev.type) {
    case MapRequest:
        if(ev.xmaprequest.window != None) {
            // Check exclusion rules: don't frame own windows or DOCK windows
            bool frame_it = ShouldFrameWindow(ev.xmaprequest.window);
            if(!frame_it) {
                XMapWindow((::Display*)_connection_, ev.xmaprequest.window);
            } else {
                // A client that was minimized and is asked to map again already
                // has a frame: re-map it instead of building a second one.
                ::Window frame = _frame_mgr.GetFrameForClient(ev.xmaprequest.window);
                if(frame != None) {
                    XMapWindow((::Display*)_connection_, frame);
                    XMapWindow((::Display*)_connection_, ev.xmaprequest.window);
                } else {
                    _frame_mgr.CreateFrame(_connection_, ev.xmaprequest.window);
                }
            }
        }
        break;

    case ConfigureRequest:
        if(ev.xconfigurerequest.window != None) {
            ::Window frame = _frame_mgr.GetFrameForClient(ev.xconfigurerequest.window);
            if(frame != None) {
                // Client is framed - reconfigure the frame instead
                XWindowChanges changes;
                changes.x = ev.xconfigurerequest.x;
                changes.y = ev.xconfigurerequest.y;
                changes.width = ev.xconfigurerequest.width;
                changes.height = ev.xconfigurerequest.height;
                changes.border_width = ev.xconfigurerequest.border_width;
                changes.sibling = ev.xconfigurerequest.above;
                changes.stack_mode = ev.xconfigurerequest.detail;
                XConfigureWindow((::Display*)_connection_, frame,
                                ev.xconfigurerequest.value_mask, &changes);
            } else {
                frame = _frame_mgr.GetClientForFrame(ev.xconfigurerequest.window);
                if(frame != None) {
                    // Request is for the frame itself - reconfigure frame
                    XWindowChanges changes;
                    changes.x = ev.xconfigurerequest.x;
                    changes.y = ev.xconfigurerequest.y;
                    changes.width = ev.xconfigurerequest.width;
                    changes.height = ev.xconfigurerequest.height;
                    changes.border_width = ev.xconfigurerequest.border_width;
                    changes.sibling = ev.xconfigurerequest.above;
                    changes.stack_mode = ev.xconfigurerequest.detail;
                    XConfigureWindow((::Display*)_connection_, ev.xconfigurerequest.window,
                                    ev.xconfigurerequest.value_mask, &changes);
                } else {
                    // No frame, configure the client directly
                    XWindowChanges changes;
                    changes.x = ev.xconfigurerequest.x;
                    changes.y = ev.xconfigurerequest.y;
                    changes.width = ev.xconfigurerequest.width;
                    changes.height = ev.xconfigurerequest.height;
                    changes.border_width = ev.xconfigurerequest.border_width;
                    changes.sibling = ev.xconfigurerequest.above;
                    changes.stack_mode = ev.xconfigurerequest.detail;
                    XConfigureWindow((::Display*)_connection_, ev.xconfigurerequest.window,
                                    ev.xconfigurerequest.value_mask, &changes);
                }
            }
        }
        break;

    case DestroyNotify:
        if(ev.xdestroywindow.window != None) {
            ::Window frame = _frame_mgr.GetFrameForClient(ev.xdestroywindow.window);
            if(frame != None) {
                // Client was destroyed - clean up frame
                _frame_mgr.RemoveFrame(_connection_, ev.xdestroywindow.window);
            } else {
                frame = _frame_mgr.GetClientForFrame(ev.xdestroywindow.window);
                if(frame != None) {
                    // Frame was destroyed - clean up frame
                    _frame_mgr.RemoveFrameByFrame(_connection_, ev.xdestroywindow.window);
                }
            }
        }
        break;

    case UnmapNotify:
        if(ev.xunmap.window != None) {
            // A client that unmaps itself is being minimized or hidden, not
            // destroyed: hide its frame and keep it, so a later MapRequest
            // re-maps the same frame instead of building a second one.
            ::Window frame = _frame_mgr.GetFrameForClient(ev.xunmap.window);
            if(frame != None)
                XUnmapWindow((::Display*)_connection_, frame);
        }
        break;

    case CirculateRequest:
        if(ev.xcirculaterequest.place == PlaceOnTop)
            XRaiseWindow((::Display*)_connection_, ev.xcirculaterequest.window);
        else
            XLowerWindow((::Display*)_connection_, ev.xcirculaterequest.window);
        break;

    case ResizeRequest:
        // Client requested to resize horizontally or vertically
        XResizeWindow((::Display*)_connection_, ev.xresizerequest.window,
                      ev.xresizerequest.width, ev.xresizerequest.height);
        break;

    case ReparentNotify: {
        Window new_parent = ev.xreparent.parent;
        if(new_parent != DefaultRootWindow((::Display*)_connection_)) {
            // Client reparented to a frame we manage
        }
        break;
    }

    case PropertyNotify:
        break;

    default:
        break;
    }

    XFlush((::Display*)_connection_);
}

bool X11WindowManager::ProcessRequest()
{
    if(!_connection_)
        return false;

    // The worker owns this connection's queue while it runs: a second reader
    // would race it for events.
    if(_thread_.IsOpen())
        return false;

    if(!XPending((::Display*)_connection_))
        return false;

    XEvent ev;
    XNextEvent((::Display*)_connection_, &ev);
    if(ev.type == ClientMessage && ev.xclient.message_type == _wake_atom)
        return true;
    HandleEvent(ev);
    return true;
}

END_UPP_NAMESPACE

#endif
