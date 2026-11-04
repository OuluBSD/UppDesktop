# DesktopX11 Package

## Scope
Applies to `uppsrc/DesktopX11` and its entire sub-tree.

## Purpose
- The two production sources for the desktop. `X11WindowSource` asks the X server
  for every window it manages rather than asking Ctrl for the windows this
  process made; `X11ScreenSource` reports screen geometry, physical millimetres
  and DPI inputs.
- A GUI-free, X11-linking package, so a check package can link a real source and
  run it against a live server. Both sources are here for that reason, not because
  they belong together: `X11ScreenSource` sat in `uppsrc/DisplayManager`, a MAIN
  package, until DM4-005 moved it.

## Why this package exists and not a file in DesktopManager

`uppsrc/DesktopModel` must keep building with no GUI flag and no display, so it
cannot hold anything that opens an X connection. That leaves the source in a
view package — which is where `X11ScreenSource` still lives, in
`uppsrc/DisplayManager`, a MAIN package. A MAIN package cannot be linked from a
check package without colliding `main`, so that source's dead-server path is
verified by hand rather than by a test, and its `AGENTS.md` says so under Known
Gaps.

This package removes that constraint:

| | before DM4-005 | now |
|---|---|---|
| `X11ScreenSource` lived in | `uppsrc/DisplayManager` (MAIN) | here |
| `X11WindowSource` lived in | nowhere | here |
| GUI flag | `GUI X11` | `X11` only |
| links X11 via | `CtrlCore`'s manifest | own `library(POSIX X11) X11` |
| linkable from a check | no, `main` collides | yes: `upptst/ScreenSourceCheck`, `upptst/WindowSourceCheck` |

The flag split is the whole mechanism. `GUI X11` defines `flagGUI`, which is
what `CtrlCore` needs; `X11` alone defines only `flagX11`, which is all Xlib
does. `DesktopX11.h` includes `X11/Xlib.h` at file scope before opening the
namespace, because Xlib's `Window`, `Display`, `None` and `Status` must not land
in `namespace Upp`; the headers below write them as `::Window` and `::Display`.

`WindowSource`, the interface itself, is in `uppsrc/DesktopModel` beside
`WindowListModel`, and is Core-only like everything in that package.

## Layout
| File | Role |
|------|------|
| `DesktopX11.upp` | Manifest; `uses Core, DesktopModel`, adds `libX11` when the `X11` flag is set |
| `DesktopX11.h` | Umbrella header; includes Xlib, then the source inside `namespace Upp` |
| `X11WindowSource.h` / `.cpp` | The window source: enumerate, classify, read titles and desktops, raise and focus, and watch the root for `SubstructureNotify` |
| `X11ScreenSource.h` / `.cpp` | The screen source: screen count, geometry, millimetres. Moved here from `uppsrc/DisplayManager` by DM4-005 |

| `X11WindowManager.h` / `.cpp` | The window manager role (phase 0006): takes over the root via SubstructureRedirect and services its event queue (MapRequest, ConfigureRequest, CirculateRequest, etc.) from its own thread, on a dedicated connection |
| `X11FrameManager.h` / `.cpp` | The frame manager (DM6-003): creates a frame window per client, SaveSet-inserts the client, sets its border to 0, reparents it into the frame's client area at the model-side insets, and maps both; removes the frame on unmap/destroy |
| `X11Utils.h` / `.cpp` | The Xlib helpers shared by the source, the manager and the frame manager: connection open/close, the tolerant error handler and its saved predecessor, atom lookup, and the property readers. Shared so the BLITZ build has one definition of each, not one per translation unit |

## What the source reads

Fixed by `ai/info/desktop-model-view-split.md`, section "The system-wide window
source":

| Property | Read for |
|----------|----------|
| `_NET_CLIENT_LIST` on the root | the set of managed windows, in stacking order |
| `_NET_WM_NAME`, falling back to `WM_NAME` | the title |
| `_NET_WM_DESKTOP` | the workspace index; `0xFFFFFFFF` is reported `AllDesktops`; filled into every `WindowEntry` as the workspace snapshot the model filter reads |
| `_NET_CURRENT_DESKTOP` on the root | the manager's current workspace, read on the same connection as the active window, refreshed per query; `CurrentDesktop()` reports it |
| `_NET_WM_STATE` | skip `_NET_WM_STATE_HIDDEN`, a minimized window |
| `_NET_WM_WINDOW_TYPE` | skip tooltip, dock and desktop windows |

A window that is not `IsViewable` is also skipped, which is not an EWMH rule: see
the hazards below.

The enumeration primitive is reused, not rewritten: `FindScreensStruts()` at
`uppsrc/CtrlCore/X11App.cpp:480` already reads `_NET_CLIENT_LIST` off the root,
and `XAtom()` and `GetPropertyInts()` already exist in the repo.

## Two connections, on purpose
The reads open a connection per query and close it. The watch holds a second
connection open for the life of the object, because events need one: a connection
closed between queries never receives anything.

Keeping them apart is what makes both properties hold at once. The reads cannot go
stale, so a server that disappears makes `ReadWindows()` return empty instead of a
list read from a dead socket. And the watch never affects a read, so a watch
connection that dies takes out the change notifications and nothing else.

`Watch()` selects `SubstructureNotifyMask | PropertyChangeMask` on the root.
Property change is selected as well because a window can change its title, its
workspace or its state without being created or destroyed, and a taskbar watching
only for open and close would show a stale title until something else happened.

`Poll()` drains with `XPending()`, which never blocks: with an empty queue it is
one check of the socket buffer and no round trip. `ReadWindows()` clears the dirty
flag, because that is the read the flag refers to, so a view cannot forget to clear
it and then spin re-reading for the rest of the session.

`Watch()` returns false on a second call, so a double call cannot leak a
connection, and the destructor closes it.

## Two decisions that are easy to get wrong

**The `_NET_CLIENT_LIST` fallback is not optional.** The atom only exists when an
EWMH-compliant window manager is running. On a fresh Xvfb,
`xprop -root _NET_CLIENT_LIST` answers "no such atom on any window", which is
every check in `upptst/WindowSourceCheck` unless something publishes it. A source
that reported an empty list there would make the whole suite pass for the wrong
reason. `Enumerate()` falls back to `XQueryTree()` on the root and keeps the
children whose override-redirect bit is clear.

**Handles are interned, not derived from the window id.** `WindowListModel::Sync()`
drops any handle the source stopped reporting, so a handle that changed identity
between two queries would silently reorder the list and break focus cycling.
`HandleOf()` allocates once per native window and `Forget()` releases it.

## Hazards

- **A stale `_NET_CLIENT_LIST` entry must not end the desktop.** The client list
  belongs to the window manager, and a manager that crashed, or a list read
  while a window was dying, can name windows that are gone. `XGetWindowAttributes`
  on such an id generates a `BadWindow` error, and Xlib error handlers are
  process-global: CtrlCore's panics on any error, so in a GUI process this is
  the desktop ending. The tolerant handler (`TolerantXErrorHandler`, in
  `X11Utils.cpp` and installed by every connection this package opens) swallows
  `BadWindow`, `BadDrawable`, `BadMatch` and `BadAtom`, and only on connections
  the process itself opened, tracked in `OwnedDisplays()`, and
  chains every other error to the handler installed before it, so CtrlCore keeps
  panicking on its own windows' errors. Every connection this source opens goes
  through `OpenConnection()`/`CloseConnection()`; the close syncs first so an
  error from a request of ours is dispatched while the connection is still
  registered, because after `XCloseDisplay` the same address can be handed to
  the next `XOpenDisplay` and a scope check would lie.
- **`XInternAtom` with `only_if_exists` returns `None`, and `None` is a fatal
  property.** Asking `XGetWindowProperty` for property `0` is `BadAtom` and takes
  the process down with no output. This was the first thing the live run found:
  every check below "no display reachable" had passed while the live path died
  instantly. `XAtomOf()` returns `None` for an atom this server never had, and
  both property readers check for it before calling Xlib.
- **Xlib state cannot outlive its connection.** Calling `XUnmapWindow` on a closed
  `Display*` crashes inside `_XSend`. Any caller that opens a display, creates
  windows and then closes the display in the same scope must let declaration
  order destroy the windows first; `upptst/WindowSourceCheck` wraps the
  connection in a `TestDisplay` whose destructor closes it, declared before the
  windows it backs.
- **A window that is not mapped is not a task.** `XQueryTree` lists every child
  of the root, mapped or not. With only `DesktopManager` running on Xvfb `:95`
  the tree walk found four windows where `xdotool search --onlyvisible` found
  three; the extra one was a 1x1 unmapped window with no `WM_CLASS`, no
  `WM_NAME` and no `_NET_WM_WINDOW_TYPE`, so none of the EWMH filters could catch
  it. `IsManaged()` now rejects anything whose `map_state` is not `IsViewable`.
  The independent tool is what made the difference visible — worth checking
  against `xdotool` rather than against the source's own output.
- **`focus` is a request, not a setting.** `XSetInputFocus` would override the
  window manager's own decision, so `Focus()` sends `_NET_ACTIVE_WINDOW` with
  source indication 2, which is what EWMH defines for a pager. On a bare server
  with no window manager the request goes nowhere and the call still reports
  success, because the send succeeded.

## Mainconfigs
```
bin/build -m CLANG -M X11 DesktopX11            # library: flagX11, no flagGUI
bin/build -m CLANG -M X11 WindowSourceCheck     # the window source
bin/build -m CLANG -M X11 ScreenSourceCheck     # the screen source
bin/build -m CLANG -M X11 FrameManagerCheck     # the frame manager, live
```

There is deliberately no empty mainconfig. `X11WindowSource.cpp` carries an
`#error` without `flagX11`, the same guard and the same reasoning as
`DisplayManager.cpp`: a build without the flag would otherwise compile to a
source that reports an empty desktop for a reason that has nothing to do with
there being no windows.

The check runs both with and without a display, and one command builds and runs
all six headless packages:

```
pysrc/bin/check_desktop_headless.py                 # headless half only
pysrc/bin/check_desktop_headless.py --display :95   # also the live half
```

The live half never runs without `--display` and never reads the environment's
`DISPLAY`: inheriting it is how the checker's first version ended up running the
checks against a real desktop. It also refuses a display with a window manager
on it, and `CheckEwmhOrder` refuses a display whose `_NET_CLIENT_LIST` is
already owned, because that check publishes the property on the root and a
second owner's list must not be overwritten. Both checks open their live
connections through `OpenLiveDisplay()`, which retries a transient refusal and
counts a leftover failure as a failure, not a skip, because with DISPLAY set a
skip is a lost check dressed up as a pass.

Directly:

```
env -u DISPLAY ./bin/CLANG/WindowSourceCheck   # 4 checks, the no-display paths
DISPLAY=:95    ./bin/CLANG/WindowSourceCheck   # 57 checks against a live server
./bin/CLANG/WindowSourceCheck list             # print what the source sees

env -u DISPLAY ./bin/CLANG/ScreenSourceCheck   # 3 checks, the no-display paths
DISPLAY=:95    ./bin/CLANG/ScreenSourceCheck   # 19 checks against a live server
```

`ScreenSourceCheck` is the check `X11ScreenSource` could not have while it lived
in a MAIN package. It covers a dead display, an unset `DISPLAY`, out-of-range and
negative indices, the DPI fallback when a server reports no millimetres, and 100
consecutive queries, which is what shows the per-query connection accumulating
nothing. Live numbers are cross-checked against `xdpyinfo`: Xvfb `:95` reports
1280x800 pixels at 325x203 millimetres and 100x100 dpi, and the source returns
the same.

`CheckWatch()` is the one check that waits, because an event arrives
asynchronously; it retries for up to a second and gives up rather than asserting
nothing.

`list` mode exists because the checks create their own windows, which proves the
source reads X11 rather than U++, but only a separate process can reproduce the
case `Ctrl::GetTopCtrls()` fails. Verified on Xvfb `:95` with `xmessage` as an
unrelated client: one window while one client ran, two while two ran, none after
they were killed, with `xdotool search` agreeing on the count at each step.

## Known Gaps
- **Excluding our own windows is the view's job, and both views now do it.**
  `DesktopManager::IsOwnWindow()` and `TaskBar::IsOwnWindow()` compare
  `TopWindow::GetWindow()` against their own windows and against
  `Ctrl::GetTopCtrls()`. The contract puts this filter view-side because a window
  carries no "who made me" and only the process knows what it made. The former
  remaining hole -- `TaskBar::CycleAllTopWindows()` with no taskbar instance
  live -- is closed since DM4-003: the no-taskbar fallback builds its own
  `X11WindowSource` and model from the server, so it cycles the same system-wide
  list rather than only this process's windows. The subtlety that remains is
  deliberate: that fallback does not exclude the caller's own windows, because
  the caller in that case is a process with no taskbar view to ask.
- **`_NET_WM_DESKTOP` is read but nothing filters on it.** `DesktopOf()` reports
  it; per-workspace window sets are not built yet.
- **The watch connection is drained by a timer, not by an event.** CtrlCore
  selects no root event mask and exposes no hook a foreign event could arrive
  through, so both views call `Poll()` on their existing tick: `TaskBar` from
  `TIMER_SYNC`, `DesktopManager` from `Paint()`. What DM4-004 removed is the
  *re-read* on every tick, not the tick. An idle tick now costs one non-blocking
  syscall where it used to open a connection, walk the tree and read five
  properties per window.
- **Handles for closed windows are released on the next `ReadWindows()`.** The
  intern table is compared against the raw enumeration rather than the filtered
  list, so a window that is still present but currently filtered out -- minimized,
  say -- keeps its handle instead of being dropped and re-appended, which would
  reorder the list. A table that only grew would be a slow leak that never shows
  up as a failing test.
- **Titles can lag by one query.** `uppsrc/CtrlCore/X11Top.cpp:94` writes the X
  title only when the window is already open, so a title set before `Open()`
  reaches the server after the first query can be made. `DesktopManager` was
  observed reading `Workspace 1` as `(untitled)` on the first sync and correctly
  on the second and third. The model refreshes titles every sync, so it settles;
  a caller that syncs once and never again will see the stale value.
- **`Xinerama` / `XRandR` geometry merging** is not done; the source reports the
  windows as the server lists them.
- **Titles are kept as bytes.** `_NET_WM_NAME` is `UTF8_STRING` and U++ `String`
  holds UTF-8, so no conversion is needed. A legacy Latin-1 `WM_NAME` is not
  valid UTF-8 and is kept anyway; a title with a replacement character is better
  than one mangled by a conversion nothing needs.

## The window manager role (DM6-002)

`X11WindowManager` is the third piece of this package, added in phase 0006. It is
not a source: it takes over the root window and answers client requests. The
contract it must follow is frozen in `ai/info/desktop-model-view-split.md` under
"The manager role":

- **Opt-in, never automatic.** Only a `DesktopManager` process started with the
  `WMANAGER` mainconfig flag constructs one. A process without the flag is
  exactly the desktop it was before.
- **Takeover copies `Screen.cc:239`.** A scoped error handler is installed, the
  redirect mask is selected on the root, and the handler is uninstalled. If
  `BadAccess` arrived during that window, another manager owns the root and this
  role **refuses**: the connection is closed and `Takeover()` returns false, and
  the process continues as a plain desktop. There is no replacement path -- the
  fork refuses the same way (`BScreen::BScreen` sets `running = false`).
- **Dedicated connection.** The redirect mask changes what a connection's queue
  receives, so the manager has its own connection for the life of the object.
  The views' watch connection is untouched and keeps behaving exactly as it did
  before this package existed.
- **The queue is serviced by the manager's own thread.** A first wiring drained
  `ProcessRequest()` from the application's paint, and it could not work: CtrlCore's
  `TopWindow::Open()` blocks in `XWindowEvent(VisibilityChangeMask)`
  (`uppsrc/CtrlCore/X11Top.cpp:328`) until a manager answers the map request, so the
  main thread is inside that wait and can never drain what would return it. The
  manager therefore runs `EventLoop()` on a thread of its own, blocked on
  `XNextEvent`, and that thread is what answers requests. Shutdown is a
  `ClientMessage` to an internal 1x1 wake window, because `XNextEvent` has no
  timeout. Two properties make the thread arrangement safe: CtrlCore calls
  `XInitThreads()` before opening anything (`uppsrc/CtrlCore/X11App.cpp:406`), and
  the worker touches only its own connection. `ProcessRequest()` remains for
  callers with no worker and refuses while the worker owns the queue, so nothing
  can race it for events.
- **The manager is not a source.** It does not enumerate windows and it does not
  intern handles. The views keep reading through `X11WindowSource` exactly as
  before; the manager's job is to be the authority the source reads from, and
  the two stay separate so a check package can still link the source alone.

## The frame manager (DM6-003)

`X11FrameManager` decorates a client by putting it inside a frame window. Contract,
from `ai/info/desktop-model-view-split.md`, "The manager role":

- **The frame is an X window.** `CreateFrame()` creates it as a child of the root
  sized `client + insets`, `XChangeSaveSet` inserts the client so the server
  reparents it back if this process dies, `XSetWindowBorderWidth(..., 0)` drops the
  client's own border, and `XReparentWindow` puts the client at
  `(insets.left, insets.top)` inside the frame. Both the client and the frame are
  mapped: the client's own map request was redirected to the manager, so it is
  still unmapped when the frame is built.
- **The arithmetic is model-side.** `FrameInsets` and `FrameGeometry` live in
  `uppsrc/DesktopModel/FrameGeometry.h`, so `FrameSizeForClient`,
  `ClientToFrame`, `FrameToClient`, `ClientRectInFrame` and `ClientSizeInFrame`
  are pure integer arithmetic with no Xlib, and `upptst/DesktopModelCheck` proves
  the round trips headless. The default insets are 4/4/24/4 (borders and a
  titlebar).
- **The frame carries the events, the client does not.** The frame's event mask is
  `SubstructureRedirect|SubstructureNotify|ButtonPress|ButtonRelease|ButtonMotion|
  EnterWindow|LeaveWindow|Exposure`. A client `ConfigureRequest` is answered by
  configuring its frame, not the client, so the client stays inside the frame's
  client area.
- **Exclusions.** `ShouldFrameWindow()` returns false for this process's own
  windows (ids registered by the view through `SetOwnWindows()`) and for
  `_NET_WM_WINDOW_TYPE_DOCK` clients. The desktop's own windows would otherwise be
  reparented into frames the desktop drew for itself.
- **Teardown.** `UnmapNotify` on a client unmaps its frame and keeps it, so a
  later `MapRequest` re-maps the same frame instead of building a second one.
  `DestroyNotify` on either the client or the frame removes the pair; the client
  is reparented back to the root first when it still exists.

### Verified live, both through the desktop and without it

The desktop itself ran the manager role on Xvfb `:95`: its own four windows
mapped bare (unframed, per the exclusion above), a foreign `xmessage` arrived as
the child of a frame-sized `client + insets` window positioned at `(4, 24)`
inside it, `xdotool windowunmap` left frame and client both `IsUnMapped`,
`windowmap` re-mapped the same frame, and `xdotool windowkill` removed both
while the desktop went on running. Terminating the manager reparented the
framed client back to the root, still mapped, which is the SaveSet doing its
job.

`upptst/FrameManagerCheck` is the same run without a GUI: a console harness
that takes over the root, frames every mapped client for a fixed time and
prints one line per event plus a summary, non-zero exit if it framed nothing.
It exists for the case where the desktop cannot run (a display the real process
must not attach to, or a CI without a GUI stack) and it isolates the frame
manager from everything else the desktop does.

```
Xvfb :95 -screen 0 1280x800x24 -nolisten tcp &
DISPLAY=:95 ./bin/CLANG/FrameManagerCheck &      # holds the root, frames clients
DISPLAY=:95 xmessage -name probe 300x150 "hi"    # -> MapRequest client=.. frame=..
```