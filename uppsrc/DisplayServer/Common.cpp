#include "Common.h"

NAMESPACE_UPP

bool g_verbose = false;

void ParseVerboseFlag()
{
	for(const String& a : CommandLine())
		if(a == "--verbose" || a == "-v")
			g_verbose = true;
}

bool g_demo_windows = false;

void ParseDemoWindowsFlag()
{
	for(const String& a : CommandLine())
		if(a == "--demo-windows")
			g_demo_windows = true;
}

void LogPaint(const char *backend, int window_id, const String& title, Rect rect)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[%s] paint window id=%d title=\"%s\" rect=(%d,%d,%d,%d)\n", backend,
	        window_id, title.Begin(), rect.left, rect.top, rect.Width(), rect.Height());
	fflush(stderr);
}

void LogComposite(const char *backend, int window_count)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[%s] composite pass: %d window(s)\n", backend, window_count);
	fflush(stderr);
}

void LogNetConnect(int client_index, const String& peer)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[net] client %d connected (%s)\n", client_index, peer.Begin());
	fflush(stderr);
}

void LogNetDisconnect(int client_index)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[net] client %d disconnected\n", client_index);
	fflush(stderr);
}

void LogNetDraw(int client_index, int batch_count, int64 total_count)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[net] client %d: received %d draw command(s) (total %lld)\n", client_index,
	        batch_count, (long long)total_count);
	fflush(stderr);
}

void LogNetTitle(int client_index, const String& title)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[net] client %d: title changed to \"%s\"\n", client_index, title.Begin());
	fflush(stderr);
}

void LogNetResize(int client_index, Size sz)
{
	if(!g_verbose)
		return;
	fprintf(stderr, "[net] client %d: sent SMSG_WINDOW_RESIZED %dx%d\n", client_index, sz.cx, sz.cy);
	fflush(stderr);
}

Vector<WindowSpec> DefaultWindowSpecs()
{
	Vector<WindowSpec> v;
	if(!g_demo_windows)
		return v; // return empty vector unless --demo-windows flag is set (NetworkDisplay/0009)
	v.Add(WindowSpec("Alpha", Color(198, 82, 82), 0));
	v.Add(WindowSpec("Beta", Color(82, 140, 198), 1));
	v.Add(WindowSpec("Gamma", Color(96, 176, 96), 2));
	return v;
}

static Color Accent(Color c)
{
	return Color(c.GetR() * 2 / 3, c.GetG() * 2 / 3, c.GetB() * 2 / 3);
}

void PaintSyntheticContent(Draw& w, Size sz, const WindowSpec& spec, bool active)
{
	w.DrawRect(sz, spec.color);
	Color accent = Accent(spec.color);
	switch(spec.shape) {
	case 1:
		w.DrawEllipse(sz.cx / 4, sz.cy / 4, max(0, sz.cx / 2), max(0, sz.cy / 2), accent);
		break;
	case 2:
		w.DrawRect(sz.cx / 2 - 4, 8, 8, max(0, sz.cy - 16), accent);
		w.DrawRect(8, sz.cy / 2 - 4, max(0, sz.cx - 16), 8, accent);
		break;
	default:
		w.DrawRect(8, 8, max(0, sz.cx - 16), max(0, sz.cy - 16), accent);
		break;
	}
	w.DrawText(10, 10, spec.title, StdFont().Bold(), White());
	if(active)
		w.DrawText(10, max(10, sz.cy - 24), "(active)", StdFont(), White());
}

#ifdef flagGUI
void SyntheticContent::Paint(Draw& w)
{
	Size sz = GetSize();
	bool active = scope && scope->IsActiveHandle(id);
	PaintSyntheticContent(w, sz, spec, active);
	LogPaint("software", id, spec.title, RectC(0, 0, sz.cx, sz.cy));
}

void SyntheticContent::LeftDown(Point p, dword keyflags)
{
	if(scope)
		scope->FocusHandle(id);
}

Rect FrameClientLocal(WindowManager::Frame& h)
{
	Size sz = h.GetSize();
	Rect m = h.Margins();
	Rect r(0, 0, sz.cx, sz.cy);
	r.left += m.left;
	r.right -= m.right;
	r.top += m.top;
	r.bottom -= m.bottom;
	r.top += GetStdFontCy() + 4;
	if(r.right < r.left)
		r.right = r.left;
	if(r.bottom < r.top)
		r.bottom = r.top;
	return r;
}

void NetworkContent::Paint(Draw& w)
{
	Size sz = GetSize();
	w.DrawRect(sz, Black());
	if(net) {
		Image img = net->GetSnapshot(client_index);
		if(!img.IsEmpty())
			w.DrawImage(0, 0, img);
	}
	LogPaint("software", id, title, RectC(0, 0, sz.cx, sz.cy));
}

void NetworkContent::LeftDown(Point p, dword keyflags)
{
	if(scope)
		scope->FocusHandle(id);
	SetFocus();
	if(net)
		net->SendMouse(client_index, MOUSE_LEFT_DOWN, p, keyflags);
}

void NetworkContent::LeftUp(Point p, dword keyflags)
{
	if(net)
		net->SendMouse(client_index, MOUSE_LEFT_UP, p, keyflags);
}

void NetworkContent::MouseMove(Point p, dword keyflags)
{
	if(net)
		net->SendMouse(client_index, MOUSE_MOVE, p, keyflags);
}

void NetworkContent::RightDown(Point p, dword keyflags)
{
	if(net)
		net->SendMouse(client_index, MOUSE_RIGHT_DOWN, p, keyflags);
}

bool NetworkContent::Key(dword key, int count)
{
	if(net)
		net->SendKey(client_index, KEY_DOWN, key);
	return true; // handled: don't let the frame/desktop try to interpret it too
}

void NetworkContent::Layout()
{
	// Guard against firing before this Ctrl is actually parented into its FrameT's
	// client area: HSizePos()/VSizePos() (called fluently in AddNetworkWindow(),
	// still on an as-yet-unparented Ctrl) each independently resolve/sync a rect of
	// their own via GetPrimaryWorkArea() (Ctrl::UpdateRect0(), CtrlPos.cpp) --
	// spuriously firing Layout() with the *screen's* size, not this window's real
	// client area, before h.Add() ever runs. Only real, post-parenting resizes
	// matter here.
	if(net && GetParent())
		net->SendResize(client_index, GetSize());
}
#endif

END_UPP_NAMESPACE
