#ifndef _DesktopModel_WindowListModel_cpp_
#define _DesktopModel_WindowListModel_cpp_

#include "DesktopModel.h"

NAMESPACE_UPP

const int WindowListModel::NoHandle;

int WindowListModel::FindIndex(const Vector<WindowEntry>& list, int handle)
{
	for(int i = 0; i < list.GetCount(); i++)
		if(list[i].handle == handle)
			return i;
	return -1;
}

void WindowListModel::CheckInvariants() const
{
	// Handles identify entries, so a duplicate would make Sync() drop and re-add
	// the same window on every poll and blur two rows of the list into one.
	for(int i = 0; i < entries.GetCount(); i++) {
		ASSERT(entries[i].handle != NoHandle);
		for(int j = i + 1; j < entries.GetCount(); j++)
			ASSERT(entries[i].handle != entries[j].handle);
	}
	// Only one window can hold the focus flag.
	int actives = 0;
	for(int i = 0; i < entries.GetCount(); i++)
		if(entries[i].active)
			actives++;
	ASSERT(actives <= 1);
}

void WindowListModel::SetWorkspace(int desktop)
{
	if(desktop == workspace)
		return;
	workspace = desktop;
	// The filtered list should read in the server's stacking order after a
	// switch: a sticky window belongs where the server put it relative to the
	// new workspace's windows, not where an earlier workspace happened to leave
	// it in this list. Rebuilding on the next Sync does that; between switches,
	// the stable-order rule from the 0002 split still holds, so a plain poll
	// never churns positions.
	resort_pending = true;
}

void WindowListModel::Sync(const Vector<WindowEntry>& seen)
{
	// A duplicate handle in what the source reports would collapse two rows into
	// one without a word, so it is refused loudly instead. The check runs on the
	// full report, before the workspace filter narrows it.
	for(int i = 0; i < seen.GetCount(); i++)
		ASSERT(FindIndex(seen, seen[i].handle) == i);

	// The workspace filter, DM5-001: the model answers for the workspace it was
	// set to, AllDesktops as the workspace passes everything, and a sticky
	// window passes every filter. Entries on other workspaces are not state the
	// model keeps hidden -- they are simply not this list's subject, and the
	// source keeps their handles alive so a switch back re-lists them.
	Vector<WindowEntry> visible;
	for(int i = 0; i < seen.GetCount(); i++) {
		if(workspace != WindowSource::AllDesktops
		   && seen[i].desktop != workspace
		   && seen[i].desktop != WindowSource::AllDesktops)
			continue;
		visible.Add(seen[i]);
	}

	if(resort_pending) {
		entries.Clear();
		for(int i = 0; i < visible.GetCount(); i++) {
			WindowEntry& entry = entries.Add();
			entry.handle = visible[i].handle;
			entry.title = visible[i].title;
			entry.active = visible[i].active;
			entry.desktop = visible[i].desktop;
		}
		resort_pending = false;
		CheckInvariants();
		return;
	}

	// Drop entries whose window is gone, walking down so Remove() stays valid.
	for(int i = entries.GetCount() - 1; i >= 0; i--)
		if(FindIndex(visible, entries[i].handle) < 0)
			entries.Remove(i);

	// Append handles we have not seen yet, at the end, so an existing window
	// keeps its place in the order.
	for(int i = 0; i < visible.GetCount(); i++) {
		if(FindIndex(entries, visible[i].handle) >= 0)
			continue;
		WindowEntry& entry = entries.Add();
		entry.handle = visible[i].handle;
		entry.title = visible[i].title;
		entry.active = visible[i].active;
		entry.desktop = visible[i].desktop;
	}

	// Refresh what the source now reports about the windows that stayed. The
	// workspace is refreshed too: a window that changed workspaces and left this
	// one is gone from `visible` and was removed above.
	for(int i = 0; i < entries.GetCount(); i++) {
		int j = FindIndex(visible, entries[i].handle);
		entries[i].title = visible[j].title;
		entries[i].active = visible[j].active;
		entries[i].desktop = visible[j].desktop;
	}

	CheckInvariants();
}

int WindowListModel::ActiveIndex() const
{
	for(int i = 0; i < entries.GetCount(); i++)
		if(entries[i].active)
			return i;
	return -1;
}

int WindowListModel::ActiveHandle() const
{
	return HandleAt(ActiveIndex());
}

int WindowListModel::NextIndex(int step) const
{
	int n = entries.GetCount();
	if(n == 0)
		return -1;

	// With nothing active there is no index to step from, and that case is where
	// the old code was undefined: forwards landed on the first window, but
	// backwards computed (-1 - 1 + n) % n and landed on the second-to-last for
	// n > 1. Define both ends instead -- forwards is the first entry, backwards
	// is the last -- and the arithmetic below is a plain wrap in either
	// direction.
	int base = ActiveIndex();
	if(base < 0)
		return step < 0 ? n - 1 : 0;

	int next = (base + step) % n;
	if(next < 0)
		next += n;
	return next;
}

int WindowListModel::NextFocus(int step) const
{
	return HandleAt(NextIndex(step));
}

String WindowListModel::ToString() const
{
	String s;
	for(int i = 0; i < entries.GetCount(); i++) {
		if(i)
			s << ' ';
		s << Format("%d:%s%s", entries[i].handle, entries[i].title,
		            entries[i].active ? "*" : "");
	}
	return s;
}

END_UPP_NAMESPACE

#endif // _DesktopModel_WindowListModel_cpp_
