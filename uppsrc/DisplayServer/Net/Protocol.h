#ifndef _DisplayServer_Net_Protocol_h_
#define _DisplayServer_Net_Protocol_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------------
// DisplayServer wire protocol (NetworkDisplay/0006).
//
// Framing (both directions, symmetric, TCP is a byte stream so this is required):
//   [uint32 LE frame_length][byte msg_type][ (frame_length - 1) bytes: payload ]
// `frame_length` counts the msg_type byte plus the payload, so a frame carrying an
// empty payload still has frame_length == 1. See FrameParser below for the receive
// side (it accumulates raw bytes and yields complete (msg_type, payload) frames as
// they become available -- safe against TCP fragmenting/coalescing frames however
// it likes).
//
// Payload fields are encoded with Upp::Stream's builtin '%' serializer
// (see Stream::operator%): fixed-width integers are stored little-endian
// (SerializeRaw uses Put32le/Get32le etc.), and Upp::String is stored as a packed
// length prefix followed by raw bytes (Stream::operator%(String&)). Client and
// server must agree on the exact field order per message type; that order is
// documented next to each enum value below and mirrored 1:1 in DrawCmd::Serialize()
// and the Encode*/Decode* helpers in Protocol.cpp.
// ---------------------------------------------------------------------------------

// Default TCP port both DisplayServer and its clients agree on when --port isn't
// given explicitly.
enum { DEFAULT_DISPLAYSERVER_PORT = 47821 };

// Scans argv for "--port N" or "--port=N" (used identically by DisplayServer's two
// backends and by TestClient, so both sides default to the same port without
// duplicating the constant/parsing).
int GetDisplayServerPort(int default_port = DEFAULT_DISPLAYSERVER_PORT);

// Client -> DisplayServer.
enum ClientMsgType {
	CMSG_HELLO      = 1, // payload: String title, int32 width, int32 height, int32
	                     // owner_window_id (NetworkDisplay/0017: -1 if this window has
	                     // no owner, otherwise the DisplayServer-assigned window_id
	                     // (see SMSG_WELCOME) of the connection's owner window -- the
	                     // client can only supply this once it already knows the
	                     // owner's own window_id, i.e. after the owner's own
	                     // SMSG_WELCOME has arrived on the owner's connection)
	CMSG_DRAW_BATCH = 2, // payload: int32 command_count, then that many DrawCmd (see DrawCmd::Serialize)
	CMSG_RESIZE     = 3, // payload: int32 width, int32 height (client-requested canvas resize)
	CMSG_BYE        = 4, // payload: (empty) -- graceful disconnect notice
	CMSG_TITLE      = 5, // payload: String title (NetworkDisplay/0015) -- sent whenever the
	                     // client's hosted TopWindow's title changes after the initial
	                     // CMSG_HELLO (e.g. Ctrl::Title() called again later); DisplayServer
	                     // should update that connection's live frame title.
};

// DisplayServer -> client.
enum ServerMsgType {
	SMSG_WELCOME        = 1, // payload: int32 window_id
	SMSG_INPUT_MOUSE    = 2, // payload: byte kind (MouseKind), int32 x, int32 y, uint32 keyflags
	SMSG_INPUT_KEY      = 3, // payload: byte kind (KeyKind), uint32 keycode (raw Upp key code incl. modifier bits)
	SMSG_WINDOW_RESIZED = 4, // payload: int32 width, int32 height -- host frame resized by the desktop user
	SMSG_CLOSE          = 5, // payload: (empty) -- host window was closed, client should exit/detach
};

enum MouseKind {
	MOUSE_MOVE       = 0,
	MOUSE_LEFT_DOWN  = 1,
	MOUSE_LEFT_UP    = 2,
	MOUSE_RIGHT_DOWN = 3,
	MOUSE_RIGHT_UP   = 4,
};

enum KeyKind {
	KEY_DOWN = 0,
};

// Draw commands carried inside CMSG_DRAW_BATCH. Deliberately a minimal, custom, hand
// -rolled set (see NetworkDisplay/0006 status notes in the plan doc for why
// Eon/Draw's ProgDraw/ProgPainter were investigated and NOT reused as the wire
// format) -- just enough 2D primitives for a real client process to prove its actual
// drawing renders inside a DisplayServer-hosted window.
enum DrawOpType {
	DOP_CLEAR   = 0, // byte r,g,b -- fill the whole canvas with a solid color
	DOP_RECT    = 1, // int32 x,y,w,h; byte r,g,b -- filled rectangle
	DOP_LINE    = 2, // int32 x,y,x2,y2,width; byte r,g,b -- a single line segment
	DOP_ELLIPSE = 3, // int32 x,y,w,h; byte r,g,b -- filled ellipse inscribed in (x,y,w,h)
	DOP_TEXT    = 4, // int32 x,y; byte r,g,b; String text -- drawn with StdFont()
	DOP_IMAGE   = 5, // int32 x,y,w,h; String image_rgba (w*h*4 raw bytes, row-major:
	                 // r,g,b,a per pixel, r/g/b PREMULTIPLIED by a -- same convention
	                 // U++'s own Image/RGBA uses internally for IMAGE_ALPHA content,
	                 // see Premultiply()/AlphaBlend() in uppsrc/Draw/Image.h) -- raster
	                 // blit with a real per-pixel alpha channel. Added in
	                 // NetworkDisplay/0007 as an opaque RGB-only (w*h*3 bytes) blit; a
	                 // generic Ctrl/Draw client (unlike 0006's own hand-written
	                 // TestClient) ends up rasterizing almost everything -- glyphs,
	                 // icons, gradients -- through Draw::PutImage, so a real backend
	                 // needs a raw-pixel primitive alongside the flat-color ones 0006
	                 // shipped with. NetworkDisplay/0011 widened the payload to RGBA
	                 // (breaking change to this one op's payload shape -- no
	                 // versioning/back-compat requirement per 0006) because the
	                 // RGB-only shape forced fully-transparent source pixels to be
	                 // encoded as literal black and discarded partial alpha entirely,
	                 // so anti-aliased glyph edges/icon transparency rendered as solid
	                 // black boxes instead of blending against the real window
	                 // background.
};

struct DrawCmd : Moveable<DrawCmd> {
	byte   op = DOP_RECT;
	int    x = 0, y = 0, x2 = 0, y2 = 0, w = 0, h = 0;
	int    width = 1;
	byte   r = 255, g = 255, b = 255;
	String text;
	String image_rgba; // DOP_IMAGE payload only (w*h*4 bytes, premultiplied RGBA)

	// Symmetric encode/decode: reads current fields into `s` when storing, fills
	// fields from `s` when loading (standard Upp::Stream idiom).
	void Serialize(Stream& s);
};

// Builds one length-prefixed frame ready to hand to a transport's Send().
String EncodeFrame(byte msg_type, const String& payload);

// Payload encode/decode helpers. Each operates purely on the payload bytes (i.e.
// everything after the msg_type byte) -- overall frame length-prefixing is handled
// by EncodeFrame()/FrameParser.
// owner_window_id defaults to -1 ("no owner") so existing callers that only ever open
// ownerless windows (e.g. TestClient) don't need to pass it explicitly.
String EncodeHello(const String& title, Size sz, int owner_window_id = -1);
bool   DecodeHello(const String& payload, String& title, Size& sz, int& owner_window_id);

String EncodeDrawBatch(Vector<DrawCmd>& cmds);
bool   DecodeDrawBatch(const String& payload, Vector<DrawCmd>& cmds);

String EncodeSize(Size sz); // shared shape: CMSG_RESIZE and SMSG_WINDOW_RESIZED
bool   DecodeSize(const String& payload, Size& sz);

String EncodeTitle(const String& title); // CMSG_TITLE payload (NetworkDisplay/0015)
bool   DecodeTitle(const String& payload, String& title);

String EncodeWelcome(int window_id);
bool   DecodeWelcome(const String& payload, int& window_id);

String EncodeInputMouse(byte kind, Point p, dword keyflags);
bool   DecodeInputMouse(const String& payload, byte& kind, Point& p, dword& keyflags);

String EncodeInputKey(byte kind, dword keycode);
bool   DecodeInputKey(const String& payload, byte& kind, dword& keycode);

// Accumulates raw bytes arriving from a non-blocking socket (in whatever chunk sizes
// they happen to arrive) and extracts complete frames as they become available.
class FrameParser {
	String buf;

public:
	void Feed(const void *data, int len)  { buf.Cat((const char *)data, len); }
	void Feed(const String& data)         { buf.Cat(data); }
	bool IsEmpty() const                  { return buf.IsEmpty(); }

	// Extracts and removes the next complete frame, if one is fully buffered.
	// Returns false (buffer left untouched) if fewer than a full frame's bytes have
	// arrived yet -- call again after feeding more data.
	bool Next(byte& msg_type, String& payload);
};

END_UPP_NAMESPACE

#endif
