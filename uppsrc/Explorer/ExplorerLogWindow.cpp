#include "ExplorerLogWindow.h"

NAMESPACE_UPP

#if defined(flagDEBUG) && defined(flagGUI)

static ExplorerLogWindow *log_window;

static const char *LevelName(int level)
{
	switch(level) {
	case APPLOG_LEVEL_TRACE:   return "TRACE";
	case APPLOG_LEVEL_DEBUG:   return "DEBUG";
	case APPLOG_LEVEL_INFO:    return "INFO";
	case APPLOG_LEVEL_WARNING: return "WARN";
	case APPLOG_LEVEL_ERROR:   return "ERROR";
	}
	return "?";
}

ExplorerLogWindow::ExplorerLogWindow()
{
	SetTitle(t_("Explorer log"));
	Sizeable();
	// A TopWindow has no size of its own, and this one lays its children out from
	// their own constraints, so the initial rect has to be stated here.
	SetRect(120, 120, 900, 480);

	list.AddColumn("time", 9);
	list.AddColumn("level", 6);
	list.AddColumn("channel", 9);
	list.AddColumn("message", 0);
	list.MultiSelect(false);

	auto_scroll.SetLabel("Follow tail");
	auto_scroll.Set(1);

	int fcy = GetStdFont().GetHeight() + 8;

	// The option sits on the right, so the field gives up that much width.
	Add(filter.HSizePos(4, 176).TopPos(4, fcy));
	Add(auto_scroll.RightPos(8, 160).TopPos(4, fcy));
	Add(list.VSizePos(fcy + 8, fcy + 8));
	Add(status.BottomPos(0, fcy).HSizePos());

	filter <<= callback(this, &ExplorerLogWindow::ApplyFilter);

	status.Set("no records");
	Rebuild();
}

void ExplorerLogWindow::GotFocus()
{
	// Records keep arriving while the window sits behind others; re-read AppLog on
	// focus rather than subscribing to WhenRecord, which would need a callback whose
	// lifetime spans window close.
	Rebuild();
}

void ExplorerLogWindow::ApplyFilter()
{
	Rebuild();
}

void ExplorerLogWindow::Rebuild()
{
	const Vector<AppLogRecord>& records = ExplorerLog::Get().Records().GetRecords();
	String want = TrimBoth(filter.GetText().ToString());
	bool filtering = !want.IsEmpty();

	shown.Clear();
	for(const AppLogRecord& r : records) {
		// A single field matches a level name, a channel name, or message text.
		if(!filtering || want.Find(LevelName(r.level)) >= 0 || want.Find(r.channel) >= 0 ||
		   want.Find(r.message) >= 0)
			shown.Add(r);
	}

	list.Clear();
	for(const AppLogRecord& r : shown)
		list.Add(FormatTime(r.time, "YYYY-MM-DD HH:MM:SS"), LevelName(r.level), r.channel, r.message);

	status.Set(Sprintf("%d of %d records", (int)shown.GetCount(), (int)records.GetCount()));
	Follow();
}

void ExplorerLogWindow::Follow()
{
	if(!auto_scroll) return;
	int n = list.GetCount();
	if(n > 0) {
		list.SetCursor(n - 1);
		list.SetCursorEditFocus();
	}
}

void ExplorerLogWindow::Open(Ctrl *owner)
{
	if(!log_window) {
		log_window = new ExplorerLogWindow;
		// Centre before the first show: after that the window remembers where it
		// was and reopening it should not move it.
		if(owner) log_window->CenterScreen();
		log_window->Open();
	} else
		log_window->Rebuild();
	log_window->Activate();
}

void ExplorerLogWindow::Shutdown()
{
	if(log_window) {
		delete log_window;
		log_window = nullptr;
	}
}

#endif // flagDEBUG && flagGUI

END_UPP_NAMESPACE