#ifndef _DesktopManager_DesktopManager_h_
#define _DesktopManager_DesktopManager_h_

//! X11 DesktopManager based on fluxbox architecture
//! 
//! Adapted from fluxbox's Screen.cc, Workspace.cc, Window.cc, and other
//! core components at ~/Ohjelmat/fluxbox/src/.
//! 
//! Provides virtual desktop management, window placement, and integration
//! with X11 display via CtrlCore X11 backend.

#include <CtrlLib/CtrlLib.h>
#include <DesktopModel/DesktopModel.h>
#include <DesktopX11/DesktopX11.h>

#include "DesktopManagerIcons.h"

NAMESPACE_UPP

//! Main desktop window manager class
/*!
  Implements virtual desktop workspaces, window stacking, focus management,
  and integration with taskbar and display manager.

  The authoritative state lives in two models from `uppsrc/DesktopModel`:
  `WorkspaceModel` owns how many workspaces exist and which is current, and
  `WindowListModel` owns the ordered window list and its focus index. This class
  opens and closes the `TopWindow`s and paints. It owns no index arithmetic of
  its own: every transition returns what has to happen and this class only
  carries it out.

  The window list comes from `X11WindowSource`, which asks the X server for
  every window it manages. It used to come from `Ctrl::GetTopCtrls()`, which can
  only return windows this process created, so the desktop could see itself and
  nothing else. The handle table that view needed is gone with it: the source
  interns a handle per window and resolves in both directions, and a window this
  process owns is not a `TopWindow*` that can be activated, so the source owns
  the raise and the focus request for it.
*/
class DesktopManager : public TopWindow {
public:
	DesktopManager();
	virtual ~DesktopManager();

	//! Initialize the desktop manager and create initial windows
	void Initialize();

	//! Create a new virtual workspace
	void CreateWorkspace();

	//! Remove a workspace
	void RemoveWorkspace();

	//! Switch to next workspace
	void NextWorkspace();

	//! Switch to previous workspace
	void PrevWorkspace();

	//! Get current workspace
	int GetCurrentWorkspace() const;

	//! Set the current workspace
	void SetCurrentWorkspace(int id);

	//! Get number of workspaces
	int GetWorkspaceCount() const;

	//! Focus next window
	void FocusNext();

	//! Focus previous window
	void FocusPrev();

	//! Raise a window
	void RaiseWindow(TopWindow* win);

	//! Lower a window
	void LowerWindow(TopWindow* win);

	//! Get the root window, null until Initialize() runs
	TopWindow* GetRoot() const { return const_cast<One<TopWindow>&>(root).Get(); }

	//! Get the workspace window at id, null when id is out of range
	TopWindow* GetWorkspace(int id) const
	{
		if(id < 0 || id >= workspaces.GetCount()) return nullptr;
		return const_cast<One<TopWindow>&>(workspaces[id]).Get();
	}

	//! Re-read the window list from the X server and hand it to the model.
	//! Returns the number of windows the model now lists.
	int SyncWindows();

	//! Drain the watch connection and report whether the X server has reported a
	//! change since the last `SyncWindows()`. `Paint()` calls this before redrawing
	//! the list, so a repaint with nothing happening costs no window queries at
	//! all. Never blocks.
	bool WindowsChanged();

	//! The window model, for callers and tests that want the list itself
	WindowListModel& GetWindows()             { return windows; }
	const WindowListModel& GetWindows() const { return windows; }

	//! The workspace model
	WorkspaceModel& GetWorkspaceModel()             { return workspace; }
	const WorkspaceModel& GetWorkspaceModel() const { return workspace; }

	//! The window source, so a caller that wants a title or a workspace index for
	//! a handle the model holds can ask the thing that read it
	X11WindowSource& GetWindowSource()             { return source; }
	const X11WindowSource& GetWindowSource() const { return source; }

protected:
	virtual void Paint(Draw& w) override;

private:
	//! Ask the model for the next focus candidate and raise it. This is the only
	//! place a focus decision is made.
	void CycleFocus(int step);

	//! Close the window of workspace from, open the window of workspace to.
	//! Takes the pair the model reported rather than an index, so neither end is
	//! computed twice.
	void ApplySwitch(const WorkspaceSwitch& change);

	//! What the server reports, minus the windows this class owns. The desktop
	//! background, the workspace windows and this window are all mapped top-level
	//! windows, so without this the taskbar lists the desktop itself. The filter
	//! is here rather than in the source because only the view knows which windows
	//! are its own.
	Vector<WindowEntry> SnapshotWindows();

	//! True when a native window belongs to this DesktopManager instance
	bool IsOwnWindow(::Window native) const;

	One<TopWindow>           root;
	Vector<One<TopWindow>>   workspaces;

	WorkspaceModel           workspace;
	WindowListModel          windows;

	//! The production window source. A member rather than a local so its interned
	//! handles survive between syncs: the model drops a handle the source stops
	//! reporting, so a source that re-allocated on every query would reorder the
	//! list and break focus cycling on every read.
	X11WindowSource          source;

#ifdef flagWMANAGER
	//! The window manager. Constructed once and takes over the root window when
	//! WMANAGER is defined. The manager's ProcessRequest() handles MapRequest,
	//! ConfigureRequest, etc. from the dedicated connection.
	X11WindowManager        mgr;

	//! The X11 frame manager. Handles frame creation and management for client windows.
	X11FrameManager         frm;
#endif
};

END_UPP_NAMESPACE

#endif // _DesktopManager_DesktopManager_h_