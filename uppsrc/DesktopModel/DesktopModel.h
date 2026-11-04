#ifndef _DesktopModel_DesktopModel_h_
#define _DesktopModel_DesktopModel_h_

//! Headless models behind the X11 desktop packages
/*!
  `WorkspaceModel`, `WindowListModel` and `ScreenInfoModel` hold the
  authoritative state of `uppsrc/DesktopManager`, `uppsrc/TaskBar` and
  `uppsrc/DisplayManager`. This package uses `Core` only: no `Ctrl`, no
  `TopWindow`, no Xlib, no `Xdisplay`, and no `LOG`. Every transition is
  therefore reachable from a caller that has neither a display nor an event
  loop, which is what `ai/rules/headless-gui-dual-purpose.md` asks for.

  The model/view boundary is frozen in `ai/info/desktop-model-view-split.md`,
  and each of the three views has the per-method table next to its own code.
  Read both before adding anything here.
*/

#include <Core/Core.h>

NAMESPACE_UPP

// WindowSource.h comes first: it owns WindowEntry, the exchange type a source
// reports and the model consumes, and WindowListModel's workspace filter reads
// WindowSource::AllDesktops. The order inside this header is therefore not
// cosmetic -- see DM5-002.
#include "WindowSource.h"
#include "WorkspaceModel.h"
#include "WindowListModel.h"
#include "ScreenInfoModel.h"
#include "FrameGeometry.h"

END_UPP_NAMESPACE

#endif // _DesktopModel_DesktopModel_h_
