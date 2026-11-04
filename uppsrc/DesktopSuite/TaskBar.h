#ifndef _DesktopSuite_TaskBar_h_
#define _DesktopSuite_TaskBar_h_

#include <CtrlLib/CtrlLib.h>

NAMESPACE_UPP

class TaskBar : public Ctrl {
public:
	TaskBar();
	virtual ~TaskBar();

	virtual void Paint(Draw& w) override;
	virtual void LeftDown(Point p, dword keyflags) override;
	virtual void Layout() override;

	void Sync();
	static void CycleWindows();

	int  GetItemCount() const { return items.GetCount(); }

private:
	struct WinItem : Moveable<WinItem> {
		Ptr<TopWindow> win;
		Rect           rect;
		String         title;
		bool           active;
	};
	Vector<WinItem> items;
};

END_UPP_NAMESPACE

#endif
