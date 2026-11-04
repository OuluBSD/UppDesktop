// DisplayServer, OpenGL-rendered backend.
//
// Investigation result (see plan doc / report for the full writeup): GLDraw *is*
// Draw-API-compatible (GLDraw : SDraw : Draw, GLDraw/GLDraw.h + GLDraw/SDraw.h), so
// it is perfectly usable wherever a plain `Draw&` is expected -- including calling
// FrameT<CtxUpp2D>'s existing, unmodified `Paint(Draw&)` directly. What does *not*
// work is wiring that up as automatic per-Ctrl dispatch: FrameT<Dim>::Paint(DrawT&)
// is declared `override` against Ctrl's fixed `virtual void Paint(Draw&)`, which
// requires DrawT == Draw exactly (parameter types aren't covariant in C++, so a
// hypothetical `CtxUppGL2D` with DrawT = GLDraw could not compile that override).
// GLCtrl itself already claims that exact override slot for its own purposes
// (uppsrc/GLCtrl/GLCtrl.h: `void Paint(Draw& w) override;` under PLATFORM_POSIX) and
// exposes a *different* hook, `GLPaint()`, as the actual user paint entry point
// (see reference/OpenGL, reference/GLDrawDemo) -- so a Ctrl-attached FrameT<Dim>
// would never even get its Paint() called through GLCtrl's pipeline.
//
// Given that, this backend keeps ScopeT<CtxUpp2D>/FrameT<CtxUpp2D> for exactly what
// the task doc suggested falling back to: move/focus/maximize/minimize/close STATE
// (position rects, maximized/shown flags, active-id bookkeeping) -- the ScopeT/
// FrameT instances here are real, just never attached to a visible Ctrl tree (their
// `desktop` StaticRect is never Added to this GLCtrl or its TopWindow, so normal
// Ctrl paint/input dispatch never touches them). Compositing and input hit-testing
// are done manually in GLPaint()/LeftDown()/MouseMove(), reusing FrameT::Paint(Draw&)
// for the chrome (passing a GLDraw as the Draw&, which is genuinely GPU-drawn) plus
// PaintSyntheticContent() (shared with the software backend) for window content.
#ifdef flagGL

#include "Common.h"
#include <GLCtrl/GLCtrl.h>
#include <GLDraw/GLDraw.h>

using namespace Upp;

static WindowManager::Frame *FindWindowFrame(WindowManager& scope, int id)
{
	for(int i = 0; i < scope.GetCount(); i++)
		if(scope.GetPosId(i) == id)
			return &scope[i];
	return NULL;
}

class GLDesktop : public GLCtrl {
public:
	typedef GLDesktop CLASSNAME;

	TopWindow             *host = NULL;
	WindowManager          scope; // ScopeT<CtxUpp2D>: state model only, see file header
	Vector<int>            zorder; // back = topmost (GL variant tracks its own z-order,
	                                // since it doesn't rely on Ctrl sibling order for paint)
	VectorMap<int, WindowSpec> specs;

	// NetworkDisplay/0006: real client connections, each with its own entry in
	// `scope`/`zorder` exactly like the synthetic windows above -- `net_index` (window
	// id -> client index) is how GLPaint()/input handling tell a network-backed
	// window apart from a synthetic one (mutually exclusive with `specs`).
	NetServer              net;
	VectorMap<int, int>    net_index;

	bool  dragging = false;
	int   drag_id = -1;
	Point drag_start;
	Rect  drag_start_rect;

	static constexpr int TASKBAR_H = 28;
	static constexpr int BTN = 20;

	GLDesktop()
	{
		// CloseHandle runs a tick after QueueCloseHandle (deferred via PostCallback,
		// see Ctrl/Display/ScopeT.h), so trigger a repaint once it actually happens --
		// otherwise the closed window would linger on screen until some unrelated
		// event (e.g. a later click) happened to trigger the next GLPaint(). Also the
		// one reliable place to notice a network window actually closing (see
		// PruneClosed(): WhenHandleClose fires with no id, so we diff our own
		// bookkeeping against what's still in `scope`).
		scope.WhenHandleClose << [this] { Refresh(); };

		net.WhenConnect = [this](int client_index) {
			NetClientSession *c = net.Find(client_index);
			if(c)
				AddNetworkWindow(client_index, c->title, c->size, c->owner_window_id);
			Refresh();
		};
		net.WhenDrawBatch = [this](int) { Refresh(); };
		net.WhenDisconnect = [this](int client_index) {
			for(int i = 0; i < net_index.GetCount(); i++)
				if(net_index[i] == client_index) {
					scope.QueueCloseHandle(net_index.GetKey(i));
					break;
				}
		};
		// NetworkDisplay/0015: same live title-update wiring as the software
		// backend (SoftwareMain.cpp) -- see its WhenTitleChanged for the rationale.
		net.WhenTitleChanged = [this](int client_index) {
			for(int i = 0; i < net_index.GetCount(); i++)
				if(net_index[i] == client_index) {
					int id = net_index.GetKey(i);
					WindowManager::Frame *f = FindWindowFrame(scope, id);
					NetClientSession *c = net.Find(client_index);
					if(f && c)
						f->SetTitle(c->title);
					break;
				}
			Refresh();
		};
	}

	void Layout() override
	{
		Size sz = GetSize();
		scope.SetFrameBox(RectC(0, 0, sz.cx, sz.cy));
	}

	void AddWindow(const WindowSpec& spec)
	{
		ASSERT(host);
		scope.AddInterface(*host);
		int pos = scope.GetCount() - 1;
		WindowManager::Frame& h = scope[pos];
		int id = scope.GetPosId(pos);
		h.SetTitle(spec.title);
		// NetworkDisplay/0018: opt this Frame into FrameT's sizeable state (kept for
		// consistency with the software backend and any future caller that consults
		// f->IsSizeable() -- see the file header and this backend's own LeftDown()/
		// MouseMove() below for why it has no user-visible effect *yet* here: this
		// Frame is never attached to a live Ctrl tree in this backend, so FrameT's own
		// GetDragMode()/StartDrag()/MouseMove()/CursorImage() are never actually
		// invoked -- GLDesktop hit-tests/drags chrome itself, manually, and today only
		// implements titlebar move + maximize/minimize/close, not edge/corner resize).
		h.Sizeable(true);
		specs.Add(id, spec);
		zorder.Add(id);
	}

	// Same window-creation path as AddWindow() above (same scope.AddInterface() call,
	// same Frame machinery), just tagged in net_index instead of specs so GLPaint()
	// and input handling know to source this window's content from NetServer instead
	// of PaintSyntheticContent().
	void AddNetworkWindow(int client_index, const String& title, Size csize, int owner_window_id = -1)
	{
		ASSERT(host);
		scope.AddInterface(*host);
		int pos = scope.GetCount() - 1;
		WindowManager::Frame& h = scope[pos];
		int id = scope.GetPosId(pos);
		h.SetTitle(title.GetCount() ? title : Format("Client %d", client_index));
		// NetworkDisplay/0018: same Sizeable(true) opt-in as AddWindow() above -- see
		// there for why it doesn't yet do anything user-visible in this backend.
		h.Sizeable(true);

		// NetworkDisplay/0017: same owner-centered repositioning as the software
		// backend's AddNetworkWindow() -- see SoftwareMain.cpp for the full rationale.
		// Only the position (not size) of AddInterface()'s just-set default frame box
		// is overridden here, and only when owner_window_id names a still-open window.
		if(owner_window_id >= 0) {
			WindowManager::Frame *owner_frame = FindWindowFrame(scope, owner_window_id);
			if(owner_frame) {
				Rect box = h.GetFrameBox();
				Size want = csize.cx > 0 && csize.cy > 0 ? csize : box.GetSize();
				Rect centered = owner_frame->GetFrameBox().CenterRect(want);
				h.SetFrameBox(RectC(centered.left, centered.top, box.GetWidth(), box.GetHeight()));
			}
		}

		// NetworkDisplay/0015: same real-declared-size sizing as the software
		// backend's AddNetworkWindow() -- see SoftwareMain.cpp for the rationale.
		if(csize.cx > 0 && csize.cy > 0) {
			Rect box = h.GetFrameBox();
			h.SetClient(RectC(box.left, box.top, csize.cx, csize.cy));
		}

		net_index.Add(id, client_index);
		zorder.Add(id);
		net.SendWelcome(client_index, id);
	}

	// Drops zorder/specs/net_index entries whose Frame no longer exists (closed
	// windows are removed from `scope` a tick after being queued, via ScopeT's
	// PostCallback) -- and, for a network window, this is also the one place that
	// notices the close actually happened, so it's where we tear down the matching
	// NetServer session.
	void PruneClosed()
	{
		for(int i = zorder.GetCount() - 1; i >= 0; i--) {
			int id = zorder[i];
			if(!FindWindowFrame(scope, id)) {
				specs.RemoveKey(id);
				int np = net_index.Find(id);
				if(np >= 0) {
					net.CloseAndRemove(net_index[np]);
					net_index.Remove(np);
				}
				zorder.Remove(i);
			}
		}
	}

	void RaiseToTop(int id)
	{
		scope.FocusHandle(id);
		int i = FindIndex(zorder, id);
		if(i >= 0) {
			zorder.Remove(i);
			zorder.Add(id);
		}
	}

	Rect ButtonRect(WindowManager::Frame& f, int slot_from_right) const
	{
		Size sz = f.GetSize();
		Rect m = f.Margins();
		int x = sz.cx - m.right - slot_from_right * BTN;
		return Rect(x - BTN, m.top, x, m.top + BTN);
	}

	// Hit-test rect equals ButtonRect (full slot, no gap); the drawn rect is inset by
	// 1px so adjacent same-colored buttons remain visually distinct.
	static Rect Inset1(Rect r) { return Rect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1); }

	void GLPaint() override
	{
		PruneClosed();

		Size sz = GetSize();
		GLDraw w;
		w.Init(sz);

		w.DrawRect(sz, Color(40, 44, 52));

		LogComposite("opengl", zorder.GetCount());

		for(int id : zorder) {
			WindowManager::Frame *f = FindWindowFrame(scope, id);
			if(!f || !f->IsShown())
				continue;

			Rect r = f->GetFrameBox();
			if(!w.Clipoff(r))
				continue;

			f->Paint(w); // reuse the existing FrameT<CtxUpp2D> chrome painter as-is

			// FrameT's close/maximize/minimize Buttons are never part of a live Ctrl
			// tree in this backend (see file header), so draw simple stand-ins for
			// them at the same slots LeftDown() hit-tests against.
			Rect close_r = ButtonRect(*f, 1);
			Rect max_r = ButtonRect(*f, 2);
			Rect min_r = ButtonRect(*f, 3);
			w.DrawRect(Inset1(close_r), Color(176, 60, 60));
			w.DrawText(close_r.left + 6, close_r.top + 3, "x", StdFont(), White());
			w.DrawRect(Inset1(max_r), Color(90, 90, 100));
			w.DrawText(max_r.left + 5, max_r.top + 3, f->IsMaximized() ? "o" : "+", StdFont(),
			           White());
			w.DrawRect(Inset1(min_r), Color(90, 90, 100));
			w.DrawText(min_r.left + 5, min_r.top + 8, "_", StdFont(), White());

			Rect client = FrameClientLocal(*f);
			if(w.Clipoff(client)) {
				int np = net_index.Find(id);
				if(np >= 0) {
					w.DrawRect(client.GetSize(), Black());
					Image img = net.GetSnapshot(net_index[np]);
					if(!img.IsEmpty())
						w.DrawImage(0, 0, img);
				}
				else {
					bool active = scope.IsActiveHandle(id);
					const WindowSpec& spec = specs.Get(id);
					PaintSyntheticContent(w, client.GetSize(), spec, active);
				}
				LogPaint("opengl", id, f->GetTitle(), client);
				w.End();
			}

			w.End();
		}

		// taskbar (also serves to un-minimize windows, mirroring the software backend).
		// NetworkDisplay/0019: every open window gets an entry here, synthetic or
		// network -- `zorder` already tracks both uniformly (see AddWindow()/
		// AddNetworkWindow() above and PruneClosed() for removal on close), so no
		// separate bookkeeping is needed. A network window's title is read straight
		// off its own Frame (kept live via WhenTitleChanged, see the constructor)
		// instead of a synthetic WindowSpec, which network windows don't have. Every
		// connected client (main app window or popup/dialog alike, per
		// NetworkDisplay/0014) is its own entry here, matching the software backend's
		// choice for the same reason: nothing distinguishes a transient dialog from a
		// real top-level window in this backend either.
		Rect bar(0, sz.cy - TASKBAR_H, sz.cx, sz.cy);
		w.DrawRect(bar, Color(30, 32, 38));
		int x = 4;
		for(int id : zorder) {
			WindowManager::Frame *f = FindWindowFrame(scope, id);
			if(!f)
				continue;
			bool active = scope.IsActiveHandle(id);
			w.DrawRect(RectC(x, bar.top + 2, 120, TASKBAR_H - 4),
			           active ? Color(90, 110, 140) : Color(60, 62, 70));
			w.DrawText(x + 6, bar.top + 6, f->GetTitle(), StdFont(), White());
			x += 124;
		}

		w.End();
	}

	void LeftDown(Point p, dword keyflags) override
	{
		Size sz = GetSize();
		if(p.y >= sz.cy - TASKBAR_H) {
			// NetworkDisplay/0019: same zorder-based entry list as the taskbar's own
			// drawing loop in GLPaint() above -- must stay in lockstep with it (same
			// ids in the same order) since hit-testing here relies on identical x
			// positions.
			int x = 4;
			for(int id : zorder) {
				if(!FindWindowFrame(scope, id))
					continue;
				Rect br(x, sz.cy - TASKBAR_H + 2, x + 120, sz.cy - 2);
				if(br.Contains(p)) {
					RaiseToTop(id);
					Refresh();
					return;
				}
				x += 124;
			}
			return;
		}

		for(int idx = zorder.GetCount() - 1; idx >= 0; idx--) {
			int id = zorder[idx];
			WindowManager::Frame *f = FindWindowFrame(scope, id);
			if(!f || !f->IsShown())
				continue;
			Rect r = f->GetFrameBox();
			if(!r.Contains(p))
				continue;

			RaiseToTop(id);

			Point local = p - r.TopLeft();
			Rect m = f->Margins();
			int titlebar_bottom = m.top + GetStdFontCy() + 4;

			if(!f->IsMaximized() && ButtonRect(*f, 1).Contains(local))
				scope.QueueCloseHandle(id);
			else if(ButtonRect(*f, 2).Contains(local)) {
				if(f->IsMaximized())
					scope.RestoreHandle(id);
				else
					scope.MaximizeHandle(id);
				// NetworkDisplay/0015: Maximize()/Overlap() (called via
				// MaximizeHandle()/RestoreHandle() above) already resized `f`
				// in-place -- tell the client its real client-area size just
				// changed so it stops rendering at its stale original size
				// (previously the black-area bug: DisplayServer just filled the
				// newly-enlarged frame with black, nothing else painted there).
				int np = net_index.Find(id);
				if(np >= 0) {
					Rect client = FrameClientLocal(*f);
					net.SendResize(net_index[np], client.GetSize());
				}
			}
			else if(!f->IsMaximized() && ButtonRect(*f, 3).Contains(local))
				scope.MinimizeHandle(id);
			else if(!f->IsMaximized() && local.y < titlebar_bottom) {
				dragging = true;
				drag_id = id;
				drag_start = p;
				drag_start_rect = r;
				SetCapture();
			}
			else {
				// Not on any chrome button/titlebar -- if this is a network window and
				// the click landed in its content area, forward it as real mouse input
				// (NetworkDisplay/0006's basic input round-trip).
				int np = net_index.Find(id);
				if(np >= 0) {
					Rect client = FrameClientLocal(*f);
					if(client.Contains(local))
						net.SendMouse(net_index[np], MOUSE_LEFT_DOWN, local - client.TopLeft(),
						              keyflags);
				}
			}
			Refresh();
			return;
		}
	}

	void LeftUp(Point p, dword keyflags) override
	{
		if(dragging) {
			dragging = false;
			ReleaseCapture();
			return;
		}
		for(int idx = zorder.GetCount() - 1; idx >= 0; idx--) {
			int id = zorder[idx];
			WindowManager::Frame *f = FindWindowFrame(scope, id);
			if(!f || !f->IsShown())
				continue;
			Rect r = f->GetFrameBox();
			if(!r.Contains(p))
				continue;
			int np = net_index.Find(id);
			if(np >= 0) {
				Rect client = FrameClientLocal(*f);
				Point local = p - r.TopLeft();
				if(client.Contains(local))
					net.SendMouse(net_index[np], MOUSE_LEFT_UP, local - client.TopLeft(), keyflags);
			}
			return;
		}
	}

	bool Key(dword key, int count) override
	{
		int id = scope.GetActiveHandleId();
		int np = net_index.Find(id);
		if(np >= 0) {
			net.SendKey(net_index[np], KEY_DOWN, key);
			return true;
		}
		return false;
	}

	void MouseMove(Point p, dword keyflags) override
	{
		if(dragging && HasCapture()) {
			WindowManager::Frame *f = FindWindowFrame(scope, drag_id);
			if(f) {
				Rect r = drag_start_rect;
				r.Offset(p - drag_start);
				f->SetFrameBox(r);
				Refresh();
			}
			return;
		}
		// Not dragging chrome -- forward hover/drag-inside-content moves to whichever
		// network window is topmost under the cursor.
		for(int idx = zorder.GetCount() - 1; idx >= 0; idx--) {
			int id = zorder[idx];
			WindowManager::Frame *f = FindWindowFrame(scope, id);
			if(!f || !f->IsShown())
				continue;
			Rect r = f->GetFrameBox();
			if(!r.Contains(p))
				continue;
			int np = net_index.Find(id);
			if(np >= 0) {
				Rect client = FrameClientLocal(*f);
				Point local = p - r.TopLeft();
				if(client.Contains(local))
					net.SendMouse(net_index[np], MOUSE_MOVE, local - client.TopLeft(), keyflags);
			}
			return;
		}
	}
};

GUI_APP_MAIN
{
	ParseVerboseFlag();
	ParseDemoWindowsFlag();
	if(g_verbose)
		fprintf(stderr, "[opengl] DisplayServer starting, verbose logging enabled\n");

	Ctrl::GlobalBackPaint();

	TopWindow win;
	win.Title("DisplayServer -- OpenGL backend");
	win.Sizeable().Zoomable();
	win.SetRect(0, 0, 1000, 700);

	GLDesktop gl;
	gl.host = &win;
	gl.scope.SetFrameBox(RectC(0, 0, 1000, 700));
	win.Add(gl.SizePos());

	for(const WindowSpec& spec : DefaultWindowSpecs())
		gl.AddWindow(spec);

	int port = GetDisplayServerPort();
	gl.net.Start(port);

	win.Open();

	// Same single-threaded, non-blocking poll approach as the software backend (see
	// SoftwareMain.cpp) -- pumps NetServer from inside GLCtrl's own event loop.
	SetTimeCallback(-20, [&gl] { gl.net.Poll(); });

	win.Run();
}

#endif
