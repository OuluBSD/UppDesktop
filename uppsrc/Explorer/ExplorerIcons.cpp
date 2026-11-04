#include "ExplorerIcons.h"

#ifdef flagGUI

NAMESPACE_UPP

// Colours are picked as flat fills rather than drawn from a palette: a file icon
// has to be readable at 24px on a light background and at 64px on a dark one, and
// a theme lookup is not available to a plain Image.
static const Color col_folder_body = Color(232, 178, 62);
static const Color col_folder_tab  = Color(206, 150, 42);
static const Color col_paper       = Color(252, 252, 250);
static const Color col_edge        = Color(96, 96, 92);
static const Color col_drive_body  = Color(178, 182, 188);
static const Color col_drive_face  = Color(226, 229, 233);
static const Color col_text_band   = Color(140, 144, 150);
static const Color col_image_band  = Color(72, 148, 96);
static const Color col_audio_band  = Color(158, 96, 190);
static const Color col_video_band  = Color(198, 82, 74);
static const Color col_archive_band= Color(214, 140, 48);
static const Color col_code_band   = Color(72, 112, 186);
static const Color col_exec_band   = Color(74, 158, 96);
static const Color col_file_band   = Color(168, 168, 164);

// A canvas for the icons below.
//
//
// Not SImageDraw: its buffer is not zeroed by Create() -- debug builds poison
// freshly allocated pixels on purpose -- so an icon drawn into it comes out with
// vertical red and black stripes through every pixel that was never painted. And
// its operator Image() force-sets alpha to 255, throwing away the transparency the
// icons need to sit on a selection background. So the buffer is cleared here, and
// the result is assembled as a premultiplied-alpha image the same way
// Ctrl/Compositing does it.
struct IconCanvas : SImageDraw1 {
	using SImageDraw1::ib;   //< SImageDraw1 keeps its pixels protected

	void Create(Size sz)
	{
		ib.Create(sz);
		Upp::Fill(ib.Begin(), RGBAZero(), ib.GetLength());
		Init(sz);
	}

	operator Image() const
	{
		ImageBuffer b(ib.GetSize());
		memcpy(b, ib.Begin(), sizeof(RGBA) * ib.GetLength());
		b.SetKind(IMAGE_ALPHA);
		return Image(b);
	}
};

// Cache keyed by kind and size together, because the same kind is drawn at two
// sizes (one per icon mode) and a 16px picture reused at 64px is the exact
// shortcut this file exists to avoid.
static Index<int> icon_cache_keys;
static Vector<Image> icon_cache_images;

Image ExplorerIcons::Get(IconKind kind, int size)
{
	if(size < 8) size = 8;
	int key = (int)kind * 256 + size;
	int at = icon_cache_keys.Find(key);
	if(at >= 0) return icon_cache_images[at];

	Image img;
	switch(kind) {
	case ICON_FOLDER:
		img = MakeFolder(size, col_folder_body, col_folder_tab);
		break;
	case ICON_MOUNT:
		img = MakeDrive(size);
		break;
	case ICON_TEXT:
		img = MakeDocument(size, col_text_band, false);
		break;
	case ICON_IMAGE:
		img = MakeDocument(size, col_image_band, false);
		break;
	case ICON_AUDIO:
		img = MakeDocument(size, col_audio_band, false);
		break;
	case ICON_VIDEO:
		img = MakeDocument(size, col_video_band, false);
		break;
	case ICON_ARCHIVE:
		img = MakeDocument(size, col_archive_band, false);
		break;
	case ICON_CODE:
		img = MakeDocument(size, col_code_band, true);
		break;
	case ICON_EXECUTABLE:
		img = MakeDocument(size, col_exec_band, false);
		break;
	case ICON_FILE:
		img = MakeDocument(size, col_file_band, false);
		break;
	}

	icon_cache_keys.Add(key);
	icon_cache_images.Add(img);
	return img;
}

// Get icon for a MIME type, mapping it to one of the drawn kinds.
//
// The picture is drawn, not borrowed (see the package AGENTS.md): the MIME type
// only decides *which* drawn kind an entry gets, so that a `.tar.gz` and a `.txt`
// no longer look identical. A real icon-theme backend would be an addition behind
// this same call, not a replacement for it -- the drawn kind stays the floor a
// themeless desktop falls back to.
Image ExplorerIcons::GetMimeIcon(const String& mime_type, int size)
{
	if(size < 8) size = 8;

	// Convert MIME type to IconKind
	IconKind kind = ICON_FILE;
	if(mime_type.StartsWith("inode/")) {
		if(mime_type == "inode/directory") kind = ICON_FOLDER;
		else if(mime_type == "inode/mount-point") kind = ICON_MOUNT;
	}
	else if(mime_type.StartsWith("text/")) kind = ICON_TEXT;
	else if(mime_type.StartsWith("image/")) kind = ICON_IMAGE;
	else if(mime_type.StartsWith("audio/")) kind = ICON_AUDIO;
	else if(mime_type.StartsWith("video/")) kind = ICON_VIDEO;
	else if(mime_type.StartsWith("application/")) {
		if(mime_type.Find("zip") >= 0 || mime_type.Find("tar") >= 0 || mime_type.Find("gzip") >= 0 || mime_type.Find("x-rar") >= 0 || mime_type.Find("x-7z") >= 0)
			kind = ICON_ARCHIVE;
		else if(mime_type.Find("pdf") >= 0 || mime_type.Find("msword") >= 0 || mime_type.Find("vnd.openxmlformats") >= 0)
			kind = ICON_TEXT;
		else if(mime_type.Find("x-executable") >= 0 || mime_type.Find("x-sh") >= 0 || mime_type.Find("x-python") >= 0)
			kind = ICON_EXECUTABLE;
		else if(mime_type.Find("json") >= 0 || mime_type.Find("xml") >= 0 || mime_type.Find("javascript") >= 0 || mime_type.Find("x-httpd-php") >= 0)
			kind = ICON_CODE;
		else
			kind = ICON_FILE;
	}
	
	return Get(kind, size);
}

Image ExplorerIcons::GetFor(const ExplorerEntry& e, ViewMode mode)
{
	// The picture is drawn at the size its cell reserves for it, not at the cell's
	// full width: a cell is wider than its picture so long names have room. The
	// small mode's picture is the size a list row can carry without the name
	// being pushed off the end of the cell.
	int size = mode == VIEW_BIG_ICONS ? 64 : 20;
	
	// Try to get icon from MIME type if available
	if(!e.mime.IsEmpty())
		return GetMimeIcon(e.mime, size);
	
	// Fallback to kind-based icon
	return Get(EntryIconKind(e), size);
}

Image ExplorerIcons::MakeFolder(int size, Color body, Color tab)
{
	IconCanvas iw; iw.Create(Size(size, size));
	int m = size / 8;              //< a margin that scales
	int body_top = size / 3;
	iw.DrawRect(m, body_top, size - 2 * m, size - body_top - m, body);
	// The tab, drawn before the body edge so the two read as one shape.
	iw.DrawRect(m, body_top - m / 2, size / 3, m / 2 + 1, tab);
	iw.DrawRect(m, body_top - m / 2, 1, size - body_top + m / 2 - m, col_edge);
	iw.DrawRect(size - m - 1, body_top - m / 2, 1, size - body_top + m / 2 - m, col_edge);
	iw.DrawRect(m, size - m - 1, size - 2 * m, 1, col_edge);
	return iw;
}

Image ExplorerIcons::MakeDrive(int size)
{
	// A mount point is a drive: a box with a platter, not a folder. That distinction
	// is the whole reason IconKind has both.
	IconCanvas iw; iw.Create(Size(size, size));
	int m = size / 8;
	int h = size / 2;
	iw.DrawRect(m, m + h / 2, size - 2 * m, h, col_drive_body);
	iw.DrawRect(m + 1, m + h / 2 + 1, size - 2 * m - 2, h / 3, col_drive_face);
	iw.DrawEllipse(size / 2 - h / 3, m + h / 2 + h / 3 + 1, h / 3, h / 3, col_drive_face);
	iw.DrawRect(m, size - m - 1, size - 2 * m, 1, col_edge);
	iw.DrawRect(m, m + h / 2, size - 2 * m, 1, col_edge);
	return iw;
}

Image ExplorerIcons::MakeDocument(int size, Color band, bool folded)
{
	IconCanvas iw; iw.Create(Size(size, size));
	int m = size / 8;
	int w = size - 2 * m;
	int h = size - 2 * m;
	int corner = folded ? size / 4 : 0;

	iw.DrawRect(m, m, w, h, col_paper);
	if(folded) {
		// The corner is cut by painting the triangle back out in the border colour,
		// which is cheaper than clipping and keeps the silhouette square.
		Vector<Point> tri;
		tri.Add(Point(m + w - corner, m));
		tri.Add(Point(m + w - corner, m + corner));
		tri.Add(Point(m + w, m + corner));
		iw.DrawPolygon(tri, col_paper);
		iw.DrawLine(m + w - corner, m, m + w - corner, m + corner, 1, col_edge);
		iw.DrawLine(m + w - corner, m + corner, m + w, m + corner, 1, col_edge);
	}
	iw.DrawRect(m, m, 1, h, col_edge);
	iw.DrawRect(m, m, w, 1, col_edge);
	iw.DrawRect(m, m + h - 1, w, 1, col_edge);
	iw.DrawRect(m + w - 1, m, 1, h, col_edge);
	// The band is what tells the kinds apart at a glance; the shape is identical on
	// purpose, so the eye reads colour first and shape second.
	int band_h = h / 6 + 1;
	iw.DrawRect(m + 1, m + h - band_h - 1, w - 2, band_h, band);
	return iw;
}

END_UPP_NAMESPACE

#endif // flagGUI