#ifndef _DesktopManager_DesktopManager_cpp_
#define _DesktopManager_DesktopManager_cpp_

#include "DesktopManager.h"
#include "DesktopManagerIcons.h"

// This package is X11-only. Under a mainconfig without flagX11 the X11 code
// below used to compile to nothing, with no error and no warning: the build
// succeeded, a window opened, and no X11 code was in it. A comment cannot
// prevent that, so the build stops here instead.
#if !defined flagX11
#error "DesktopManager requires the X11 backend: build with a mainconfig that passes X11, for example \"X11\" = \"GUI X11\". Without flagX11 CtrlCore selects the GTK backend and the X11 calls below have no declarations."
#endif

#include <X11/Xlib.h>

NAMESPACE_UPP

DesktopManager::DesktopManager()
{
	// The watch connection is what lets the desktop learn that another client
	// opened a window without re-reading the whole list on a timer. CtrlCore
	// selects no root event mask, so this connection is the only way to be told.
	source.Watch();
	Title("DesktopManager");
	Sizeable().Zoomable();

#ifdef flagWMANAGER
	// Initialize the X11 window manager for the manager role
	if(mgr.Takeover()) {
		// Manager has successfully taken over - it's ready to handle requests
		// The ProcessRequest() calls need to be integrated with the event loop
	} else {
		// Another manager is already running - refuse and continue as plain desktop
	}
#endif
}

// root and workspaces are One<>, so they free themselves. ~Ctrl already calls
// Close(), which is a no-op for a window that was never opened, so no explicit
// teardown is needed here.
DesktopManager::~DesktopManager()
{
}

void DesktopManager::Initialize()
{
	if(!root.IsEmpty())
		return;

	// Create root window for desktop
	root.Create<TopWindow>();
	root->Title("Desktop");
	root->Sizeable().Zoomable();
	root->SetRect(Ctrl::GetVirtualScreenArea());
	root->Open();

	// Create initial workspace
	CreateWorkspace();
}

void DesktopManager::CreateWorkspace()
{
	// The model decides the index and reports it; the view opens a window for
	// exactly that index and never computes one of its own.
	int id = workspace.CreateWorkspace();

	One<TopWindow>& ws = workspaces.Add(new TopWindow());
	ws->Title(Format("Workspace %d", id + 1));
	ws->Sizeable().Zoomable();
	ws->SetRect(Ctrl::GetVirtualScreenArea());

	// The first workspace created by the model is the current one, so this is
	// the same condition as before, now read from the model.
	if(workspace.IsCurrent(id))
		ws->Open();
}

void DesktopManager::RemoveWorkspace()
{
	// The old code clamped current_workspace before removing and therefore left
	// it one past the end of the shorter list. The model clamps after the
	// removal, so this must not clamp anything.
	int id = workspace.Current();
	if(!workspace.RemoveWorkspace(id))
		return;
	// The One<TopWindow> closes and frees the removed workspace's window.
	workspaces.Remove(id);
	// Removing the current workspace moved the current index, and the window for
	// it was never opened. Opening it keeps Current() and "the open workspace"
	// in step, which is the same thing ApplySwitch() does after a switch.
	if(TopWindow* now = GetWorkspace(workspace.Current()))
		if(!now->IsOpen())
			now->Open();
}

void DesktopManager::NextWorkspace()
{
	ApplySwitch(workspace.SwitchToRelative(1));
}

void DesktopManager::PrevWorkspace()
{
	ApplySwitch(workspace.SwitchToRelative(-1));
}

int DesktopManager::GetCurrentWorkspace() const
{
	return workspace.Current();
}

void DesktopManager::SetCurrentWorkspace(int id)
{
	ApplySwitch(workspace.SwitchTo(id));
}

void DesktopManager::ApplySwitch(const WorkspaceSwitch& change)
{
	if(!change.changed)
		return;
	// Both indices come from the model. Recomputing either here is how a view
	// and a model end up disagreeing about which workspace is open.
	if(TopWindow* from = GetWorkspace(change.from))
		if(from->IsOpen())
			from->Close();
	if(TopWindow* to = GetWorkspace(change.to))
		if(!to->IsOpen())
			to->Open();
}

int DesktopManager::GetWorkspaceCount() const
{
	return workspace.GetCount();
}

bool DesktopManager::IsOwnWindow(::Window native) const
{
	if(native == None)
		return false;
	if(TopWindow* own = GetRoot())
		if(own->GetWindow() == native)
			return true;
	for(int i = 0; i < workspaces.GetCount(); i++)
		if(TopWindow* own = GetWorkspace(i))
			if(own->GetWindow() == native)
				return true;
	// The rest of this process's windows are not reachable as members: the
	// DesktopManager window itself, and whatever CtrlCore opened behind it, which
	// on X11 includes a clipboard window that has no title and no class. So
	// membership of this process is asked of the one API that knows it. This is
	// the whole remaining use of Ctrl::GetTopCtrls() here: the list of windows
	// comes from the server, and this call only decides what to leave out. Asking
	// the server instead is not possible -- a window carries no "who made me" --
	// and asking for the list from Ctrl again would restore the bug.
	Vector<Ctrl*> top = Ctrl::GetTopCtrls();
	for(int i = 0; i < top.GetCount(); i++)
		if(TopWindow* tw = dynamic_cast<TopWindow*>(top[i]))
			if(tw->GetWindow() == native)
				return true;
	return false;
}

Vector<WindowEntry> DesktopManager::SnapshotWindows()
{
	// The source opens and closes its own connection per query, so this is a
	// round trip rather than a walk of U++'s own window list. That is the point:
	// Ctrl::GetTopCtrls() walks a vector this process fills in when it constructs
	// a TopWindow, so it could only ever report the windows this desktop made.
	Vector<WindowEntry> seen = source.ReadWindows();
	Vector<WindowEntry> out;
	for(int i = 0; i < seen.GetCount(); i++) {
		if(IsOwnWindow(source.NativeOf(seen[i].handle)))
			continue;
		out.Add(seen[i]);
	}
	return out;
}

int DesktopManager::SyncWindows()
{
	// The same pair TaskBar::Sync() drives, DM5-001's contract: the snapshot
	// first, whose ReadWindows() also refreshed CurrentDesktop(), then the
	// workspace the model answers for, then the filtered sync. SetWorkspace()
	// is a no-op when the value is unchanged, so an ordinary sync pays nothing.
	Vector<WindowEntry> snapshot = SnapshotWindows();
	windows.SetWorkspace(source.CurrentDesktop());
	windows.Sync(snapshot);

#ifdef flagWMANAGER
	// Tell the manager which windows are this process's own, so it never frames
	// them. The ids change as workspaces open and close, so this is refreshed
	// with the same sync the rest of the list uses.
	Vector< ::Window > own;
	if(TopWindow* r = GetRoot())
		own.Add(( ::Window )r->GetWindow());
	for(int i = 0; i < workspaces.GetCount(); i++)
		if(TopWindow* ws = GetWorkspace(i))
			own.Add(( ::Window )ws->GetWindow());
	Vector<Ctrl*> top = Ctrl::GetTopCtrls();
	for(int i = 0; i < top.GetCount(); i++)
		if(TopWindow* tw = dynamic_cast<TopWindow*>(top[i]))
			own.Add(( ::Window )tw->GetWindow());
	mgr.SetOwnWindows(own);
#endif

	return windows.GetCount();
}

bool DesktopManager::WindowsChanged()
{
	// Drain the watch connection and report whether the server has reported a
	// change since the last read. Nothing here blocks: with an empty queue it is
	// one non-blocking check of the socket buffer.
	bool changed = source.Poll();

#ifdef flagWMANAGER
	// The manager's request queue is serviced by the manager's own thread, not
	// from here: it could not be answered from the main thread at all, because
	// CtrlCore's TopWindow::Open() blocks in XWindowEvent(VisibilityChangeMask)
	// until a manager answers the map request, and a main-thread loop would be
	// inside that wait. This call is a fallback drain only for the plain-desktop
	// case, where the manager took nothing over and the worker never ran;
	// X11WindowManager::ProcessRequest() itself refuses while the worker owns
	// the connection.
	mgr.ProcessRequest();
#endif

	return changed;
}

void DesktopManager::CycleFocus(int step)
{
	// This package has no polling timer, and before the split this method asked
	// Ctrl for the open windows itself. The sync therefore stays at the read, so
	// a focus request works before the first Paint and after a window opens or
	// closes between two paints.
	SyncWindows();
	// The model answers with a handle; the view raises the window. It used to
	// answer with a computed index, and it computed both the -1 start for an
	// unfocused desktop and the modulo here, which is why backwards cycling from
	// no focus landed on the second-to-last window.
	int handle = windows.NextFocus(step);
	if(handle == WindowListModel::NoHandle)
		return;
	// Every entry in the list is a window this process did not create, because
	// SnapshotWindows() filters its own out. There is no TopWindow* to call
	// SetForeground() on, so the two X operations are the source's. Focus is a
	// request to the window manager rather than XSetInputFocus, which would fight
	// it for the keyboard.
	source.Raise(handle);
	source.Focus(handle);
}

void DesktopManager::FocusNext()
{
	CycleFocus(1);
}

void DesktopManager::FocusPrev()
{
	CycleFocus(-1);
}

void DesktopManager::RaiseWindow(TopWindow* win)
{
	if(win)
		win->SetForeground();
}

void DesktopManager::LowerWindow(TopWindow* win)
{
	if(!win)
		return;
	// U++ exposes no per-window Z-order API; the X11 backend only raises inside
	// Ctrl::SetWndForeground(). Send the stacking request directly so a desktop
	// manager can push a window behind its siblings.
	Window xwin = win->GetWindow();
	if(xwin)
		XLowerWindow(Xdisplay, xwin);
}

void DesktopManager::Paint(Draw& w)
{
	Size sz = GetSize();
	w.DrawRect(sz, SColorFace());

	// Everything below is read from the model. The (untitled) substitution
	// happens in SnapshotWindows(), where the entry is built, so the model is
	// never asked to invent text.
	//
	// The sync happens here because this package has no polling timer: before the
	// split this method asked Ctrl for the open windows every time it painted.
	// Syncing at the read keeps the list as current as it was, and keeps the
	// model out of Paint()'s own logic.
	//
	// Only re-read when the watch connection has reported a change. A repaint
	// with nothing having happened on the display now costs no window queries at
	// all, where this used to open an X connection, walk the tree and read five
	// properties per window on every single paint.
	if(WindowsChanged())
		SyncWindows();
	w.DrawText(8, 6, Format("Workspace %d of %d", workspace.Current() + 1, workspace.GetCount()),
	           StdFont(14), SColorText());
	w.DrawText(8, 24, (const char *)windows.ToString(), StdFont(), SColorText());

	int y = 44;
	for(int i = 0; i < windows.GetCount(); i++) {
		Color fg = windows.IsActiveAt(i) ? SColorHighlightText() : SColorText();
		w.DrawText(16, y, Format("%d. %s", i + 1, (const char *)windows.TitleAt(i)),
		           StdFont(), fg);
		y += 18;
	}
}

END_UPP_NAMESPACE

#endif // _DesktopManager_DesktopManager_cpp_
