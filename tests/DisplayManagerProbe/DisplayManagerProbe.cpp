#include <Core/Core.h>
#include <DesktopModel/DesktopModel.h>

// Headless twin of the DisplayManager view, task DM2-011 and DM2-012.
//
//	bin/build -m CLANG -M HEADLESS DisplayManagerProbe && ./bin/CLANG/DisplayManagerProbe
//
// It drives the same model calls that uppsrc/DisplayManager/DisplayManager.cpp
// drives, in the same order, and prints the state after each one. The view adds
// a TopWindow, the X11 source and painting; none of those can exist without a
// display. What is proved here is that the geometry, the work area and the DPI
// all come from ScreenInfoModel, so the numbers the view paints are produced
// with no X11 anywhere in the process.
//
// The source below is a fake, and deliberately so: the real one lives in
// uppsrc/DisplayManager because it opens an X connection. Task DM2-017 verified
// the real source against xdpyinfo on a live server; this twin proves the model
// half, which is the half a caller can reach without a display at all.
//
// Printing is printf, not LOG: the models must stay silent, and LOG does not
// reliably reach stdout in these builds. A printed value that contradicts what
// the model was told sets the exit code.

using namespace Upp;

static int failures = 0;

static void Report(bool ok, const char* what, const String& detail)
{
	if(ok)
		printf("%-5s %s\n", "ok", what);
	else {
		printf("%-5s %s\n     got: %s\n", "FAIL", what, detail.Begin());
		failures++;
	}
}

static void Show(const char* step, const ScreenInfoModel& screens)
{
	printf("     %-34s %s\n", step, (const char *)screens.ToString());
}

// The stand-in for X11ScreenSource: two screens with known pixel and millimetre
// metrics, and a flag for making one of them unreadable.
class FakeScreenSource : public ScreenSource
{
public:
	FakeScreenSource() : unreadable(-1) {}

	void Add(int x, int y, int width, int height, int width_mm, int height_mm)
	{
		ScreenMetrics m;
		m.x = x;
		m.y = y;
		m.width = width;
		m.height = height;
		m.width_mm = width_mm;
		m.height_mm = height_mm;
		metrics.Add(m);
	}

	//! Index that ReadScreen() will refuse, or -1 for none
	void FailAt(int index) { unreadable = index; }

	virtual int GetCount() override { return metrics.GetCount(); }

	virtual bool ReadScreen(int index, ScreenMetrics& out) override
	{
		if(index < 0 || index >= metrics.GetCount() || index == unreadable)
			return false;
		out = metrics[index];
		return true;
	}

private:
	Vector<ScreenMetrics> metrics;
	int unreadable;
};

static void ProbeInitialize()
{
	printf("\n-- DisplayManager Initialize: what the view calls\n");

	ScreenInfoModel screens;
	Show("fresh", screens);
	Report(screens.GetCount() == 0, "a model that was never updated has no screens",
	       screens.ToString());

	FakeScreenSource source;
	// 1280x800 at 325x203 mm is 100 x 100 dpi; the second screen reports 0 mm,
	// which is what a virtual machine usually does.
	source.Add(0, 0, 1280, 800, 325, 203);
	source.Add(0, 0, 1920, 1080, 0, 0);

	bool found = screens.Update(source);
	Show("Initialize -> Update(source)", screens);
	Report(found, "Initialize reports that it found screens", screens.ToString());
	Report(screens.GetCount() == 2, "both screens are listed", screens.ToString());

	Rect geometry = screens.Geometry(0);
	printf("     geometry 0                       %d x %d at %d, %d\n",
	       geometry.GetWidth(), geometry.GetHeight(), geometry.left, geometry.top);
	Report(geometry.GetWidth() == 1280 && geometry.GetHeight() == 800,
	       "the geometry is the one the source reported", screens.ToString());

	int hx, hy;
	screens.DPI(0, hx, hy);
	printf("     dpi 0                            %d x %d\n", hx, hy);
	Report(hx == 100 && hy == 100, "dpi is derived from pixels and millimetres",
	       screens.ToString());

	screens.DPI(1, hx, hy);
	printf("     dpi 1                            %d x %d\n", hx, hy);
	Report(hx == 0 && hy == 0, "a server reporting no physical size gives dpi 0",
	       screens.ToString());

	// Out-of-range reads answer 0 rather than reading past the end.
	Report(screens.DPI(7) == 0 && screens.Geometry(7) == Rect(),
	       "an index the model does not have answers empty", screens.ToString());

	// A source that reaches nothing: Initialize reports false and the list is
	// empty, not stale.
	FakeScreenSource dead;
	Report(!screens.Update(dead), "a source with no screens reports false",
	       screens.ToString());
	Show("Update from a source with no screens", screens);
	Report(screens.GetCount() == 0, "the screen list is emptied rather than kept stale",
	       screens.ToString());
}

static void ProbeStrips()
{
	printf("\n-- DisplayManager strips: what the setters call\n");

	ScreenInfoModel screens;
	FakeScreenSource source;
	source.Add(0, 0, 1280, 800, 325, 203);
	screens.Update(source);

	Report(screens.TaskbarHeight() == ScreenInfoModel::DefaultTaskbarHeight,
	       "the default taskbar strip is 24", screens.ToString());
	Report(screens.PanelHeight() == 0, "the default panel strip is 0", screens.ToString());

	Rect work = screens.WorkArea(0);
	printf("     default work area                %d x %d at %d, %d\n",
	       work.GetWidth(), work.GetHeight(), work.left, work.top);
	Report(work.GetWidth() == 1280 && work.GetHeight() == 800 - 24
	       && work.top == 0,
	       "the work area is the screen minus the taskbar strip", screens.ToString());

	// SetPanelHeight / SetTaskbarHeight. The model recomputes every work area
	// itself, so the setters are not the only callers of the recalculation.
	screens.SetTaskbarHeight(40);
	screens.SetPanelHeight(24);
	work = screens.WorkArea(0);
	printf("     work area, panel 24 taskbar 40   %d x %d at %d, %d\n",
	       work.GetWidth(), work.GetHeight(), work.left, work.top);
	Show("after both strips are set", screens);
	Report(work.GetWidth() == 1280 && work.GetHeight() == 800 - 40 - 24
	       && work.top == 24,
	       "the work area follows both strips", screens.ToString());

	// Strips taller than the screen must not produce a negative height.
	screens.SetTaskbarHeight(5000);
	work = screens.WorkArea(0);
	printf("     work area, taskbar 5000          %d x %d at %d, %d\n",
	       work.GetWidth(), work.GetHeight(), work.left, work.top);
	Report(work.GetHeight() >= 0, "strips taller than the screen clamp instead of wrapping",
	       screens.ToString());

	screens.SetTaskbarHeight(-5);
	Report(screens.TaskbarHeight() == 0, "a negative strip height is clamped to 0",
	       screens.ToString());
}

static void ProbeScreenChange()
{
	printf("\n-- DisplayManager HandleScreenChange: one screen at a time\n");

	ScreenInfoModel screens;
	FakeScreenSource source;
	source.Add(0, 0, 1280, 800, 325, 203);
	source.Add(0, 0, 1920, 1080, 508, 286);
	screens.Update(source);
	Show("after Initialize", screens);

	// A mode change on the first screen only.
	FakeScreenSource resized;
	resized.Add(0, 0, 1024, 768, 260, 195);
	resized.Add(0, 0, 1920, 1080, 508, 286);
	bool refreshed = screens.RefreshScreen(0, resized);
	Show("RefreshScreen(0) after a resize", screens);
	Report(refreshed, "HandleScreenChange(0) reports success", screens.ToString());
	Report(screens.Geometry(0).GetWidth() == 1024, "screen 0 has the new size",
	       screens.ToString());
	Report(screens.Geometry(1).GetWidth() == 1920, "screen 1 was left alone",
	       screens.ToString());
	Report(screens.WorkArea(0).GetHeight() == 768 - 24,
	       "the work area of the refreshed screen was recomputed", screens.ToString());

	// An index the model does not have still returns false.
	Report(!screens.RefreshScreen(7, resized),
	       "an index the model does not have returns false", screens.ToString());
	Report(!screens.RefreshScreen(-1, resized),
	       "a negative index returns false", screens.ToString());

	// A screen the source cannot read leaves the list alone.
	FakeScreenSource broken;
	broken.Add(0, 0, 800, 600, 200, 150);
	broken.FailAt(1);
	Report(!screens.RefreshScreen(1, broken),
	       "a screen the source cannot read returns false", screens.ToString());
	Show("after a failed re-read", screens);
	Report(screens.GetCount() == 2 && screens.Geometry(1).GetWidth() == 1920,
	       "a failed re-read leaves the old value in place", screens.ToString());
}

int main()
{
	ProbeInitialize();
	ProbeStrips();
	ProbeScreenChange();

	printf("\nDisplayManagerProbe: %s\n", failures ? "FAILED" : "all checks passed");
	NoMemoryLeaksCheck = true;
	SetExitCode(failures ? 1 : 0);
	return GetExitCode();
}
