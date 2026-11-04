#ifndef _DesktopModel_WindowListModel_h_
#define _DesktopModel_WindowListModel_h_

#include "WindowSource.h"

//! The ordered window list, the title snapshots, and focus cycling over them
/*!
  The order is the one `Sync()` is given, kept stable across syncs: a window that
  is already listed keeps its position, and a window seen for the first time is
  appended. That order used to be whatever `Ctrl::GetTopCtrls()` happened to
  return, which is U++ internal list order rather than Z-order, so nothing about
  it could be inspected. Here it is the model's own list and `HandleAt()` shows
  it. A source that knows the real Z-order replaces this order without touching
  the cycling arithmetic.

  The model does not mark the entry that `NextFocus()` returns as active. It has
  no way to know whether the raise succeeded, and a model that guessed would be
  reporting X state it never asked for. The view raises the window, then the next
  `Sync()` brings the real focus flag back.
*/
class WindowListModel
{
public:
	//! Returned instead of a handle when there is no window to focus
	static const int NoHandle = 0;

	WindowListModel() {}

	//! Drop every entry
	void Clear() { entries.Clear(); }

	//! Rebuild the bookkeeping from what the source currently sees: entries whose
	//! handle is gone are dropped, unseen handles are appended, and the title and
	//! focus flag of every surviving entry are refreshed. The workspace filter is
	//! applied to what the source reported before any of that: entries on other
	//! workspaces are not listed, sticky entries pass every filter.
	void Sync(const Vector<WindowEntry>& seen);

	//! Which workspace the list answers for, or AllDesktops for all of them.
	//! Changing it rebuilds the list in the server's order on the next Sync().
	void SetWorkspace(int desktop);
	int  GetWorkspace() const { return workspace; }

	//! The next focus candidate in cycling order, or NoHandle when the list is
	//! empty. step is +1 for forwards and -1 for backwards.
	int NextFocus(int step = 1) const;

	//! NextFocus() as an index, or -1 when the list is empty
	int NextIndex(int step = 1) const;

	//! Index of the entry the source reported as active, or -1 when none is
	int ActiveIndex() const;

	//! Handle at the active index, or NoHandle
	int ActiveHandle() const;

	int    GetCount() const         { return entries.GetCount(); }
	bool   IsEmpty() const       { return entries.IsEmpty(); }
	int    HandleAt(int i) const { return i >= 0 && i < entries.GetCount() ? entries[i].handle : NoHandle; }
	String TitleAt(int i) const  { return i >= 0 && i < entries.GetCount() ? entries[i].title : String(); }
	bool   IsActiveAt(int i) const { return i >= 0 && i < entries.GetCount() && entries[i].active; }

	//! The cycling order, as text. Exposed so a caller can check that the order
	//! is the one it handed in.
	String ToString() const;

private:
	//! Position of a handle in a list, or -1. Linear because the lists are short
	//! and Vector has no Find.
	static int FindIndex(const Vector<WindowEntry>& list, int handle);
	void CheckInvariants() const;

	Vector<WindowEntry> entries;
	int  workspace = WindowSource::AllDesktops;
	bool resort_pending = false;
};

#endif // _DesktopModel_WindowListModel_h_
