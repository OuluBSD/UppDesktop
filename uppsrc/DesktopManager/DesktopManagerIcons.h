#ifndef _DesktopManager_DesktopManagerIcons_h_
#define _DesktopManager_DesktopManagerIcons_h_

//! DesktopManager icon definitions
//! Based on fluxbox icon styles and U++ conventions

#include <CtrlLib/CtrlLib.h>

NAMESPACE_UPP

//! Icon kind enumeration for DesktopManager
enum class DesktopIconKind {
	DESKTOP,
	FOLDER,
	APPLICATION,
	TERMINAL,
	FILE_MANAGER,
	SETTINGS,
	POWER,
	LOGOUT
};

//! Desktop manager icon images
/*!
  Icon images for the DesktopManager UI.
  These are referenced from the skin and draw functions.
*/
struct DesktopManagerIcons {
	Image         desktop;
	Image         folder;
	Image         application;
	Image         terminal;
	Image         file_manager;
	Image         settings;
	Image         power;
	Image         logout;
};

END_UPP_NAMESPACE

#endif // _DesktopManager_DesktopManagerIcons_h_