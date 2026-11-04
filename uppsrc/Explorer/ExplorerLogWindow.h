#ifndef _Explorer_ExplorerLogWindow_h
#define _Explorer_ExplorerLogWindow_h

#include "ExplorerLog.h"

#if defined(flagDEBUG) && defined(flagGUI)

// Explorer.h is the package umbrella: it brings in CtrlLib, the model and the
// policy. It must come before this file opens NAMESPACE_UPP -- Explorer.h opens
// its own namespace, and nesting it inside ours produces Upp::Upp.
#include "Explorer.h"

NAMESPACE_UPP

// In-app log viewer, debug builds only.
//
// Renders the same AppLog records the headless twin prints, so what is on screen
// here is exactly what `--log` shows on the command line. The filter matches a
// level name, a channel name, or a substring of the message.
class ExplorerLogWindow : public TopWindow {
	ArrayCtrl      list;
	EditString     filter;
	Option         auto_scroll;
	StatusBar      status;

	Vector<AppLogRecord> shown;

	void Rebuild();
	void ApplyFilter();
	void Follow();

public:
	void GotFocus() override;

public:
	typedef ExplorerLogWindow CLASSNAME;

	//< Opens (or raises) the log window. Safe to call repeatedly.
	static void Open(Ctrl *owner = nullptr);
	//< Tears down the log window and its event hook. Named Shutdown rather than
	//< Close because TopWindow::Close() is virtual and a static would hide it.
	static void Shutdown();

	ExplorerLogWindow();
};

END_UPP_NAMESPACE

#endif // flagDEBUG && flagGUI

#endif