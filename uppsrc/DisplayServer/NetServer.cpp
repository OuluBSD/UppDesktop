#include "NetServer.h"
#include "Common.h"

NAMESPACE_UPP

#ifdef flagGUI

void NetClientSession::EnsureCanvas()
{
	Size sz = size;
	if(sz.cx <= 0 || sz.cy <= 0)
		sz = Size(640, 480);
	if(canvas.IsEmpty() || canvas_alloc_size != sz) {
		canvas.Create(sz);
		canvas_alloc_size = sz;
		canvas->DrawRect(sz, Black());
		snapshot = *canvas;
	}
}

void NetClientSession::ApplyDrawBatch(const Vector<DrawCmd>& cmds)
{
	EnsureCanvas();
	ImageDraw& w = *canvas;
	for(const DrawCmd& c : cmds) {
		Color col(c.r, c.g, c.b);
		switch(c.op) {
		case DOP_CLEAR:
			w.DrawRect(canvas_alloc_size, col);
			break;
		case DOP_RECT:
			w.DrawRect(c.x, c.y, c.w, c.h, col);
			break;
		case DOP_LINE:
			w.DrawLine(c.x, c.y, c.x2, c.y2, c.width, col);
			break;
		case DOP_ELLIPSE:
			w.DrawEllipse(c.x, c.y, c.w, c.h, col);
			break;
		case DOP_TEXT:
			w.DrawText(c.x, c.y, c.text, StdFont(), col);
			break;
		case DOP_IMAGE: {
			// Real RGBA blit (NetworkDisplay/0007 protocol extension, widened to
			// carry real alpha in NetworkDisplay/0011 -- see Net/Protocol.h's
			// DOP_IMAGE comment). The wire bytes are already alpha-premultiplied
			// (NetDpy's PutImage sends the source Image's own RGBA verbatim, and
			// U++ Image content is premultiplied internally for IMAGE_ALPHA kind
			// -- see Premultiply()/AlphaBlend() in uppsrc/Draw/Image.h), which is
			// exactly what's needed for a correct "source-over" composite. So
			// this reuses the same already-established premultiplied-alpha
			// compositing knowledge this codebase relies on elsewhere
			// (Ctrl/Compositing, CompositeEasingNetwork/Compositing/0002;
			// SImageDraw1::PutImage's Over()/AlphaBlend() in
			// uppsrc/Draw/ImageOp.cpp+ImageBlit.cpp) rather than re-deriving the
			// blend formula here: build a properly-kinded premultiplied Image and
			// let Draw::DrawImage's normal alpha-compositing path (the same one
			// used for every translucent icon/glyph any other U++ GUI draws)
			// blend it against whatever is already in `canvas`.
			if(c.w > 0 && c.h > 0 && c.image_rgba.GetCount() == c.w * c.h * 4) {
				ImageBuffer ib(c.w, c.h);
				const byte *src = (const byte *)~c.image_rgba;
				RGBA *dst = ~ib;
				bool has_alpha = false;
				for(int i = 0; i < c.w * c.h; i++) {
					dst[i].r = src[4 * i + 0];
					dst[i].g = src[4 * i + 1];
					dst[i].b = src[4 * i + 2];
					dst[i].a = src[4 * i + 3];
					has_alpha |= dst[i].a != 255;
				}
				ib.SetKind(has_alpha ? IMAGE_ALPHA : IMAGE_OPAQUE);
				w.DrawImage(c.x, c.y, Image(ib));
			}
			break;
		}
		default:
			break;
		}
	}
	snapshot = *canvas;
	draw_cmd_total += cmds.GetCount();
}

bool NetServer::Start(int port)
{
	bool ok = listener.Listen(port);
	if(g_verbose)
		fprintf(stderr, "[net] %s on port %d\n", ok ? "listening" : "FAILED to listen", port);
	return ok;
}

void NetServer::AcceptNew()
{
	One<NetworkDisplayTransport> t = listener.Accept();
	if(t.IsEmpty())
		return;
	int idx = next_index++;
	String peer = t->Describe();
	NetClientSession& c = clients.Add(idx);
	c.index = idx;
	c.transport = pick(t);
	LogNetConnect(idx, peer);
}

void NetServer::PumpClient(NetClientSession& c)
{
	if(c.transport.IsEmpty())
		return;

	byte buf[8192];
	bool closed = false;
	for(;;) {
		int n = c.transport->Receive(buf, sizeof(buf));
		if(n < 0) {
			closed = true;
			break;
		}
		if(n == 0)
			break;
		c.parser.Feed(buf, n);
	}

	byte type;
	String payload;
	while(c.parser.Next(type, payload)) {
		switch(type) {
		case CMSG_HELLO: {
			String title;
			Size sz;
			int owner_window_id = -1;
			if(!c.hello_received && DecodeHello(payload, title, sz, owner_window_id)) {
				c.hello_received = true;
				c.title = title;
				c.size = sz;
				c.owner_window_id = owner_window_id;
				WhenConnect(c.index);
			}
			break;
		}
		case CMSG_DRAW_BATCH: {
			Vector<DrawCmd> cmds;
			if(c.hello_received && DecodeDrawBatch(payload, cmds)) {
				c.ApplyDrawBatch(cmds);
				LogNetDraw(c.index, cmds.GetCount(), c.draw_cmd_total);
				WhenDrawBatch(c.index);
			}
			break;
		}
		case CMSG_RESIZE: {
			Size sz;
			if(c.hello_received && DecodeSize(payload, sz))
				c.size = sz;
			break;
		}
		case CMSG_TITLE: {
			String title;
			if(c.hello_received && DecodeTitle(payload, title)) {
				c.title = title;
				LogNetTitle(c.index, title);
				WhenTitleChanged(c.index);
			}
			break;
		}
		case CMSG_BYE:
			closed = true;
			break;
		default:
			break;
		}
	}

	if(closed && !c.disconnect_notified) {
		c.disconnect_notified = true;
		LogNetDisconnect(c.index);
		if(c.hello_received) {
			WhenDisconnect(c.index); // backend owns tearing down the window + calling CloseAndRemove
			return;
		}
		clients.RemoveKey(c.index); // never got far enough to have a window; nothing to tear down
	}
}

void NetServer::Poll()
{
	AcceptNew();
	for(int i = clients.GetCount() - 1; i >= 0; i--)
		PumpClient(clients[i]);
}

NetClientSession *NetServer::Find(int client_index)
{
	return clients.FindPtr(client_index);
}

Image NetServer::GetSnapshot(int client_index)
{
	NetClientSession *c = Find(client_index);
	return c ? c->snapshot : Image();
}

void NetServer::SendWelcome(int client_index, int window_id)
{
	NetClientSession *c = Find(client_index);
	if(!c || c->transport.IsEmpty() || !c->transport->IsOpen())
		return;
	c->transport->Send(EncodeFrame(SMSG_WELCOME, EncodeWelcome(window_id)));
}

void NetServer::SendMouse(int client_index, byte kind, Point p, dword keyflags)
{
	NetClientSession *c = Find(client_index);
	if(!c || c->transport.IsEmpty() || !c->transport->IsOpen())
		return;
	c->transport->Send(EncodeFrame(SMSG_INPUT_MOUSE, EncodeInputMouse(kind, p, keyflags)));
}

void NetServer::SendKey(int client_index, byte kind, dword keycode)
{
	NetClientSession *c = Find(client_index);
	if(!c || c->transport.IsEmpty() || !c->transport->IsOpen())
		return;
	c->transport->Send(EncodeFrame(SMSG_INPUT_KEY, EncodeInputKey(kind, keycode)));
}

void NetServer::SendResize(int client_index, Size sz)
{
	NetClientSession *c = Find(client_index);
	if(!c || c->transport.IsEmpty() || !c->transport->IsOpen())
		return;
	if(c->size == sz)
		return; // already at this size (server's own record) -- nothing to tell the client
	c->size = sz;
	c->transport->Send(EncodeFrame(SMSG_WINDOW_RESIZED, EncodeSize(sz)));
	LogNetResize(client_index, sz);
}

void NetServer::CloseAndRemove(int client_index)
{
	int pos = clients.Find(client_index);
	if(pos < 0)
		return;
	NetClientSession& c = clients[pos];
	if(!c.transport.IsEmpty() && c.transport->IsOpen())
		c.transport->Send(EncodeFrame(SMSG_CLOSE, String()));
	if(!c.transport.IsEmpty())
		c.transport->Close();
	clients.Remove(pos);
}

#endif

END_UPP_NAMESPACE
