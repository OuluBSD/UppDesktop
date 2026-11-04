// Desktop.cpp
//
// Backend selection. Each platform backend in Core/Desktop/<Platform>
// registers itself at static-init time by calling RegisterDesktopBackend(),
// and the first registered backend wins. The selection is therefore a plain
// global pointer: no caller has to test PLATFORM_* or any other macro to find
// out which backend is in force, and a headless test can substitute its own
// backend by calling RegisterDesktopBackend() directly.
//
// The registration order is the translation-unit init order, which is not
// guaranteed across translation units. That is deliberate for the stubs: the
// platform backends are independent packages, so whichever one is linked
// registers itself; if more than one is linked the first to register wins and
// the others are recorded in Diagnostics() so the conflict is visible rather
// than silent.

#include <Core/Desktop/Desktop.h>

namespace Upp {

namespace {

IDesktopBackend* g_desktop_backend = nullptr;
int              g_desktop_backend_count = 0;

// The icon theme a GUI integration layer prefers, when it knows one. Empty
// means "no preference"; backends fall back to their own defaults.
String g_preferred_icon_theme;

} // anonymous

void SetPreferredIconTheme(const String& theme_name)
{
	g_preferred_icon_theme = theme_name;
}

String GetPreferredIconTheme()
{
	return g_preferred_icon_theme;
}

void RegisterDesktopBackend(IDesktopBackend* backend)
{
	if(!backend)
		return;
	if(!g_desktop_backend) {
		g_desktop_backend = backend;
	}
	g_desktop_backend_count++;
}

IDesktopBackend* GetDesktopBackend()
{
	return g_desktop_backend;
}

int GetDesktopBackendCount()
{
	return g_desktop_backend_count;
}

String DescribeDesktopBackend()
{
	if(!g_desktop_backend)
		return "no desktop backend registered";
	String s = String("desktop backend: ") + g_desktop_backend->Name();
	s << " (" << g_desktop_backend_count << " registration" << (g_desktop_backend_count == 1 ? "" : "s") << ")";
	return s;
}

} // Upp