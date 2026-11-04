// Minimal throwaway network client for DisplayServer (NetworkDisplay/0006).
//
// Connects over TCP, sends a CMSG_HELLO + one CMSG_DRAW_BATCH containing a handful of
// distinctive shapes (bright magenta rectangle, green ellipse, yellow line, text), then
// lingers for a while printing any input events DisplayServer forwards back (mouse
// clicks/moves, key presses inside the resulting window, or a close notification),
// before sending CMSG_BYE and disconnecting.
//
// This exists purely to prove the wire protocol/transport end-to-end (see the plan
// doc's Status section for exact verification steps/output) ahead of NetworkDisplay/
// 0007's much larger real-app (UWord) integration test.
#include <Core/Core.h>
#include <DisplayServer/Net/Protocol.h>
#include <DisplayServer/Net/Transport.h>

using namespace Upp;

CONSOLE_APP_MAIN
{
	String host = "127.0.0.1";
	int    port = GetDisplayServerPort();
	int    linger_ms = 15000;

	const Vector<String>& args = CommandLine();
	for(int i = 0; i < args.GetCount(); i++) {
		if(args[i] == "--host" && i + 1 < args.GetCount())
			host = args[++i];
		else if(args[i] == "--port" && i + 1 < args.GetCount())
			port = StrInt(args[++i]);
		else if(args[i] == "--linger" && i + 1 < args.GetCount())
			linger_ms = StrInt(args[++i]);
	}

	String endpoint = host + ":" + AsString(port);
	Cout() << "TestClient: connecting to " << endpoint << " ...\n";

	NetworkDisplayTransport t;
	if(!t.Connect(endpoint)) {
		Cerr() << "TestClient: failed to connect to " << endpoint << "\n";
		SetExitCode(1);
		return;
	}
	Cout() << "TestClient: connected (" << t.Describe() << ")\n";

	Size canvas(320, 240);
	t.Send(EncodeFrame(CMSG_HELLO, EncodeHello("TestClient", canvas)));

	Vector<DrawCmd> cmds;
	{
		DrawCmd& c = cmds.Add();
		c.op = DOP_CLEAR;
		c.r = 20; c.g = 20; c.b = 40;
	}
	{
		// Distinctive bright magenta rectangle -- the shape/color the verification
		// step looks for in the resulting DisplayServer window.
		DrawCmd& c = cmds.Add();
		c.op = DOP_RECT;
		c.x = 20; c.y = 20; c.w = 120; c.h = 80;
		c.r = 255; c.g = 0; c.b = 255;
	}
	{
		DrawCmd& c = cmds.Add();
		c.op = DOP_ELLIPSE;
		c.x = 160; c.y = 30; c.w = 120; c.h = 100;
		c.r = 0; c.g = 255; c.b = 120;
	}
	{
		DrawCmd& c = cmds.Add();
		c.op = DOP_LINE;
		c.x = 10; c.y = 200; c.x2 = 300; c.y2 = 200; c.width = 4;
		c.r = 255; c.g = 255; c.b = 0;
	}
	{
		DrawCmd& c = cmds.Add();
		c.op = DOP_TEXT;
		c.x = 20; c.y = 140;
		c.r = 255; c.g = 255; c.b = 255;
		c.text = "Hello from TestClient";
	}
	t.Send(EncodeFrame(CMSG_DRAW_BATCH, EncodeDrawBatch(cmds)));
	Cout() << "TestClient: sent HELLO + " << cmds.GetCount() << " draw command(s)\n";

	FrameParser parser;
	int start = msecs();
	bool closed_by_server = false;
	while(msecs(start) < linger_ms) {
		byte buf[4096];
		int n = t.Receive(buf, sizeof(buf));
		if(n < 0) {
			Cout() << "TestClient: connection closed by server\n";
			closed_by_server = true;
			break;
		}
		if(n > 0)
			parser.Feed(buf, n);

		byte type;
		String payload;
		while(parser.Next(type, payload)) {
			switch(type) {
			case SMSG_WELCOME: {
				int window_id = -1;
				DecodeWelcome(payload, window_id);
				Cout() << "TestClient: WELCOME window_id=" << window_id << "\n";
				break;
			}
			case SMSG_INPUT_MOUSE: {
				byte kind = 0;
				Point p(0, 0);
				dword keyflags = 0;
				DecodeInputMouse(payload, kind, p, keyflags);
				Cout() << "TestClient: MOUSE kind=" << (int)kind << " at (" << p.x << "," << p.y
				       << ")\n";
				break;
			}
			case SMSG_INPUT_KEY: {
				byte kind = 0;
				dword keycode = 0;
				DecodeInputKey(payload, kind, keycode);
				Cout() << "TestClient: KEY kind=" << (int)kind << " code=" << (int64)keycode
				       << "\n";
				break;
			}
			case SMSG_WINDOW_RESIZED: {
				Size sz;
				DecodeSize(payload, sz);
				Cout() << "TestClient: WINDOW_RESIZED " << sz.cx << "x" << sz.cy << "\n";
				break;
			}
			case SMSG_CLOSE:
				Cout() << "TestClient: server closed our window\n";
				closed_by_server = true;
				break;
			default:
				break;
			}
		}
		if(closed_by_server)
			break;
		Sleep(20);
	}

	if(!closed_by_server) {
		t.Send(EncodeFrame(CMSG_BYE, String()));
		Cout() << "TestClient: linger time elapsed, sent BYE\n";
	}
	t.Close();
	Cout() << "TestClient: done\n";
}
