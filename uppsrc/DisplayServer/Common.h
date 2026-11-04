#ifndef _DisplayServer_Common_h_
#define _DisplayServer_Common_h_

#include <Ctrl/Display/Display.h>
#include "NetServer.h"

NAMESPACE_UPP

// Set once at startup by ParseVerboseFlag(), read by the logging helpers below.
extern bool g_verbose;
void ParseVerboseFlag();

// Set once at startup by ParseDemoWindowsFlag(); controls whether the Alpha/Beta/Gamma
// synthetic windows are created (NetworkDisplay/0009).
extern bool g_demo_windows;
void ParseDemoWindowsFlag();

// One line per window paint / composite pass, used by both backends
// (NetworkDisplay/0007 will consume this log to confirm drawing happened without
// visually inspecting the screen).
void LogPaint(const char *backend, int window_id, const String& title, Rect rect);
void LogComposite(const char *backend, int window_count);

// Network transport lifecycle/activity logging (NetworkDisplay/0006). 0007 depends
// on being able to observe from --verbose log output that a real client's drawing
// actually happened, so these are unconditionally useful (not backend-specific).
void LogNetConnect(int client_index, const String& peer);
void LogNetDisconnect(int client_index);
void LogNetDraw(int client_index, int batch_count, int64 total_count);
void LogNetTitle(int client_index, const String& title);  // NetworkDisplay/0015
void LogNetResize(int client_index, Size sz);              // NetworkDisplay/0015

struct WindowSpec : Moveable<WindowSpec> {
	String title;
	Color  color;
	int    shape; // 0 = plain, 1 = ellipse accent, 2 = cross accent

	WindowSpec() : shape(0) {}
	WindowSpec(const char *title, Color color, int shape) : title(title), color(color), shape(shape) {}
};

Vector<WindowSpec> DefaultWindowSpecs();

// Shared content painter (solid color + shape + title), driven through the plain
// Draw-shaped API so it works unmodified against both plain Draw (software backend)
// and GLDraw (OpenGL backend, which implements the same Draw-derived interface).
void PaintSyntheticContent(Draw& w, Size sz, const WindowSpec& spec, bool active);

#ifdef flagGUI
// Software backend only: a Ctrl hosted inside a FrameT<CtxUpp2D>'s client area.
class SyntheticContent : public Ctrl {
public:
	typedef SyntheticContent CLASSNAME;

	int            id = -1;
	WindowSpec     spec;
	WindowManager* scope = NULL;

	void Paint(Draw& w) override;
	void LeftDown(Point p, dword keyflags) override;
};

// The client (content) rect in the frame's own local coordinate space: (0,0)-based,
// so it is valid both to position a child Ctrl inside the frame (software backend)
// and to draw directly once a Draw/GLDraw has been offset+clipped to the frame's
// box (OpenGL backend's manual compositor).
Rect FrameClientLocal(WindowManager::Frame& h);

// Software backend only: renders a connected network client's decoded canvas
// (NetClientSession::snapshot) as this Ctrl's entire content, and forwards basic
// mouse/keyboard input back to that client over the network (NetworkDisplay/0006).
// Plays exactly the same role SyntheticContent plays for the built-in demo windows.
class NetworkContent : public Ctrl {
public:
	typedef NetworkContent CLASSNAME;

	int            id = -1;           // ScopeT<CtxUpp2D> handle id, same role as SyntheticContent::id
	int            client_index = -1; // NetServer client index this window is bound to
	String         title;             // for LogPaint() only, same role as SyntheticContent::spec.title
	WindowManager* scope = NULL;
	NetServer*     net = NULL;

	void Paint(Draw& w) override;
	void LeftDown(Point p, dword keyflags) override;
	void LeftUp(Point p, dword keyflags) override;
	void MouseMove(Point p, dword keyflags) override;
	void RightDown(Point p, dword keyflags) override;
	bool Key(dword key, int count) override;

	// NetworkDisplay/0015: this Ctrl is anchored (HSizePos/VSizePos) inside its
	// FrameT's client area, so whenever that frame's real rect changes (maximize,
	// restore, manual drag-resize), Ctrl's normal child-relayout machinery
	// (Ctrl::SyncLayout(), CtrlPos.cpp) already calls this Ctrl's own Layout() with
	// its newly-resolved size -- reused here instead of adding new polling, per the
	// plan doc. NetServer::SendResize() itself no-ops if the size didn't actually
	// change, so it's safe to call unconditionally.
	void Layout() override;
};
#endif

END_UPP_NAMESPACE

#endif
