#ifndef _Explorer_ExplorerIcons_h
#define _Explorer_ExplorerIcons_h

#ifdef flagGUI

#include <Draw/Draw.h>
#include "ExplorerModel.h"

NAMESPACE_UPP

// Pictures for the icon view.
//
// CtrlLib's CtrlImg is 16x16 and its Dir/File glyphs are the only ones this fork
// ships. Scaling those up to fill a 64px cell gives the mushy, grey file manager
// look that "borrowed icons" means, so these are drawn instead, at the exact size
// the cell uses. Drawing them costs a few hundred microseconds once per kind and
// size, and the results are cached.
//
// Everything here is presentation: the *choice* of icon for an entry is
// ExplorerModel::EntryIconKind(), which the headless twin prints, so classification
// is testable and this file is not.
struct ExplorerIcons {
	//< Picture for `kind` at `size` pixels square. Cached; the same Image comes
	//< back every time, so it is safe to compare and to keep.
	static Image Get(IconKind kind, int size);

	//< Picture for a MIME type at `size` pixels square. Uses Desktop backend
	//< for theme lookup, falls back to built-in icons.
	static Image GetMimeIcon(const String& mime_type, int size);

	//< Picture for an entry, at the size the given view mode draws at.
	static Image GetFor(const ExplorerEntry& e, ViewMode mode);

private:
	static Image MakeFolder(int size, Color body, Color tab);
	static Image MakeDrive(int size);
	static Image MakeDocument(int size, Color band, bool folded);
};

END_UPP_NAMESPACE

#endif // flagGUI
#endif