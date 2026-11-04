// Desktop.h
//
// Umbrella header for the Core/Desktop service contract. Includes the
// backend interface and the selection entry point. Platform backends live in
// their own subpackages and are included only by their own main headers; this
// header never pulls in a platform backend, so a caller that wants only the
// contract can include <Core/Desktop/Desktop.h> and link the backend it needs.

#ifndef _Core_Desktop_Desktop_h_
#define _Core_Desktop_Desktop_h_

#include <Core/Core.h>

#include "DesktopCommon.h"
#include "DesktopBackend.h"

#endif // _Core_Desktop_Desktop_h_