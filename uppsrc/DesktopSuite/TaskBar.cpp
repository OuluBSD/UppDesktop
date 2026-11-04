#include "DesktopSuite.h"

NAMESPACE_UPP

TaskBar::TaskBar()
{
}

TaskBar::~TaskBar()
{
}

void TaskBar::Sync()
{
	Vector<Ctrl *> top = Ctrl::GetTopCtrls();
	Index<TopWindow *> open_wins;
	for(int i = 0; i < top.GetCount(); i++) {
		TopWindow *tw = dynamic_cast<TopWindow *>(top[i]);
		if(tw && tw->IsOpen() && (Ctrl*)tw != this)
			open_wins.FindAdd(tw);
	}
	for(int i = items.GetCount() - 1; i >= 0; i--)
		if(open_wins.Find(items[i].win) < 0)
			items.Remove(i);
	for(int i = 0; i < open_wins.GetCount(); i++) {
		TopWindow *tw = open_wins[i];
		bool found = false;
		for(int j = 0; j < items.GetCount(); j++) {
			if(items[j].win == tw) {
				found = true;
				break;
			}
		}
		if(!found) {
			WinItem& item = items.Add();
			item.win = tw;
		}
	}
	for(int i = 0; i < items.GetCount(); i++) {
		TopWindow *tw = items[i].win;
		items[i].title = tw->GetTitle().ToString();
		if(items[i].title.IsEmpty())
			items[i].title = "Window";
		items[i].active = tw->IsForeground();
	}
	Layout();
	Refresh();
}

void TaskBar::Layout()
{
	if(items.IsEmpty())
		return;
	int cx = GetSize().cx;
	int cy = GetSize().cy;
	int item_w = min(160, max(64, (cx - 4) / max(1, items.GetCount())));
	int x = 2;
	for(int i = 0; i < items.GetCount(); i++) {
		items[i].rect = Rect(x, 0, min(x + item_w, cx - 2), cy);
		x += item_w + 2;
	}
}

void TaskBar::Paint(Draw& w)
{
	Sync();
	Size sz = GetSize();
	w.DrawRect(sz, SColorFace());

	for(int i = 0; i < items.GetCount(); i++) {
		const WinItem& item = items[i];
		Color bg = item.active ? SColorHighlight() : SColorFace();
		Color fg = item.active ? SColorHighlightText() : SColorText();
		w.DrawRect(item.rect, bg);
		DrawFrame(w, item.rect, SColorShadow());
		DrawTextEllipsis(w, item.rect.left + 4, item.rect.top, item.rect.GetWidth() - 8,
		                 item.title, "..", StdFont(), fg);
	}
}

void TaskBar::LeftDown(Point p, dword keyflags)
{
	for(int i = 0; i < items.GetCount(); i++) {
		if(items[i].rect.Contains(p) && items[i].win) {
			items[i].win->SetForeground();
			Sync();
			return;
		}
	}
}

void TaskBar::CycleWindows()
{
	TaskBar *tb = nullptr;
	Ctrl *d = Ctrl::GetDesktop();
	if(d) {
		for(Ctrl *c = d->GetFirstChild(); c; c = c->GetNext()) {
			tb = dynamic_cast<TaskBar *>(c);
			if(tb) break;
		}
	}
	if(tb && tb->items.GetCount() > 1) {
		int active_idx = -1;
		for(int i = 0; i < tb->items.GetCount(); i++) {
			if(tb->items[i].win && tb->items[i].win->IsForeground()) {
				active_idx = i;
				break;
			}
		}
		int next_idx = (active_idx + 1) % tb->items.GetCount();
		if(tb->items[next_idx].win)
			tb->items[next_idx].win->SetForeground();
		return;
	}

	Vector<TopWindow *> wins;
	Vector<Ctrl *> top = Ctrl::GetTopCtrls();
	for(int i = 0; i < top.GetCount(); i++) {
		TopWindow *tw = dynamic_cast<TopWindow *>(top[i]);
		if(tw && tw->IsOpen() && (Ctrl*)tw != d)
			wins.Add(tw);
	}
	if(wins.GetCount() <= 1)
		return;

	int active_idx = -1;
	for(int i = 0; i < wins.GetCount(); i++) {
		if(wins[i]->IsForeground()) {
			active_idx = i;
			break;
		}
	}
	int next_idx = (active_idx + 1) % wins.GetCount();
	wins[next_idx]->SetForeground();
}

END_UPP_NAMESPACE
