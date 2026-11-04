// DesktopWin.cpp
//
// Windows desktop backend stub.

#include <Core/Desktop/Win/DesktopWin.h>
#include <Core/Desktop/DesktopBackend.h>

namespace Upp {

class WinDesktopBackend : public IDesktopBackend {
public:
	const char* Name() const override { return "Win32"; }
	
	bool IsSupported() const override
	{
#ifdef PLATFORM_WIN32
		return true;
#else
		return false;
#endif
	}
	
	String Diagnostics() const override
	{
		return "Windows backend: not implemented";
	}
	
	String GetUserDirectory(const String& xdg_var, const String& fallback, DesktopStatus& status) const override
	{
		status = DESKTOP_UNSUPPORTED;
		return String();
	}
	
	DesktopMimeResult DetectMime(const String& path) const override
	{
		DesktopMimeResult r;
		r.status = DESKTOP_UNSUPPORTED;
		r.detail = "Not implemented";
		return r;
	}
	
	DesktopMimeResult GetMimeDescription(const String& mime) const override
	{
		DesktopMimeResult r;
		r.status = DESKTOP_UNSUPPORTED;
		r.detail = "Not implemented";
		return r;
	}
	
	DesktopIconResult LookupIcon(const String& mime_or_name, int size, int scale) const override
	{
		DesktopIconResult r;
		r.status = DESKTOP_UNSUPPORTED;
		r.fallback_reason = "Not implemented";
		return r;
	}
	
	DesktopIconResult LookupAppIcon(const String& name, int size, int scale) const override
	{
		DesktopIconResult r;
		r.status = DESKTOP_UNSUPPORTED;
		r.fallback_reason = "Not implemented";
		return r;
	}
	
	DesktopApp GetDesktopApp(const String& desktop_file) const override
	{
		DesktopApp r;
		r.status = DESKTOP_UNSUPPORTED;
		return r;
	}
	
	Vector<DesktopDefaultApp> GetDefaultApps(const String& mime_or_ext) const override
	{
		return {};
	}
	
	DesktopTrashInfo GetTrashInfo() const override
	{
		DesktopTrashInfo r;
		r.status = DESKTOP_UNSUPPORTED;
		return r;
	}
	
	DesktopStatus MoveToTrash(const String& path, String& new_path) const override
	{
		new_path = String();
		return DESKTOP_UNSUPPORTED;
	}
	
	DesktopStatus RestoreTrash(const String& trash_path, String& original_path) const override
	{
		original_path = String();
		return DESKTOP_UNSUPPORTED;
	}
	
	DesktopStatus EmptyTrash(String& detail) const override
	{
		detail = "Not implemented";
		return DESKTOP_UNSUPPORTED;
	}
};

void RegisterWinDesktopBackend()
{
#ifdef PLATFORM_WIN32
	static WinDesktopBackend singleton;
	RegisterDesktopBackend(&singleton);
#endif
}

} // Upp