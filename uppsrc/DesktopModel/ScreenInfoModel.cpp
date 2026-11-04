#ifndef _DesktopModel_ScreenInfoModel_cpp_
#define _DesktopModel_ScreenInfoModel_cpp_

#include "DesktopModel.h"

NAMESPACE_UPP

// 25.4 mm in an inch: the factor between the millimetres a server reports and the
// dots per inch that follow from them.
static const double kScreenMillimetresPerInch = 25.4;

void ScreenInfoModel::CheckInvariants() const
{
	for(int i = 0; i < screens.GetCount(); i++) {
		// A work area outside its own screen means the strips ate the whole
		// screen, which is a layout decision a caller has to be able to see.
		ASSERT(screens[i].work_area.GetHeight() >= 0);
		ASSERT(screens[i].work_area.GetWidth() >= 0);
		// 0 means "the server reported no physical size", never a computed zero.
		ASSERT(screens[i].dpi_x >= 0);
		ASSERT(screens[i].dpi_y >= 0);
	}
	ASSERT(taskbar_height >= 0);
	ASSERT(panel_height >= 0);
}

void ScreenInfoModel::StoreScreen(int index, const ScreenMetrics& metrics)
{
	if(index < 0)
		return;

	if(index >= screens.GetCount()) {
		// Fill the gap rather than leaving default records for screens that were
		// never read, so index i always means screen i.
		while(screens.GetCount() <= index)
			screens.Add();
	}

	ScreenRecord& record = screens[index];
	record.geometry = Rect(metrics.x, metrics.y, metrics.x + metrics.width,
	                       metrics.y + metrics.height);
	record.dpi_x = metrics.width_mm > 0
		? (int)(metrics.width * kScreenMillimetresPerInch / metrics.width_mm + 0.5) : 0;
	record.dpi_y = metrics.height_mm > 0
		? (int)(metrics.height * kScreenMillimetresPerInch / metrics.height_mm + 0.5) : 0;
}

void ScreenInfoModel::RecalcWorkAreas()
{
	for(int i = 0; i < screens.GetCount(); i++) {
		const Rect& g = screens[i].geometry;
		// The work area is the screen minus the panel strip on top and the
		// taskbar strip at the bottom, mirroring fluxbox's slit/root area split.
		Rect work(g.left, g.top + panel_height, g.right, g.bottom - taskbar_height);
		// Strips taller than the screen must not produce a negative height.
		if(work.bottom < work.top)
			work.bottom = work.top;
		screens[i].work_area = work;
	}
	CheckInvariants();
}

bool ScreenInfoModel::Update(ScreenSource& source)
{
	screens.Clear();

	int count = source.GetCount();
	for(int i = 0; i < count; i++) {
		ScreenMetrics metrics;
		if(source.ReadScreen(i, metrics))
			StoreScreen(i, metrics);
	}
	RecalcWorkAreas();
	return screens.GetCount() > 0;
}

bool ScreenInfoModel::RefreshScreen(int index, ScreenSource& source)
{
	if(index < 0 || index >= screens.GetCount())
		return false;
	ScreenMetrics metrics;
	if(!source.ReadScreen(index, metrics))
		return false;
	StoreScreen(index, metrics);
	RecalcWorkAreas();
	return true;
}

Rect ScreenInfoModel::Geometry(int index) const
{
	return index >= 0 && index < screens.GetCount() ? screens[index].geometry : Rect();
}

Rect ScreenInfoModel::WorkArea(int index) const
{
	return index >= 0 && index < screens.GetCount() ? screens[index].work_area : Rect();
}

int ScreenInfoModel::DPI(int index) const
{
	return index >= 0 && index < screens.GetCount() ? screens[index].dpi_x : 0;
}

void ScreenInfoModel::DPI(int index, int& horizontal, int& vertical) const
{
	horizontal = vertical = 0;
	if(index < 0 || index >= screens.GetCount())
		return;
	horizontal = screens[index].dpi_x;
	vertical = screens[index].dpi_y;
}

String ScreenInfoModel::ToString() const
{
	String s = Format("screens %d, taskbar %d, panel %d", screens.GetCount(),
	                  taskbar_height, panel_height);
	for(int i = 0; i < screens.GetCount(); i++) {
		const Rect& g = screens[i].geometry;
		const Rect& a = screens[i].work_area;
		s << Format("; %d: %d x %d at %d,%d work %d x %d at %d,%d dpi %d x %d", i,
		            g.GetWidth(), g.GetHeight(), g.left, g.top,
		            a.GetWidth(), a.GetHeight(), a.left, a.top,
		            screens[i].dpi_x, screens[i].dpi_y);
	}
	return s;
}

END_UPP_NAMESPACE

#endif // _DesktopModel_ScreenInfoModel_cpp_
