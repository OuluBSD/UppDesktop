#include "Protocol.h"

NAMESPACE_UPP

int GetDisplayServerPort(int default_port)
{
	const Vector<String>& args = CommandLine();
	for(int i = 0; i < args.GetCount(); i++) {
		if(args[i] == "--port" && i + 1 < args.GetCount())
			return StrInt(args[i + 1]);
		if(args[i].StartsWith("--port="))
			return StrInt(args[i].Mid(7));
	}
	return default_port;
}

void DrawCmd::Serialize(Stream& s)
{
	s % op;
	switch(op) {
	case DOP_CLEAR:
		s % r % g % b;
		break;
	case DOP_RECT:
	case DOP_ELLIPSE:
		s % x % y % w % h % r % g % b;
		break;
	case DOP_LINE:
		s % x % y % x2 % y2 % width % r % g % b;
		break;
	case DOP_TEXT:
		s % x % y % r % g % b % text;
		break;
	case DOP_IMAGE:
		s % x % y % w % h % image_rgba;
		break;
	default:
		break;
	}
}

String EncodeFrame(byte msg_type, const String& payload)
{
	dword frame_len = (dword)payload.GetCount() + 1;
	char head[5];
	head[0] = (char)(frame_len & 0xFF);
	head[1] = (char)((frame_len >> 8) & 0xFF);
	head[2] = (char)((frame_len >> 16) & 0xFF);
	head[3] = (char)((frame_len >> 24) & 0xFF);
	head[4] = (char)msg_type;
	String out(head, 5);
	out.Cat(payload);
	return out;
}

bool FrameParser::Next(byte& msg_type, String& payload)
{
	if(buf.GetCount() < 5)
		return false;
	const byte *p = (const byte *)buf.Begin();
	dword frame_len = (dword)p[0] | ((dword)p[1] << 8) | ((dword)p[2] << 16) | ((dword)p[3] << 24);
	if(frame_len < 1 || frame_len > (dword)(64 * 1024 * 1024)) {
		// Malformed/hostile length prefix -- our own encoder never produces this;
		// drop everything buffered so a corrupted stream doesn't wedge forever.
		buf.Clear();
		return false;
	}
	if((dword)buf.GetCount() < 4 + frame_len)
		return false; // wait for more bytes
	msg_type = p[4];
	payload = buf.Mid(5, frame_len - 1);
	buf.Remove(0, 4 + frame_len);
	return true;
}

String EncodeHello(const String& title, Size sz, int owner_window_id)
{
	StringStream ss;
	ss.Create();
	String t = title;
	int w = sz.cx, h = sz.cy;
	int owner = owner_window_id;
	ss % t % w % h % owner;
	return ss.GetResult();
}

bool DecodeHello(const String& payload, String& title, Size& sz, int& owner_window_id)
{
	StringStream ss(payload);
	int w = 0, h = 0;
	int owner = -1;
	ss % title % w % h % owner;
	sz = Size(w, h);
	owner_window_id = owner;
	return !ss.IsError();
}

String EncodeDrawBatch(Vector<DrawCmd>& cmds)
{
	StringStream ss;
	ss.Create();
	int n = cmds.GetCount();
	ss % n;
	for(DrawCmd& c : cmds)
		c.Serialize(ss);
	return ss.GetResult();
}

bool DecodeDrawBatch(const String& payload, Vector<DrawCmd>& cmds)
{
	StringStream ss(payload);
	int n = 0;
	ss % n;
	cmds.Clear();
	if(n < 0 || n > 200000)
		return false;
	for(int i = 0; i < n; i++) {
		DrawCmd& c = cmds.Add();
		c.Serialize(ss);
	}
	return !ss.IsError();
}

String EncodeSize(Size sz)
{
	StringStream ss;
	ss.Create();
	int w = sz.cx, h = sz.cy;
	ss % w % h;
	return ss.GetResult();
}

bool DecodeSize(const String& payload, Size& sz)
{
	StringStream ss(payload);
	int w = 0, h = 0;
	ss % w % h;
	sz = Size(w, h);
	return !ss.IsError();
}

String EncodeTitle(const String& title)
{
	StringStream ss;
	ss.Create();
	String t = title;
	ss % t;
	return ss.GetResult();
}

bool DecodeTitle(const String& payload, String& title)
{
	StringStream ss(payload);
	ss % title;
	return !ss.IsError();
}

String EncodeWelcome(int window_id)
{
	StringStream ss;
	ss.Create();
	int id = window_id;
	ss % id;
	return ss.GetResult();
}

bool DecodeWelcome(const String& payload, int& window_id)
{
	StringStream ss(payload);
	ss % window_id;
	return !ss.IsError();
}

String EncodeInputMouse(byte kind, Point p, dword keyflags)
{
	StringStream ss;
	ss.Create();
	byte k = kind;
	int x = p.x, y = p.y;
	dword kf = keyflags;
	ss % k % x % y % kf;
	return ss.GetResult();
}

bool DecodeInputMouse(const String& payload, byte& kind, Point& p, dword& keyflags)
{
	StringStream ss(payload);
	int x = 0, y = 0;
	ss % kind % x % y % keyflags;
	p = Point(x, y);
	return !ss.IsError();
}

String EncodeInputKey(byte kind, dword keycode)
{
	StringStream ss;
	ss.Create();
	byte k = kind;
	dword kc = keycode;
	ss % k % kc;
	return ss.GetResult();
}

bool DecodeInputKey(const String& payload, byte& kind, dword& keycode)
{
	StringStream ss(payload);
	ss % kind % keycode;
	return !ss.IsError();
}

END_UPP_NAMESPACE
