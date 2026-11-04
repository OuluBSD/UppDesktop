# DisplayServer

A native, network-transparent display server (the "X11 server" of the
`CompositeEasingNetwork` initiative's `NetworkDisplay` track). It hosts real,
framed, movable/focusable/maximizable/minimizable windows via `Ctrl/Display`'s
`ScopeT`/`FrameT` window-manager templates, and composites them onto screen in
one of two rendering backends. Some windows are synthetic (built in for
demo/testing); others belong to real client processes that connect to it over
the network and have their drawing shown as a real window here.

This is a client/server pair — by default, `DisplayServer` starts with an empty
desktop (no synthetic windows), ready to host network clients. The 3 built-in
synthetic demo windows (`Alpha`/`Beta`/`Gamma`) can be enabled with the
`--demo-windows` flag if needed for standalone testing. To see a *network*
client's window, you need a second program connected to it. Two are already
built and verified against this exact server; see below.

## Building and running the server

Two mainconfig entries, same package:

```
cd /home/sblo/Dev/ai-upp
./bin/build --search-root "$(pwd)/uppsrc/DisplayServer" --add-root "$(pwd)/uppsrc/DisplayServer" --list-mainconfigs DisplayServer
#  [0] Software = GUI
#  [1] OpenGL   = GUI GL
./bin/build --search-root "$(pwd)/uppsrc/DisplayServer" --add-root "$(pwd)/uppsrc/DisplayServer" -M 0 -j6 DisplayServer
DISPLAY=:0 ./bin/DisplayServer --verbose
```

`--verbose` logs each composite pass and each connected client's draw activity
— the quickest way to confirm a client actually connected and is drawing
something, without needing to look at the screen.

`--demo-windows` creates the 3 built-in Alpha/Beta/Gamma synthetic demo windows
on startup (for standalone testing without a network client). The server starts
with an empty desktop by default.

The server listens on TCP port `47821` by default
(`Net/Protocol.h`'s `DEFAULT_DISPLAYSERVER_PORT`), overridable with `--port N`
on both the server and any client below.

## Trying it with a client

### Quickest: the built-in test client

No other package needed — sends a few distinctive shapes (a rect, an
ellipse, a line, some text) and prints back any input events the server
sends it (mouse/keyboard forwarded from that window).

```
cd /home/sblo/Dev/ai-upp
./bin/build --search-root "$(pwd)/uppsrc/DisplayServer" --add-root "$(pwd)/uppsrc/DisplayServer" -j6 TestClient
./bin/TestClient
```

A new framed window titled "TestClient" appears in the running `DisplayServer`
with those shapes drawn in it.

### A real application: `examples/UWord` with `GUI NETDPY`

This is the recommended way to see the server host something non-trivial —
`examples/UWord` is a full `RichEdit`-based word processor, built with the
`GUI NETDPY` mainconfig entry instead of its normal native-window one:

```
cd /home/sblo/Dev/ai-upp
./bin/build --list-mainconfigs UWord
#  ... plus  [4] GUI NETDPY = GUI NETDPY
./bin/build -M 4 -j6 UWord
DISPLAY=:0 ./bin/UWord
```

With `DisplayServer` already running, this `UWord` connects to it instead of
opening its own native window — its real menu bar, toolbar, ruler, and
document editing area appear as a framed window inside `DisplayServer`,
fully interactive (mouse and keyboard both round-trip over the network).

`UWord` also accepts `--host`/`--port` if you need to point it at a
`DisplayServer` on a different host/port than the default
(`127.0.0.1:47821`).

## What `GUI NETDPY` means for any app

`uppsrc/NetDpy` is a `VirtualGui`-derived backend, structurally the same
kind of thing `uppsrc/Turtle` is for browser clients. Any U++ app can opt in
by adding `uses(NETDPY) NetDpy;` to its `.upp` and a
`mainconfig "" = "GUI NETDPY";` entry (see `examples/UWord/UWord.upp` /
`UWord.cpp`'s `#if defined(flagNETDPY)` branch for the exact pattern —
apps built this way need their own `CONSOLE_APP_MAIN` that connects a
`NetDpyServer` and drives it via `RunNetDpyGui()`, since `GUI_APP_MAIN` is
never defined for this backend, the same way it isn't for `TURTLE`).

## Known limitations (as of `NetworkDisplay/0007`)

- Whole-window redraws are sent on most changes (no dirty-rect diffing),
  so draw traffic is heavier than it needs to be for small edits.
- No modifier-key (shift/ctrl/alt) state tracking in the input path yet.
- No invert-rect operation (used for some selection/caret rendering) —
  approximated or dropped rather than implemented.
- Client canvas size is fixed at connect time (`CMSG_HELLO`'s width/height);
  no live resize message yet.
- Shared-memory transport is a stub only (`SharedMemoryDisplayTransport` in
  `Net/Transport.h`) — not implemented, real transport is TCP-only for now.

See `NetworkDisplay/0004`-`0007` in the plan tree
(`CompositeEasingNetwork/NetworkDisplay/`) for the full design history,
wire protocol details, and verification records.
