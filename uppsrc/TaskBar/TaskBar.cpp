#ifndef _TaskBar_TaskBar_cpp_
#define _TaskBar_TaskBar_cpp_

#include <CtrlLib/CtrlLib.h>
#include "TaskBar.h"

// No Xlib call in this file, but the package is still X11-only: without flagX11
// CtrlCore links the GTK backend and the taskbar stops being the X11 one. The
// guard used to be invisible, so make it explicit at compile time.
#if !defined flagX11
#error "TaskBar requires the X11 backend: build with a mainconfig that passes X11, for example \"X11\" = \"GUI X11\". Without flagX11 CtrlCore selects the GTK backend instead."
#endif

NAMESPACE_UPP

// Poll interval for the window list. The list only changes when some other
// process or window opens or closes, and CtrlCore has no hook for that, so
// poll. Two timers is the documented idiom (id 1 = sync, id 2 = window
// layouts shift as windows come and go).
// The interval is now only a tick for draining the watch connection, not a
// window-list rebuild. XPending never blocks, so an idle tick is one syscall
// where the old code opened an X connection, walked the tree and re-read five
// properties per window. It stays a timer because CtrlCore selects no root
// event mask and exposes no hook, so there is nothing for an event to call.
enum { TIMER_SYNC = 1, TIMER_LAYOUT, SYNC_INTERVAL_MS = 500 };

Vector<TaskBar*> TaskBar::live;

	TaskBar::TaskBar()
	{
		// THISBACK is unusable here: it expands to &CLASSNAME::Sync and CLASSNAME
		// is a private member of Ctrl, so a Ctrl subclass cannot name it. Bind a
		// lambda instead.
		if(!source.Watch())
			LOG("Watch() failed: the event connection could not open, so the list only moves on Poll's initial dirty flag");
		SetTimeCallback(-SYNC_INTERVAL_MS, [this]{ Sync(); }, TIMER_SYNC);
		SetTimeCallback(-SYNC_INTERVAL_MS, [this]{ Layout(); Refresh(); }, TIMER_LAYOUT);
		live.Add(this);
	}

TaskBar::~TaskBar()
{
	KillTimeCallback(TIMER_SYNC);
	KillTimeCallback(TIMER_LAYOUT);
	// Vector::Remove takes an index, not a value.
	for(int i = live.GetCount() - 1; i >= 0; i--)
		if(live[i] == this)
			live.Remove(i);
}

bool TaskBar::IsOwnWindow(::Window native) const
{
	if(native == None)
		return false;
	// The host window is this Ctrl's top window. A taskbar with a button for
	// itself is the oldest bug in this file, and the host is a different window
	// from the TaskBar Ctrl, so it has to be named explicitly.
	if(const TopWindow* host = GetTopWindow())
		if(host->GetWindow() == native)
			return true;
	// Everything else this process made: the desktop, its workspaces and the
	// DesktopManager window. Membership of this process is asked of the one API
	// that knows it. This is the only remaining use of Ctrl::GetTopCtrls() in this
	// package -- the list of windows comes from the server, and this call only
	// decides what to leave out. The server cannot answer it, because a window
	// carries no "who made me", and asking Ctrl for the list instead is the bug
	// this change exists to fix.
	Vector<Ctrl*> top = Ctrl::GetTopCtrls();
	for(int i = 0; i < top.GetCount(); i++)
		if(TopWindow* tw = dynamic_cast<TopWindow*>(top[i]))
			if(tw->GetWindow() == native)
				return true;
	return false;
}

Vector<WindowEntry> TaskBar::SnapshotWindows()
{
	Vector<WindowEntry> seen = source.ReadWindows();
	Vector<WindowEntry> out;
	for(int i = 0; i < seen.GetCount(); i++) {
		if(IsOwnWindow(source.NativeOf(seen[i].handle)))
			continue;
		out.Add(seen[i]);
	}
	return out;
}

void TaskBar::RestackSelf()
{
	// The panel is one window the view deliberately leaves out of the list, so
	// it has no handle to raise with, and the source's raise takes a handle.
	// Raising the clicked client puts it on top of the whole stack, which puts
	// it over this panel; nothing but this panel will put the panel back.
	if(const TopWindow* host = GetTopWindow())
		source.RaiseNative(host->GetWindow());
}

void TaskBar::Sync()
{
	// Drain the watch connection first. When the server has reported no change,
	// the list cannot have moved, so there is nothing to re-read and this returns
	// without touching the window list at all.
	if(!source.Poll())
		return;
	// The model owns the add, the drop, the title snapshot and the active flag.
	// What is left for the view is geometry: one rect per listed entry. The
	// workspace filter is the model's too (DM5-001); the view only names the
	// workspace it answers for, re-read from the source on every sync, never
	// cached between them. SetWorkspace() is a no-op when the value has not
	// changed, so an ordinary sync pays nothing for this line.
	Vector<WindowEntry> snapshot = SnapshotWindows();
	windows.SetWorkspace(source.CurrentDesktop());
	windows.Sync(snapshot);
	Layout();
	Refresh();
	RestackSelf();
	// One line per real sync, which on an idle desktop is rare: the tick returns
	// before this point unless the server reported a change. The snapshot count
	// against the model count is what tells a read-side problem (both low) from a
	// model problem (snapshot high, model low).
	String titles;
	for(int i = 0; i < snapshot.GetCount(); i++)
		titles << (i ? "; " : "") << snapshot[i].title;
	LOG("Sync: snapshot " << snapshot.GetCount() << ", model " << windows.GetCount()
	    << ", rects " << rects.GetCount() << ", ctrl " << GetSize().cx << "x" << GetSize().cy
	    << ": " << titles);
}

void TaskBar::Layout()
{
	int cx = GetSize().cx;
	int cy = GetSize().cy;
	int count = windows.GetCount();
	if(rects.GetCount() != count)
		rects.SetCount(count);
	if(count == 0 || cx <= 4)
		return;
	int item_w = min(160, max(64, (cx - 4) / count));
	int x = 2;
	for(int i = 0; i < count; i++) {
		rects[i] = Rect(x, 0, min(x + item_w, cx - 2), cy);
		x += item_w + 2;
	}
}

void TaskBar::Paint(Draw& w)
{
	// Deliberately no Sync() here: Paint runs on every expose, and rebuilding the
	// window list each time means opening an X connection, walking the window tree
	// and re-reading every title per pixel row. Sync() is driven by TIMER_SYNC and
	// by LeftDown instead, and DM4-004 is what replaces that timer. Nothing here
	// writes state; the titles come from the model and the rects from Layout().
	Size sz = GetSize();
	w.DrawRect(sz, SColorFace());

	for(int i = 0; i < windows.GetCount() && i < rects.GetCount(); i++) {
		const Rect& rect = rects[i];
		Color bg = windows.IsActiveAt(i) ? SColorHighlight() : SColorFace();
		Color fg = windows.IsActiveAt(i) ? SColorHighlightText() : SColorText();
		w.DrawRect(rect, bg);
		DrawFrame(w, rect, SColorShadow());
		DrawTextEllipsis(w, rect.left + 4, rect.top, rect.GetWidth() - 8,
		                 windows.TitleAt(i), "..", StdFont(), fg);
	}
}

void TaskBar::LeftDown(Point p, dword keyflags)
{
	for(int i = 0; i < windows.GetCount() && i < rects.GetCount(); i++) {
		if(!rects[i].Contains(p))
			continue;
		// Hit-test the rect, then ask the model for the handle at that index.
		// Every listed window is one this process did not create, so there is no
		// TopWindow* to activate: the source owns the raise and the focus request.
		int handle = windows.HandleAt(i);
		if(handle != WindowListModel::NoHandle) {
			source.Raise(handle);
			source.Focus(handle);
			// The raise put the client over this panel; the restack is what
			// keeps a docked strip visible after every click.
			RestackSelf();
			Sync();
		}
		return;
	}
}

void TaskBar::CycleWindows()
{
	// The model owns the cycling arithmetic, including what to do when nothing
	// holds the focus. The view only applies the answer.
	int next_idx = windows.NextIndex(1);
	if(next_idx < 0)
		return;
	int handle = windows.HandleAt(next_idx);
	if(handle != WindowListModel::NoHandle) {
		source.Raise(handle);
		source.Focus(handle);
	}
	Sync();
}

void TaskBar::CycleAllTopWindows()
{
	// Prefer a live taskbar's tracked list, which already excludes this process's
	// own windows. Fall back to asking the server directly when no taskbar is
	// running, so a global shortcut still works.
	//
	// The live registry and the live[0] limitation are about which taskbar
	// answers, not about what the window list means, so both stay here.
	//
	// The source is not interchangeable with the model's handles. Handles are
	// interned per X11WindowSource, so raising a handle through a different
	// instance resolves to nothing and the call silently does nothing. Whichever
	// list is used, it has to be raised through the source that filled it -- which
	// is why the fallback keeps its own source and its own model together rather
	// than borrowing either.
	X11WindowSource local_source;
	WindowListModel local_model;
	WindowListModel* model = nullptr;
	X11WindowSource* raise_with = nullptr;

	if(!live.IsEmpty()) {
		TaskBar* tb = live[0];
		model = &tb->windows;
		raise_with = &tb->source;
	}
	else {
		local_model.Sync(local_source.ReadWindows());
		model = &local_model;
		raise_with = &local_source;
	}

	// The model already answers "the next one", including from no focus at all,
	// so the active-index search and the modulo this method used to do are gone.
	int handle = model->NextFocus(1);
	if(handle == WindowListModel::NoHandle)
		return;
	raise_with->Raise(handle);
	raise_with->Focus(handle);
}

END_UPP_NAMESPACE

#endif
