#ifndef _DesktopX11_X11WindowManager_h_
#define _DesktopX11_X11WindowManager_h_

#include "DesktopX11.h"
#include "X11FrameManager.h"

//! The window manager role: takes over the root and answers client requests
/*!
  Constructed once and takes over the root window when the manager role is
  taken. While the manager holds SubstructureRedirect, every map request from
  every client is queued on the manager's own connection and the window stays
  unmapped until the manager answers it.

  That queue is therefore serviced by this manager's own thread. It cannot be
  serviced from the application's main thread: CtrlCore's TopWindow::Open()
  ends in a blocking XWindowEvent(VisibilityChangeMask) that only returns once
  a manager has answered the map request, so a loop that drains from the main
  thread would deadlock the very first window it opens. The worker runs the
  same handling ProcessRequest() describes, from a connection only it touches;
  CtrlCore calls XInitThreads() before opening anything
  (uppsrc/CtrlCore/X11App.cpp:406), so the two connections can be used from
  two threads.

  The manager role is opt-in: only the DesktopManager process started with
  the WMANAGER mainconfig flag constructs one. A takeover that finds another
  manager running returns false, the worker is never started, and the process
  continues as a plain desktop, which is the deliberate refusal the WM
  contract freezes.
*/
class X11WindowManager {
public:
	//! Default constructor
	X11WindowManager();

	//! Constructs the manager. Called once when the manager role is taken.
	static X11WindowManager& Create();

	//! Takes over the window manager role on the root window.
	//! Returns true if successful (no other manager running), false otherwise.
	//! Uses a dedicated connection, and on success also starts the thread that
	//! services its event queue.
	bool Takeover();

	//! Processes one pending request event when nothing else services the
	//! connection. False when the worker thread owns the queue, which is always
	//! the case after a successful Takeover(); a main thread calling
	//! XNextEvent on the same connection would race it.
	bool ProcessRequest();

	//! True while the worker thread services the manager's connection.
	bool IsRunning() const { return _thread_.IsOpen(); }

	//! The windows this process itself owns, as the view reports them. Belt to
	//! the suspenders of IsOwnWindow(): the class check catches a window whose
	//! id the view has not registered yet, which is every window between
	//! XCreateWindow and the view's next sync.
	void SetOwnWindows(const Vector< ::Window >& own);

	//! Returns the manager's dedicated connection.
	DesktopDisplay* GetConnection() const { return _connection_; }

	~X11WindowManager();

private:
	X11WindowManager(const X11WindowManager&) = delete;
	X11WindowManager& operator=(const X11WindowManager&) = delete;

	//! The worker body: blocks on XNextEvent and dispatches each event, until a
	//! wake message says to stop.
	void EventLoop();

	//! One event. Called from the worker while it runs and from ProcessRequest()
	//! when it does not, and it is the only place that touches the connection
	//! after Takeover() returns.
	void HandleEvent(const XEvent& ev);

	//! True when the window should get a frame. False for this process's own
	//! windows, by id or by window class, and for DOCK-typed clients, all of
	//! which the contract leaves unframed.
	bool ShouldFrameWindow(::Window win) const;

	//! True when a native window belongs to this process, either by an id the
	//! view registered or by the window class CtrlCore stamps on everything it
	//! opens.
	bool IsOwnWindow(::Window win) const;

	//! Stops the worker and waits for it. Idempotent.
	void StopWorker();

	DesktopDisplay*    _connection_;
	X11FrameManager    _frame_mgr;

	//! The view's own windows and the lock guarding them: the view writes from
	//! the main thread, the frame decision reads from the worker. Mutable
	//! because the frame decision may run from a const method.
	mutable RWMutex    _own_mutex;
	Vector< ::Window > _own;

	Thread             _thread_;
	bool               _stop;
	//! A 1x1 never-mapped window on the manager connection and the atom of the
	//! ClientMessage sent to it on shutdown: XNextEvent has no timeout, so the
	//! only way out of its wait is an event.
	::Window           _wake_window;
	Atom               _wake_atom;
};

#endif // _DesktopX11_X11WindowManager_h_