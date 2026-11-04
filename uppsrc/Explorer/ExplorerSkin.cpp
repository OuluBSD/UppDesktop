#include "ExplorerSkin.h"

#ifdef flagGUI

#include <CtrlLib/CtrlLib.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Order matters inside ExplorerSkin, twice over.
//
// ChClassicSkin() reads SColorFace() *while it runs* to bake the button faces,
// so the palette has to be written before the call and not after.
//
// The whole of ExplorerSkin is installed with Ctrl::SetSkin(), and ReSkin()
// re-runs it, so every value it writes is written on every run rather than once
// at start-up. A "set it if it is still the default" guard would survive one
// ReSkin and then quietly stop working.
// ---------------------------------------------------------------------------

// The Windows-95/98 system palette, by its COLOR_* names, which are the names
// everyone who has seen this look knows them by. Nothing here is invented: these
// are the sixteen colours a 98 desktop is made of, and a file panel that mixes
// them with a modern grey is not "a bit 98", it is two looks fighting.
static void ExplorerPalette98()
{
	SColorFace_Write(Color(212, 208, 200));    // COLOR_3DFACE, the button face
	SColorMenu_Write(Color(212, 208, 200));     // COLOR_MENU, the same face
	SColorPaper_Write(SWhite());                // COLOR_WINDOW, white
	SColorText_Write(SBlack());                 // COLOR_WINDOWTEXT
	SColorLabel_Write(SBlack());
	SColorLight_Write(SWhite());                // COLOR_3DLIGHT
	SColorShadow_Write(Color(128, 128, 128));   // COLOR_3DSHADOW
	SColorDisabled_Write(Color(128, 128, 128)); // COLOR_GRAYTEXT
	SColorHighlight_Write(Color(0, 0, 128));    // COLOR_HIGHLIGHT: navy, not the
	                                           // modern #3232FA. Navy on white
	                                           // text is the 98 selection, and
	                                           // the tree, the listing and every
	                                           // toolbar check all follow from it.
	SColorHighlightText_Write(SWhite());
	SColorInfo_Write(Color(255, 255, 225));    // COLOR_INFO, the pale tooltip
	SColorInfoText_Write(SBlack());
	SColorMark_Write(Color(0, 0, 128));
	SColorMenuMark_Write(Color(0, 0, 128));

	// These two are *derived* colours whose initialisers ran at static-init time
	// against the stock palette: `CH_COLOR(SColorLtFace, Blend(SColorFace,
	// SColorLight))` evaluates the blend once, and nothing recomputes it. So
	// writing the palette without these two leaves every light bevel mixing the
	// new face with a blend of the old one, which is a grey that appears nowhere
	// in 98 and is not obviously wrong enough to be noticed. Write them.
	SColorLtFace_Write(Blend(SColorFace(), SColorLight()));
	SColorDkShadow_Write(Blend(SColorShadow(), SBlack()));

	// SColorEdge is not a CH_COLOR at all: it is `#define SColorEdge Black` at
	// uppsrc/Draw/DrawUtil.cpp:213. It cannot be written, and does not need to
	// be -- black *is* the 98 dark shadow.
}

// The 98 edit control: a two-pixel sunken groove around a white field.
//
// InsetBorder() is the repository's own sunken pair -- dark on top/left, light
// on bottom/right, with the black inner line -- so the field needs no image of
// its own. `ChBorder` carries the colours as *pointers* to the SColor globals
// (that is what the ColorF array in DrawUtil.cpp holds), so this keeps tracking
// the palette across a ReSkin instead of freezing the values from today.
static void ExplorerSkinFields()
{
	Value sunken_field = ChBorder(InsetBorder(), SColorPaper());
	EditField::Style& s = EditField::StyleDefault().Write();
	// All four edge states, not just the resting one: 98 draws the same groove
	// whether or not the field has focus, and a focus ring the 98 user does not
	// recognise is worse than none.
	for(int i = 0; i < 4; i++)
		s.edge[i] = sunken_field;
	s.paper = SColorPaper();
	s.activeedge = false;
}

static void ExplorerSkinToolBars()
{
	// A 98 toolbar button is flat at rest. It grows a border when the pointer is
	// over it, and is *sunken* while it is held down and while it is checked --
	// a permanently raised button is the modern look with extra steps, and three
	// rows of them are the most visible thing in this window.
	//
	// These are ToolBar::Style::buttonstyle, not ToolButton::StyleDefault():
	// the default toolbar button look is CtrlsImg::I_TB, and it is the *bar's*
	// copy of the style that its buttons use.
	Value raised = ChBorder(ThinOutsetBorder(), SColorFace());
	Value sunken = ChBorder(InsetBorder(), SColorFace());

	ToolButton::Style& b = ToolBar::StyleDefault().Write().buttonstyle;
	b.look[CTRL_NORMAL] = Null;     // flat: the bar's own face shows through
	b.look[CTRL_HOT] = raised;
	b.look[CTRL_PRESSED] = sunken;
	b.look[CTRL_DISABLED] = Null;
	b.look[CTRL_CHECKED] = sunken;      // a toggled button stays down
	b.look[CTRL_HOTCHECKED] = sunken;
	b.offset[CTRL_NORMAL] = Point(0, 0);
	// The 98 tell for a pressed button is the icon moving down and right by one
	// pixel inside the groove it just sank into. Without it the button sinks and
	// the icon does not, and it reads as a repaint.
	b.offset[CTRL_PRESSED] = Point(1, 1);
	b.offset[CTRL_CHECKED] = Point(1, 1);
	b.offset[CTRL_HOTCHECKED] = Point(1, 1);
	// light[] draws a light outline *around* the icon, which is the XP hot
	// affordance; contrast[] tints the icon, and ToolButton::Paint() already
	// calls DisabledImage() for a disabled button on top of it. Both off: a
	// disabled command in 98 is a grey icon, and a contrast on top of Disabled
	// turns it into a dark smear.
	for(int i = 0; i < 6; i++) {
		b.light[i] = false;
		b.contrast[i] = 0;
	}
	// The bar's own background. Left as the default it is a face-coloured strip
	// with a rounded look in it; a 98 toolbar is flat face all the way across.
	// Null is *not* "flat": ChPaint draws nothing for Null and the GTK backend
	// shows black for a region nobody painted (no backing store), so the whole
	// bar row read as a black stripe. A colour is a legal look -- StdChLookFn
	// paints it with one DrawRect, and PaintBarArea / StaticBarArea both route
	// through ChPaint -- so "flat face" is a face colour, the thing Null was
	// reaching for.
	ToolBar::Style& s = ToolBar::StyleDefault().Write();
	s.look = SColorFace();
	s.arealook = SColorFace();
}

// A stripe with a one-pixel line on one side and a one-pixel line on the other,
// stretched to any size.
//
// ChBorder cannot draw this. It paints one ring *around* a rect, and the 98
// divider is two *columns* of a vertical divider -- dark on the left, light on
// the right -- with the face between them. A three-by-three image with a hot spot
// is the mechanism that fits: ChLookFnImage() nine-slices it, so the four
// corners stay 1x1, the edges and the middle stretch, and a 3x3 image becomes a
// stripe of any width.
static Value EtchedStripe(bool divider_runs_vertically)
{
	ImageBuffer ib(3, 3);
	Color face = SColorFace(), dark = SColorShadow(), light = SColorLight();
	Fill(ib, Rect(ib.GetSize()), face);
	// `ib[i]` is a *line* (an RGBA*), not a pixel, so `ib[i] = c` assigns the
	// pointer and the pixels stay whatever the buffer was cleared to. `~ib` is
	// the flat pixel pointer; the indices below are row-major over 3x3.
	RGBA *px = ~ib;
	if(divider_runs_vertically) {
		px[0] = px[3] = px[6] = dark;    // left column
		px[2] = px[5] = px[8] = light;   // right column
	}
	else {
		px[0] = px[1] = px[2] = dark;    // top row
		px[6] = px[7] = px[8] = light;   // bottom row
	}
	// The hot spot at the centre pixel is what turns the nine-slice on: with the
	// spot there, the two outer rows and columns are the 1px border and the
	// middle one is the stretch. Without it the whole image is one blob.
	ib.SetHotSpot(Point(1, 1));
	return Value(Image(ib));
}

static void ExplorerSkinSplitter()
{
	Splitter::Style& s = Splitter::StyleDefault().Write();
	// No grip dots. The dots are a *knob* affordance -- they say "there is
	// something here to grab" -- and on a 4px divider that has been the whole
	// time, the 98 look is an etched groove you drag anywhere along.
	s.dots = false;
	s.width = 4;
	// A splitter's `horz` style paints a *vertical* divider (its name says which
	// way the panes are laid out, not which way the divider runs), so horz gets
	// the left/right stripe and vert the top/bottom one.
	s.horz[0] = EtchedStripe(true);
	s.horz[1] = s.horz[0];   // dragging does not change how it looks in 98
	s.vert[0] = EtchedStripe(false);
	s.vert[1] = s.vert[0];
}

// The status strip: the face, with a dark line along the top and nothing on the
// other three sides. The 98 strip's bottom edge is the window's, and a ring
// would put a line where there is none.
//
// The same three-by-three nine-slice as the divider, with a different stripe:
// only the top row differs from the face.
static Value EtchedStripeTop()
{
	ImageBuffer ib(3, 3);
	Color face = SColorFace(), dark = SColorShadow();
	Fill(ib, Rect(ib.GetSize()), face);
	RGBA *px = ~ib;
	px[0] = px[1] = px[2] = dark;       // top row only
	ib.SetHotSpot(Point(1, 1));
	return Value(Image(ib));
}

void ExplorerSkin()
{
	ExplorerPalette98();   // first: ChClassicSkin bakes button faces from SColorFace
	// The upstream 95/98 skin, already written: uppsrc/CtrlLib/Ch.cpp:139. It
	// builds the two-tone bevel for the buttons, the scrollbars, the separators,
	// the menu bar and the tab strip, and sets GUISTYLE_CLASSIC. It does not
	// touch the fields, the toolbars, the splitters or the status strip -- the
	// four things below are exactly that gap.
	ChClassicSkin();
	ExplorerSkinFields();
	ExplorerSkinToolBars();
	ExplorerSkinSplitter();
	// The status strip's *look* is one ChPaint of one Value over the whole rect,
	// so it is a stripe: the face, with a dark line along the top. 98 draws no
	// line on the other three sides -- the strip's bottom is the window's edge --
	// so a ChBorder ring is the wrong shape again and the image nine-slice is
	// the right one. The panels around the cells are drawn by ExplorerStatusCells
	// in Explorer.cpp, which is not a style: a cell is not a member of any Style
	// struct, and putting a per-cell border there would mean reimplementing
	// InfoCtrl::Paint.
	StatusBar::StyleDefault().Write().look = EtchedStripeTop();
}

// A look, as something a person can read in a log. A Value prints as its type
// and nothing else for the composite shapes, so a ChBorder -- the sunken field --
// printed as an empty string, which is exactly the wrong answer: "the field has no
// edge" and "the field has an edge this printer cannot show" look the same.
static String DescribeLook(const Value& v)
{
	if(IsNull(v)) return "null/flat";
	if(IsType<sChBorder>(v)) {
		const sChBorder& b = ValueTo<sChBorder>(v);
		const ColorF *c = b.border;
		int n = (int)(intptr_t)*c;
		c++;
		StringStream ss;
		ss << "border " << n << "px";
		for(int i = 0; i < n; i++) {
			ss << " [" << Color(c[0]) << " " << Color(c[1]) << " " << Color(c[2]) << " " << Color(c[3]) << "]";
			c += 4;
		}
		return ss.GetResult();
	}
	if(IsType<Image>(v)) {
		Image img = v;
		return "image " << img.GetSize() << " hotspot " << img.GetHotSpot();
	}
	if(IsType<Color>(v)) return "color " << (Color)v;
	return v.GetTypeName();
}

String ExplorerSkinProbe()
{
	StringStream ss;
	ss << "face " << SColorFace() << " paper " << SColorPaper()
	   << " shadow " << SColorShadow() << " light " << SColorLight()
	   << " highlight " << SColorHighlight() << " highlighttext " << SColorHighlightText()
	   << " text " << SColorText() << " disabled " << SColorDisabled()
	   << " ltface " << SColorLtFace() << " dkshadow " << SColorDkShadow();
	// The four looks ExplorerSkin supplies, reported by what they resolve to
	// rather than by the Value's type: "sunken" is only useful if the numbers
	// behind it are also printed, and a Color prints as one.
	ss << "\n  gui style " << (int)GUI_GlobalStyle()
	   << " (classic=" << (int)GUISTYLE_CLASSIC << ")"
	   << " field edge " << DescribeLook(EditField::StyleDefault().edge[0])
	   << " toolbar normal " << (IsNull(ToolBar::StyleDefault().buttonstyle.look[CTRL_NORMAL]) ? String("null/flat") : String("painted"))
	   // The bar's own background looks. "null/flat" here was the black stripe:
	   // the skin wrote Null to mean flat face, and the GTK backend painted
	   // nothing. A colour names what the bar actually paints.
	   << " toolbar look " << DescribeLook(ToolBar::StyleDefault().look)
	   << " toolbar area " << DescribeLook(ToolBar::StyleDefault().arealook)
	   << " splitter dots " << (Splitter::StyleDefault().dots ? 1 : 0)
	   << " splitter width " << Splitter::StyleDefault().width
	   << " status look " << StatusBar::StyleDefault().look;
	return ss.GetResult();
}

END_UPP_NAMESPACE

#endif
