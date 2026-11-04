#ifndef _DisplayServer_NetServer_h_
#define _DisplayServer_NetServer_h_

#include <CtrlCore/CtrlCore.h>
#include "Net/Protocol.h"
#include "Net/Transport.h"

NAMESPACE_UPP

#ifdef flagGUI

// One connected client: its transport, incremental frame parser, and the persistent
// off-screen canvas its draw commands accumulate into. Modeled as a real canvas
// (draws are cumulative until the client issues DOP_CLEAR), not "redraw everything
// every frame" -- mirrors how an actual desktop compositor treats a client's window
// surface.
struct NetClientSession {
	int                    index = -1; // stable "client N" id, used in logs and by the backend
	One<IDisplayTransport> transport;
	FrameParser            parser;
	bool                   hello_received = false;
	bool                   disconnect_notified = false;
	String                 title;
	Size                   size = Size(640, 480);
	// NetworkDisplay/0017: DisplayServer-assigned window_id (see SendWelcome()) of this
	// connection's owner window, decoded from CMSG_HELLO -- -1 if this window has no
	// owner (e.g. the app's main window). The backend (SoftwareMain.cpp/GLMain.cpp)
	// uses this to center a popup/dialog's initial frame over its owner's current one.
	int                    owner_window_id = -1;
	One<ImageDraw>         canvas;
	Size                   canvas_alloc_size = Size(0, 0);
	Image                  snapshot; // last-rendered content; safe to read from a Ctrl::Paint()
	int64                  draw_cmd_total = 0;

	void EnsureCanvas();
	void ApplyDrawBatch(const Vector<DrawCmd>& cmds);
};

// Owns the TCP listener and every connected client's session. Meant to be Poll()ed
// periodically from the host GUI's own event loop (see SoftwareMain.cpp/GLMain.cpp
// wiring it up via a periodic Ctrl SetTimeCallback) -- everything here runs on the
// single GUI thread, so no locking is needed, and an idle client costs one
// non-blocking recv() per Poll().
class NetServer {
	NetworkListener                 listener;
	ArrayMap<int, NetClientSession> clients; // keyed by stable client index
	int                             next_index = 1;

	void AcceptNew();
	void PumpClient(NetClientSession& c);

public:
	bool Start(int port);
	void Poll();

	NetClientSession *Find(int client_index);
	Image             GetSnapshot(int client_index);

	void SendWelcome(int client_index, int window_id);
	void SendMouse(int client_index, byte kind, Point p, dword keyflags);
	void SendKey(int client_index, byte kind, dword keycode);

	// NetworkDisplay/0015: tells the client its usable client-area size changed
	// (maximize/restore/manual frame resize on the DisplayServer side) by sending
	// SMSG_WINDOW_RESIZED. No-op (and doesn't re-send) if `sz` already matches this
	// session's last-known size, so callers can call this unconditionally from a
	// Layout()/paint-time hook without spamming identical resizes.
	void SendResize(int client_index, Size sz);

	// Sends SMSG_CLOSE (best-effort) and tears down bookkeeping for this client.
	// Idempotent -- safe to call even if the client already disconnected on its own.
	void CloseAndRemove(int client_index);

	Event<int> WhenConnect;      // HELLO received, session ready for a window (arg: client_index)
	Event<int> WhenDrawBatch;    // canvas updated (arg: client_index) -- backend should Refresh()
	Event<int> WhenDisconnect;   // transport closed remotely, window not yet torn down (arg: client_index)
	Event<int> WhenTitleChanged; // CMSG_TITLE received (arg: client_index) -- backend should update
	                             // that window's live frame title (NetworkDisplay/0015)
};

#endif

END_UPP_NAMESPACE

#endif
