#ifndef _DesktopModel_FrameGeometry_h_
#define _DesktopModel_FrameGeometry_h_

#include <Core/Core.h>

//! Frame decoration insets: distance from frame edge to client area
/*!
  All values are in pixels. Left/right/bottom are typically the border width,
  top is border + titlebar height. Used for frame<->client rect conversion.
*/
struct FrameInsets {
	int left = 0;
	int right = 0;
	int top = 0;
	int bottom = 0;

	FrameInsets() {}
	FrameInsets(int l, int r, int t, int b) : left(l), right(r), top(t), bottom(b) {}

	//! Total horizontal inset
	int Horz() const { return left + right; }
	//! Total vertical inset
	int Vert() const { return top + bottom; }

	bool operator==(const FrameInsets& o) const {
		return left == o.left && right == o.right && top == o.top && bottom == o.bottom;
	}
	bool operator!=(const FrameInsets& o) const { return !(*this == o); }
};

//! Frame geometry helpers: frame rect <-> client rect conversion
/*!
  These are pure arithmetic helpers with no X11 dependencies, so they can be
  used in headless tests and in the model layer. The frame is the outer window
  that contains the client and its decorations (titlebar, buttons, borders).
*/
class FrameGeometry {
public:
	//! Default insets for a basic frame (4px borders, 24px titlebar)
	static FrameInsets DefaultInsets() { return FrameInsets(4, 4, 24, 4); }

	//! Convert client rect to frame rect (add insets)
	static Rect ClientToFrame(const Rect& client, const FrameInsets& insets) {
		return Rect(client.left - insets.left, client.top - insets.top,
		            client.right + insets.right, client.bottom + insets.bottom);
	}

	//! Convert frame rect to client rect (subtract insets)
	static Rect FrameToClient(const Rect& frame, const FrameInsets& insets) {
		return Rect(frame.left + insets.left, frame.top + insets.top,
		            frame.right - insets.right, frame.bottom - insets.bottom);
	}

	//! Client area rect within a frame at given position
	static Rect ClientRectInFrame(const Point& frame_pos, const Size& frame_size, const FrameInsets& insets) {
		return Rect(frame_pos.x + insets.left, frame_pos.y + insets.top,
		            frame_pos.x + frame_size.cx - insets.right, frame_pos.y + frame_size.cy - insets.bottom);
	}

	//! Frame size needed for a given client size
	static Size FrameSizeForClient(const Size& client_size, const FrameInsets& insets) {
		return Size(client_size.cx + insets.Horz(), client_size.cy + insets.Vert());
	}

	//! Client size that fits in a given frame size
	static Size ClientSizeInFrame(const Size& frame_size, const FrameInsets& insets) {
		return Size(frame_size.cx - insets.Horz(), frame_size.cy - insets.Vert());
	}
};

#endif // _DesktopModel_FrameGeometry_h_