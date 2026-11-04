// DisplayServer, software-rendered backend: composites N synthetic framed windows
// via plain Ctrl/Draw/Painter, using Ctrl/Display's ScopeT<CtxUpp2D>/FrameT<CtxUpp2D>
// as-is (no changes to the Dim traits struct needed for this backend).
#ifndef flagGL

#include "Common.h"

using namespace Upp;

class DesktopWindow : public TopWindow {
public:
	typedef DesktopWindow CLASSNAME;

	WindowManager           scope; // ScopeT<CtxUpp2D>
	Array<SyntheticContent> contents;
	Array<Button>           taskbar;
	static constexpr int    TASKBAR_H = 28;

	// NetworkDisplay/0006: real client connections, each with its own framed window
	// created through the exact same scope.AddInterface() path as the synthetic
	// windows above (see AddNetworkWindow()). NetworkDisplay/0019: they now get a
	// taskbar button too, same as the fixed startup demo windows -- they are
	// otherwise fully real, framed, movable/focusable/closable windows.
	NetServer            net;
	Array<NetworkContent> net_contents;

	// NetworkDisplay/0019: parallel to `taskbar` (same index), the ScopeT<CtxUpp2D>
	// handle id each taskbar button focuses/restores when clicked. Needed because a
	// network window's taskbar entry must be removed again on disconnect/close,
	// unlike synthetic windows' entries which live for the process lifetime -- this
	// is how PruneClosedNetworkWindows() finds *which* button to remove.
	Vector<int> taskbar_id;

	DesktopWindow()
	{
		Title("DisplayServer -- software backend");
		Sizeable().Zoomable();
		SetRect(0, 0, 1000, 700);
		Add(scope.GetDesktop());

		net.WhenConnect = [this](int client_index) {
			NetClientSession *c = net.Find(client_index);
			if(c)
				AddNetworkWindow(client_index, c->title, c->size, c->owner_window_id);
		};
		net.WhenDrawBatch = [this](int client_index) {
			for(NetworkContent& c : net_contents)
				if(c.client_index == client_index) {
					c.Refresh();
					break;
				}
		};
		net.WhenDisconnect = [this](int client_index) {
			for(NetworkContent& c : net_contents)
				if(c.client_index == client_index) {
					scope.QueueCloseHandle(c.id);
					break;
				}
		};
		// NetworkDisplay/0015: the client's hosted TopWindow's title changed after
		// connect -- update this window's live DisplayServer frame title to match
		// (previously only ever set once, from CMSG_HELLO, at connect time).
		net.WhenTitleChanged = [this](int client_index) {
			NetClientSession *c = net.Find(client_index);
			if(!c)
				return;
			for(NetworkContent& nc : net_contents)
				if(nc.client_index == client_index) {
					WindowManager::Frame *f = FindFrame(nc.id);
					if(f) {
						f->SetTitle(c->title);
						f->Refresh();
					}
					nc.title = c->title;
					// NetworkDisplay/0019: keep the taskbar label in sync too, same
					// live-title-update rationale as the frame itself above.
					int tp = FindTaskbarPos(nc.id);
					if(tp >= 0)
						taskbar[tp].SetLabel(c->title);
					break;
				}
		};
		// Closing is always deferred (QueueCloseHandle -> next tick), whether it was
		// triggered by the frame's own close button (user click) or by us above (a
		// client disconnecting) -- either way, once the handle is actually gone from
		// `scope`, tear down the matching net_contents entry + NetServer session.
		scope.WhenHandleClose << [this] { PruneClosedNetworkWindows(); };
	}

	// Finds the live ScopeT<CtxUpp2D> Frame for a given handle id, or NULL if it has
	// since been closed. Needed here (not just by GLMain.cpp's own copy) to reach a
	// network window's Frame again after connect time, e.g. to update its title live
	// (NetworkDisplay/0015).
	WindowManager::Frame *FindFrame(int id)
	{
		for(int p = 0; p < scope.GetCount(); p++)
			if(scope.GetPosId(p) == id)
				return &scope[p];
		return NULL;
	}

	// NetworkDisplay/0019: index into `taskbar`/`taskbar_id` for a given handle id, or
	// -1 if that handle has no taskbar entry (shouldn't happen for a live network
	// window, but callers check anyway).
	int FindTaskbarPos(int id)
	{
		for(int i = 0; i < taskbar_id.GetCount(); i++)
			if(taskbar_id[i] == id)
				return i;
		return -1;
	}

	void AddNetworkWindow(int client_index, const String& title, Size csize, int owner_window_id = -1)
	{
		scope.AddInterface(*this);
		int pos = scope.GetCount() - 1;
		WindowManager::Frame& h = scope[pos];
		int id = scope.GetPosId(pos);
		h.SetTitle(title.GetCount() ? title : Format("Client %d", client_index));
		// NetworkDisplay/0018: let the user drag-resize this window's border/corner
		// (FrameT's own GetDragMode/StartDrag/CursorImage machinery, gated behind this
		// one flag -- see Ctrl/Display/FrameT.h/.cpp). This Frame is a real Ctrl child
		// of `scope`'s desktop in this backend, so MouseMove()/CursorImage() reach it
		// through Ctrl's normal dispatch and a resize-drag's SetFrameBox() call flows
		// through the same generic path Ctrl::SyncLayout() already uses for any other
		// frame-box change (see NetworkContent::Layout(), Common.cpp), which is what
		// feeds NetworkDisplay/0015's SMSG_WINDOW_RESIZED -- no separate wiring needed.
		h.Sizeable(true);

		// NetworkDisplay/0017: if this window declared a still-open owner window (its
		// CMSG_HELLO's owner_window_id, forwarded here from NetClientSession), reposition
		// -- not resize -- AddInterface()'s just-set generic staggered default frame box
		// so it's centered over the owner's *current* frame instead. This is what makes a
		// popup/dialog (e.g. PromptOK's "Help > About..") appear centered over its owner
		// like a normal desktop app, instead of stuck at AddInterface()'s generic
		// top-left-ish offset (the 0014 regression this fixes). Sizing (below, 0015) is
		// untouched -- only the position this block computes is later reused by it (via
		// h.GetFrameBox()'s left/top). Windows with no owner (owner_window_id < 0, e.g.
		// the app's main window) fall through unchanged, keeping the existing generic
		// default position.
		if(owner_window_id >= 0) {
			WindowManager::Frame *owner_frame = FindFrame(owner_window_id);
			if(owner_frame) {
				Rect box = h.GetFrameBox();
				Size want = csize.cx > 0 && csize.cy > 0 ? csize : box.GetSize();
				Rect centered = owner_frame->GetFrameBox().CenterRect(want);
				h.SetFrameBox(RectC(centered.left, centered.top, box.GetWidth(), box.GetHeight()));
			}
		}

		// NetworkDisplay/0015: size the frame to the network client's own real
		// declared canvas size (from CMSG_HELLO), instead of leaving
		// ScopeT<Dim>::AddInterface()'s generic default handle dimensions in place
		// -- scoped to network windows only, via SetClient() (already-existing
		// FrameT API: computes the frame box that yields this exact client rect,
		// same margins/titlebar math GetClient() uses in reverse). Synthetic
		// AddWindow() windows below are untouched, so --demo-windows keeps its
		// existing default sizing.
		if(csize.cx > 0 && csize.cy > 0) {
			Rect box = h.GetFrameBox();
			h.SetClient(RectC(box.left, box.top, csize.cx, csize.cy));
		}

		NetworkContent& content = net_contents.Add();
		content.id = id;
		content.client_index = client_index;
		content.title = h.GetTitle();
		content.scope = &scope;
		content.net = &net;
		Rect m = h.Margins();
		int top_h = m.top + GetStdFontCy() + 4;
		h.Add(content.HSizePos(m.left, m.right).VSizePos(top_h, m.bottom));

		// NetworkDisplay/0019: same Button-per-handle taskbar entry synthetic windows
		// get via AddWindow() below, so a minimized network window (main app or
		// dialog) can be restored the same way. Every connected client is its own
		// framed TopWindow-equivalent here (NetworkDisplay/0014 routes each TopWindow,
		// including popups/dialogs like PromptOK, to its own AddNetworkWindow() call)
		// and there is no "is this a transient dialog" flag anywhere in NetClientSession
		// to special-case on, so the least-surprising choice -- and the one that matches
		// synthetic windows, which also all get a button -- is to give every network
		// window a taskbar entry, popups included.
		Button& b = taskbar.Add();
		b.SetLabel(h.GetTitle());
		b.WhenPush = [this, id] { scope.FocusHandle(id); };
		Add(b);
		taskbar_id.Add(id);

		net.SendWelcome(client_index, id);
		Layout();
	}

	// Mirrors the OpenGL backend's PruneClosed() idiom: scope.WhenHandleClose fires
	// with no id (see ScopeT<Dim>::CloseHandle), so the only reliable way to learn
	// *which* handle just disappeared is to compare our own bookkeeping against
	// what's still actually in `scope`.
	void PruneClosedNetworkWindows()
	{
		bool taskbar_changed = false;
		for(int i = net_contents.GetCount() - 1; i >= 0; i--) {
			int id = net_contents[i].id;
			bool still_present = false;
			for(int p = 0; p < scope.GetCount(); p++)
				if(scope.GetPosId(p) == id) {
					still_present = true;
					break;
				}
			if(!still_present) {
				net.CloseAndRemove(net_contents[i].client_index);
				net_contents.Remove(i);
				// NetworkDisplay/0019: also drop this window's taskbar entry, unlike
				// synthetic windows' entries which are never removed (they live for
				// the process lifetime, matching pre-0019 behavior -- out of scope
				// here since synthetic windows are never closed in practice).
				int tp = FindTaskbarPos(id);
				if(tp >= 0) {
					taskbar.Remove(tp);
					taskbar_id.Remove(tp);
					taskbar_changed = true;
				}
			}
		}
		if(taskbar_changed)
			Layout();
	}

	void Layout() override
	{
		Size sz = GetSize();
		Rect desktop_rect(0, 0, sz.cx, max(0, sz.cy - TASKBAR_H));
		scope.SetFrameBox(desktop_rect); // also sizes scope.GetDesktop()

		int x = 4, y = desktop_rect.bottom + 2;
		for(Button& b : taskbar) {
			b.SetRect(x, y, 120, TASKBAR_H - 4);
			x += 124;
		}
	}

	void AddWindow(const WindowSpec& spec)
	{
		scope.AddInterface(*this);
		int pos = scope.GetCount() - 1;
		WindowManager::Frame& h = scope[pos];
		int id = scope.GetPosId(pos);
		h.SetTitle(spec.title);
		// NetworkDisplay/0018: same drag-resize opt-in as AddNetworkWindow() above --
		// synthetic demo windows get it too (nothing about resize is network-specific).
		h.Sizeable(true);

		SyntheticContent& content = contents.Add();
		content.id = id;
		content.spec = spec;
		content.scope = &scope;
		Rect m = h.Margins();
		int top_h = m.top + GetStdFontCy() + 4;
		h.Add(content.HSizePos(m.left, m.right).VSizePos(top_h, m.bottom));

		Button& b = taskbar.Add();
		b.SetLabel(spec.title);
		b.WhenPush = [this, id] { scope.FocusHandle(id); };
		Add(b);
		// NetworkDisplay/0019: keep taskbar_id index-aligned with taskbar (see
		// FindTaskbarPos()) even though synthetic windows' own entries are never
		// looked up/removed by id today.
		taskbar_id.Add(id);

		Layout();
	}

	void Paint(Draw& w) override
	{
		LogComposite("software", scope.GetCount());
		TopWindow::Paint(w);
		Size sz = GetSize();
		w.DrawRect(RectC(0, max(0, sz.cy - TASKBAR_H), sz.cx, TASKBAR_H), SColorFace());
	}
};

GUI_APP_MAIN
{
	ParseVerboseFlag();
	ParseDemoWindowsFlag();
	if(g_verbose)
		fprintf(stderr, "[software] DisplayServer starting, verbose logging enabled\n");

	Ctrl::GlobalBackPaint();

	DesktopWindow win;
	for(const WindowSpec& spec : DefaultWindowSpecs())
		win.AddWindow(spec);

	int port = GetDisplayServerPort();
	win.net.Start(port);

	win.Open();

	// Poll the (non-blocking) network layer from inside the same GUI event loop that
	// already drives the synthetic windows, instead of a separate thread -- keeps
	// everything single-threaded (no locking) while still never blocking the UI,
	// since NetServer::Poll() never waits on the network.
	SetTimeCallback(-20, [&win] { win.net.Poll(); });

	win.Run();
}

#endif
