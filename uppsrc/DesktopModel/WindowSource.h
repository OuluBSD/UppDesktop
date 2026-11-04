#ifndef _DesktopModel_WindowSource_h_
#define _DesktopModel_WindowSource_h_

//! A workspace index that means "on every workspace"
/*!
  Sticky windows, and windows whose source reports no workspace, carry this
  value. It lives here rather than inside WindowSource because WindowEntry's
  default depends on it and WindowEntry is declared first, and the spellings
  must not be allowed to drift apart: WindowSource::AllDesktops is the same
  constant, kept as an alias so the call sites read by the interface's name.
*/
static const int WindowAllDesktops = -1;

//! One window as a source reports it
/*!
  The handle is an opaque integer the source allocated, the title a snapshot of
  what the window shows, the focus flag what the server reports as active. The
  workspace snapshot says which workspace the window sits on, or AllDesktops for
  a sticky window, and is what WindowListModel's workspace filter reads.

  This type lives beside the interface rather than inside WindowListModel,
  because a source hands these over and the model only consumes them.
*/
struct WindowEntry : Moveable<WindowEntry>
{
	int    handle;
	String title;
	bool   active;
	//! The workspace snapshot, sticky as the default: a window whose source says
	//! nothing about a workspace is on all of them, so an entry built without a
	//! desktop passes every filter rather than failing all of them by accident.
	//! The probe check that left this uninitialised failed instead, which is
	//! what the default exists to prevent.
	int    desktop = WindowAllDesktops;
};

//! The window list as a source sees it, plus the two operations a view needs on
//! a window it did not create itself
/*!
  `WindowListModel` takes a `Vector<WindowEntry>` and needs nothing else, so this
  interface sits beside it rather than inside it: adding the seam does not change
  the model. A headless caller supplies a fake source and drives the model with
  no display and no GUI flag, exactly as `ScreenSource` works for
  `ScreenInfoModel`.

  The interface is deliberately read-mostly. Raise and focus are here because a
  window in this list may belong to another process, in which case it is not a
  `TopWindow*` and the view has nothing to call: `TopWindow::Activate()` is a
  method on an object it does not have. Keeping the two X calls on the source is
  what lets a view stay free of Xlib while still driving windows it did not make.

  `DesktopOf()` remains the live, per-handle query for a window's workspace. The
  workspace snapshot on `WindowEntry` was added by DM5-001's contract: the
  per-workspace filter is model-side and runs in `Sync()`, which needs the
  workspace of every entry at once. The original reason for keeping the field
  off the entry -- not changing the model's type for a new source -- was a
  DM4-era rule that the DM5 contract supersedes for this one field, with the
  decision recorded in the contract's "Per-workspace window sets" section. A
  sticky window is on every desktop and reports `AllDesktops`.
*/
class WindowSource
{
public:
	//! Reported by `DesktopOf()` for a window the source cannot place, and the
	//! index a workspace-less window would have. The same constant as
	//! `WindowAllDesktops` above, which WindowEntry's default reads.
	static const int AllDesktops = WindowAllDesktops;

	virtual ~WindowSource() {}

	//! Every window the source can see, in the order it stacks them, each with
	//! the handle, title and focus flag to hand to `WindowListModel::Sync()`.
	//! Empty when no display can be reached, which is how a dead server is
	//! reported rather than as a stale list.
	virtual Vector<WindowEntry> ReadWindows() = 0;

	//! Workspace index of a handle this source reported, or `AllDesktops` when
	//! the handle is unknown to it or the server names no workspace.
	virtual int DesktopOf(int handle) const = 0;

	//! Bring a window to the top of the stack, false when the handle is unknown
	virtual bool Raise(int handle) = 0;

	//! Ask the window manager to focus a window, false when the handle is
	//! unknown or there is no window manager to ask
	virtual bool Focus(int handle) = 0;
};

#endif // _DesktopModel_WindowSource_h_