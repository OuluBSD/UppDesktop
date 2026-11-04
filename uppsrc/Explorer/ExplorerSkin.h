#ifndef _Explorer_ExplorerSkin_h
#define _Explorer_ExplorerSkin_h

#ifdef flagGUI

#include <Core/Core.h>

NAMESPACE_UPP

// The Windows-95/98 look, installed before the window is built.
//
// Ctrl::SetSkin() takes a *function*, not a style object, and Ctrl::ReSkin()
// re-runs exactly that one function between ChReset() and ChFinish(). So the
// whole look has to be one nullary function: a skin that were split into
// "call ChClassicSkin()" plus "then patch a few styles" would have its patches
// wiped by the next ReSkin -- which is what happens on a display change, and
// silently, so the window would come back half-skinned with nothing logged.
//
// Everything here is presentation. No layout, no behaviour, and nothing the
// headless twin can observe: it is a window that looks like the thing it is.
void ExplorerSkin();

// The palette and the five things ExplorerSkin fills in that ChClassicSkin does
// not, as one line for --dump-ui. The widget tree cannot show a colour, and a
// skin that failed to install is otherwise indistinguishable from one that
// installed and drew nothing -- both are a flat window. This is the evidence
// that separates them, so it reports the *values* and not just the names.
String ExplorerSkinProbe();

END_UPP_NAMESPACE

#endif

#endif
