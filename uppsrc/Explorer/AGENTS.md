# Explorer

File manager over the process-wide `MountManager`. Two binaries come out of this
package from the same sources: a window and a headless twin.

## Layout

| File | Role |
|------|------|
| `ExplorerModel.h/.cpp` | Directory browsing state: mount resolution, listing, sort, filter, history. No GUI types. |
| `ExplorerPolicy.h/.cpp` | Fail-closed authorization service. The headless run has no confirmation channel, so anything needing the user resolves to DENY. |
| `ExplorerBookmarks.h/.cpp` | The user's bookmarked places, stored in `~/.config/u++/Explorer/bookmarks.txt`. App state, not directory state, so it is a service like the policy -- and like the policy it is GUI-free, so the window and the twin share one list. |
| `ExplorerLang.h/.cpp` | Registers `Explorer.t` with Core's translation tables. One `#define TFILE` + one `#include <Core/t.h>`; `t.h` has to be included exactly once in the package. |
| `ExplorerLanguage.cpp` | Which language is in force: `$EXPLORER_LANG`, then `--lang`, then `$LANG`, then English. Not inside `#ifdef flagGUI`, so the twin takes a language too. |
| `ExplorerLog.h/.cpp` | Structured records in an `AppLog`, mirrored to the rotating `~/.local/state/u++/log/Explorer.log`. Channels are centralised in `ExplorerChannels`. |
| `ExplorerLogWindow.h/.cpp` | In-app log viewer, compiled only when `flagDEBUG && flagGUI`. |
| `ExplorerIcons.h/.cpp` | Pictures for the icon view, drawn with `SImageDraw` and cached. The choice of icon per entry is the model's; this only draws. |
| `ExplorerSkin.h/.cpp` | The Windows-95/98 look: one nullary function installed with `Ctrl::SetSkin` before the window is built. |
| `BreadBar` (in `Explorer.h/.cpp`) | The location group's upper row: the path as one clickable button per component. |
| `Explorer.h/.cpp` | The window: composition and wiring only. Listing and authorization decisions belong to the two files above. |
| `ExplorerHeadless.cpp` | The twin. Same model, same policy, same log. |
| `main.cpp` | Both entry points, plus the shared startup that opens the log and mounts the host filesystem when nothing else has. |

Every GUI file is inside `#ifdef flagGUI`, so the headless build never pulls in
CtrlLib.

## Desktop services

Three model answers delegate to the process's desktop backend (`Core/Desktop`,
see `uppsrc/Core/Desktop/AGENTS.md`), each with its own fallback kept, so a
binary that linked no desktop services classifies and lists exactly as before:

- **Entry MIME** - `IDesktopBackend::DetectMime` reads the shared-mime-info
  tables (globs2 weights, aliases, subclasses, magic); the hand table
  (`HardcodedMimeFromExtension`) answers a no-backend process. Icon kinds
  bucket by MIME prefix, so sharper values land in the same kind.
- **XDG place directories** - `LoadUserDirs(roots, false)` returns the
  *declared* `user-dirs.dirs` directories (parsed by the desktop layer,
  `$HOME` expanded there). Passing the existence question to `false` is
  deliberate: a host-side check would answer the wrong filesystem for a VFS
  mount. The model applies the MountManager existence check, skips the
  home-pointing row, dedupes and labels (`t_()`), and preserves order.
- **The Trash place** - `IDesktopBackend::GetTrashInfo` names it; the model
  keeps the MountManager visibility check. The backend answers capabilities
  without creating the directories it is asked about.

Where a window-system answer is genuinely window-system state - the GTK icon
theme and its SVG rendering - `CtrlLib/FileSel` keeps that side native and
publishes the theme name via `SetPreferredIconTheme` instead.

## Mainconfigs

```
bin/build -m CLANG -M GUI Explorer            # window
bin/build -m CLANG -M HEADLESS Explorer        # headless twin
bin/build -m CLANG -M "GUI (Valgrind)" Explorer
bin/build -m CLANG -M "HEADLESS (Valgrind)" Explorer
```

**Pass `-M` explicitly.** Both configs produce the same binary,
`bin/CLANG/Explorer`, so the last build wins -- and after switching configs umk
often reports `OK` in a couple of seconds *without relinking*, leaving the other
config's binary in place. The symptom is running the headless twin when you meant
the window. Add `-c` when switching; a clean GUI build takes about 5 minutes
against a couple of seconds for the misleading "OK".

`--list-mainconfigs Explorer` shows the four.

The `USEMALLOC` variants are the ones that give a trustworthy leak report; see
`ai/rules/valgrind-leak-checking.md`. With U++'s own allocator the debug heap
checker reports freed blocks from the internal free list as leaks, which is
noise, not a defect. Verified: the headless twin under valgrind, with all six
operations in one run, is `2373 allocs / 2373 frees`, `0 errors`, `0 bytes in use
at exit`.

## Operations, and the headless twin for each

Every mutation lives in `ExplorerModel`, not in the window, and each one calls
`GetExplorerPolicy()` **before** touching the filesystem. That is what makes the
headless twin deny exactly what the GUI would have had to confirm, and it stops a
second caller from reaching the Vfs mutators without passing policy.

| Operation | Model | Headless |
|-----------|-------|----------|
| New folder | `CreateDir` | `--mkdir <name>` |
| New file | `CreateFile` | `--touch <name>` |
| Rename | `Rename(row, new)` | `--rename <old> <new>` |
| Delete | `Delete(names, recursive)` | `--rm <name> [--recursive]` |
| Copy | `Copy(names, dest)` | `--cp <name> <destdir>` |
| Move | `Move(names, dest)` | `--mv <name> <destdir>` |
| Properties | `EntryProperties` | `--props <name>` |
| Bookmarks | `ExplorerBookmarks` | `--bookmark <p>`, `--unbookmark <p>`, `--bookmarks` |
| Sidebar places | `GetPlaces()` | `--places` |
| Listing view mode | `SetViewMode()` | `--view list\|big\|small`, `--view-width <px>` |
| Navigate | `Back`/`Forward`/`Up` | `--back`, `--forward`, `--up`, `--home`, `--refresh` |

The navigation verbs are how `CanBack` / `CanForward` / `CanUp` are testable at
all: the toolbar's enable state is the same predicate, and a greyed button cannot
be distinguished from a black one in a screenshot. A verb that cannot go anywhere
**fails with a reason and a non-zero exit**, so `--back` with no history is
testable rather than a silent no-op.

Navigation is deliberately *not* an operation in the headless twin: it is not a
mutation, it needs no authorization, and it changes where the operations land, so
it runs first and is reported before them.

### Tabs

`File > New tab` / `Close tab` (`Ctrl+T` / `Ctrl+W`). The last remaining tab closes
the *window*: a file manager with no tab and no way out is a trap. `OpenPathAt()`
only refreshes the chrome for the active tab, so `NewTab` activates *after*
opening the path -- doing it in the other order renders the previous tab's model
into the new panel.

`File > Remove all tabs` closes every tab but the one being shown, iterating from
the **end** and never touching `keep` (`ActiveTab()`), because a forward loop
shifts the indices under itself and then either skips a tab or closes the one it
was told to keep. It is **enabled only when there is more than one tab**
(`GetTabCount() > 1`): with one tab left the verb is "close the window", which
`Close tab` already is, and a menu item that means two different things depending
on the count is a menu item nobody can predict. xfe has no tabs at all, so this
verb is ours; its dump line `remove all tabs: 3 tabs -> 1 tab, kept "..."` is the
evidence that the surviving tab is the one the user was looking at.

The properties rows live in the **model**, as `EntryProperties()`, and both the
dialog and `--props` read that one list. That is deliberate: it is the only way to
verify every field a properties panel can show without driving a GUI, and it makes
the dialog and the script unable to drift apart. A `VfsItem::type_str` is the file
*extension* for an ordinary entry and the mount type only for a mount point, so it
cannot be used to tell them apart -- reading it into `ExplorerEntry::mount_type`
made every `notes.txt` claim to be a mount point. The model asks the mount registry
instead: an entry named `N` in directory `D` is a mount point exactly when something
is mounted at `D/N`.

Rules that the operations rely on:

- **`RefreshEntries()` is not a re-read.** It only re-applies sort and filter to the
  entries already in memory, which is right for `SetSort` and `SetFilter` and wrong
  for everything else. An operation that changed the disk has to call
  `RereadQuiet()`, which goes back through `SetPath` (whose history guard keeps the
  same path from being pushed twice). Getting this wrong is subtle: the operation
  reports success, and the entry it just created is invisible to the *next* command,
  so a chained `--mkdir` then `--touch` fails on a file that demonstrably exists.
- Names are validated before authorization, and authorization happens for **all**
  selected entries *before* the first one is changed. Removing the first and then
  discovering the second is denied is how a half-applied delete happens.
- `IsValidName` rejects empty names, `.`, `..`, and anything with a separator. That
  containment guard is deliberate, and it also means the CLI cannot act on a nested
  entry: `--touch d/a` is refused. To reach into a subdirectory, pass it as the
  positional path (`Explorer $PWD/d --touch a`). Refusing loudly is the point --
  accepting `../../etc/passwd` would be a much worse surprise.
- `--confirm-yes` installs a confirmation channel that accepts everything. Without
  it every `DESTRUCTIVE` operation denies, which is the correct default but makes
  delete unreachable from a script -- and an untestable path is how "delete quietly
  does nothing" ships. It logs a warning, because it removes the one safeguard the
  headless run normally has.

## Selection and sorting

`ColumnList` has `SelectOne` but no whole-list verbs, so `ExplorerPanel` adds
`SelectAll`, `InvertSelection` and `DeselectAll`; `InvertSelection` snapshots the
state first because inverting row by row while writing can settle back where it
started. `DeselectAll` does **not** need the snapshot, and the reason is worth
keeping: `ColumnList::SelectOne()` writes one row's flag and nothing else, so a
plain loop cannot read the state it is writing. InvertSelection reads first
because its *answer* comes from the state it overwrites; DeselectAll's answer is
the constant `false`. (The old comment here claimed SelectOne moves the cursor;
it does not -- `ColumnList.cpp:323`.)

All three are on the **Edit** menu, next to Copy, Cut and Paste. They were in View,
which is where Select all and Invert selection already were; xfe puts them in Edit,
and the other two verbs in that menu had nowhere else to go. They moved together
rather than one at a time, and the ones that had a key kept it -- a shortcut that
stays with its item is the reason a user who learned Ctrl+A can still find it. A
verb left behind under a heading it no longer belongs to is the same defect as a
missing verb, one level down.

The window's `DeselectAll`/`SelectAll` act on the **active pane** only
(`ActivePanel()`), like every other listing command.

Sorting lives in the model and the View menu drives it, rather than relying on
`ArrayCtrl::ColumnSort`. `ArrayCtrl` would sort its own copy of the view, and the
listing is re-rendered from the model immediately afterwards, so the two orderings
would disagree. Re-picking the active sort key flips the direction, which is what a
second click on the same column header is expected to do.

Four settings make up the order, all per tab in the model: the key, the direction,
**folders first** and **ignore case**. The last two are checkable menu items
(`View > Sort by`), they were `dirs_first` hardcoded-on with no setter until now,
and `ignore_case` is new. `ExplorerModel::DescribeSort()` is the single wording of
all four: the window's `--dump-ui` prints it, the twin prints it in its listing
header, and neither formats its own.

Three decisions in the comparator, all of them things a reader would otherwise
have to re-derive:

- **Descending is the comparator with its arguments swapped**, not the negation of
  its answer. "a is not less than b" is *also* true of two entries that compare
  equal -- two files of one size, two names differing only in case -- so negating
  claims each precedes the other and `std::sort` is handed an ordering with no
  transitivity. The visible symptom is a descending listing in an arbitrary order:
  with 24 equal-size entries the old comparator reversed them, the swapped one
  leaves them alone.
- **Ignore case ends in a byte-wise tie-break.** Without it "Notes" and "notes"
  compare equal and their order is `std::sort`'s business, not the names' -- and
  `std::sort` is not stable, so a re-read could come back differently ordered for
  a reason no user can see.
- **Ignore case applies to the text keys only** (NAME, EXTENSION, TYPE). A size and
  a time have no case to ignore; applying it there would be a no-op pretending to
  be a setting.

Folders first defaults to **on** and ignore case to **off**, which is the
behaviour the listing had before either was reachable. Note the asymmetry: `NewTab`
inherits the view mode from the tab it was opened from, but a new tab starts with
the *default* sort flags. That is deliberate (a new tab is a fresh view) and
surprising enough to be written down rather than discovered.

`--ignore-case` and `--no-folders-first` open the window in a chosen sort state,
and the headless twin takes the same two flags. Between them a script expresses
all four orderings the menu offers, and the two binaries can be compared for the
same ones -- which is the whole point: a toggle that can only be checked in the
state the window happened to open in has an unverifiable other state.

## Two clipboards, and which one "Copy names" uses

| | `clip_names` (internal) | the system clipboard |
|---|---|---|
| holds | entry names plus the directory they came from | text, nothing else |
| filled by | Copy / Cut (`CopySelected`) | **Copy names** |
| emptied by | a successful move-paste | whatever the user pastes over it |
| read by | `PasteClipboard` | every other program |

`File > Copy names` writes the selected names to the **system** clipboard, one per
line, and touches neither `clip_names` nor the selection. That is the decision
worth recording, because the internal clipboard is the obvious one to reach for and
it would be a bug: `PasteClipboard()` reads `clip_names` as *entries to create or
move*, so a name list in it would make the next paste create files called
`notes.txt`, `cherry` and so on in whatever directory the paste happened to land in.

The platform clipboard is `uppsrc/CtrlCore/CtrlCore.h` (`WriteClipboardText`,
`ReadClipboardText`) and has a real backend per platform (`GtkClip.cpp`,
`X11Clip.cpp`, `Win32Clip.cpp`, `TuiClip.cpp`) -- not a package-local shim.

## Verifying the verbs a menu item is the only route to

A menu item is not evidence that anything works, and a widget dump cannot see one:
a submenu's items are built when the user pulls it. So `--dump-ui` prints

- the **menu surface**, walked with `GuiAutomationVisitor` over the live `MenuBar`
  -- every item with its path, its check state and whether it is enabled,
  including items two levels down in `View > Sort by`;
- **`selection verbs`**: the selected-row count, then Select all, then Deselect
  all, on whichever list the run is drawing. Run it with `--view list` and with
  `--view big` to cover `ExplorerColumns` *and* the icon grid: a Deselect all that
  worked in only one of them would look identical in the other.
- **`copy names`**: the system clipboard read back after the call, with the
  previous contents restored afterwards (a diagnostic run has no business leaving
  the user's clipboard holding a file name).
- **`remove all tabs`**: the tab count before and after, and which path survived.

The tab probe runs in the **constructor**, not in the dump, and its result is
carried out in `tab_probe`. Two reasons, both learned by crashing the dump:
opening a tab adds a control to the tab strip, which must not happen inside the
paint loop, and `RemoveAllTabs` keeps the *active* tab -- so the probe activates
the original tab (`panels.Set(0)`) before removing, or it measures the two tabs it
just opened rather than the one the run asked about.

## The menu bar

Six menus: **File, Edit, View, Go, Sidebar, Help**. (The sixth was "Left
Panel" until the vocabulary split of task 14: "panel" now means a main-area
listing pane only, the sidebar menu is "Sidebar".) Four of the decisions that
shaped them are worth writing down, because three of them are *not* "match xfe".

**Edit exists, and its first job was not parity.** Copy and Cut were toolbar
buttons and nothing else -- no menu entry, no key. `View > Toolbars` lets the
Tools group be switched off, so those two verbs disappeared for anyone who used
it, and for anyone driving the window from the keyboard. A verb that exists only
in a toolbar is a verb with one door. Paste is in the same state. The enable rules
are the toolbar's own, computed the same way in both places (`blocked` from
`ModifyBlockReason()`, `have_selection`, `!clip_names.IsEmpty()`), because a menu
that offers what the toolbar refuses is two answers to one question.

`Ctrl+C` / `Ctrl+X` / `Ctrl+V` are bound **both** on the menu items and in
`Explorer::Key`. A menu accelerator is only reached when the focused control
forwards the key it did not use to the menu bar -- `ColumnList` does
(`ColumnList.cpp:291`), a `TreeCtrl` does not -- so the window's own handler is
what makes them work with the focus anywhere. It cannot fire the verb twice: the
key stops at whichever of the two handles it first.

**No top-level Bookmarks menu, on purpose.** xfe has one. Here the *places panel*
is the bookmark browser and `Go > Bookmarks` carries the two commands (add the
current folder, remove the selected bookmark) next to the paths themselves. A
third list of the same bookmarks would be a third place for it to be wrong, and
the two that exist are fed by one `ExplorerBookmarks` service. A parity gap
recorded on purpose beats a second source of truth.

**No preferences dialog, on purpose.** Every value xfe's dialog holds is already a
direct item in View or File -- view mode, columns, zoom, sort, toolbars, hidden
files, and now folders-first and ignore-case. A dialog over those would be a
second way to reach them and a settings file to persist them, in a package whose
state is deliberately per tab in the model and nowhere else. If the split-pane
sync flag ever becomes a *setting* rather than a decision (task 09), that is the
first thing that genuinely wants a place to live -- and the place would still be
the model, not a file.

**Help > Keyboard shortcuts is generated from the menu, not written beside it.**
`Help > Contents` would have opened a manual, and there is no manual; a Help entry
that opens nothing is a defect. The window has two dozen shortcuts and no way to
discover any of them, so `ReadShortcuts()` walks the live `MenuBar` with
`GuiAutomationVisitor` and lists every item whose accelerator is non-zero. A
hand-written shortcut table is a second source of truth that starts lying the
first time an item gets a key and the table does not, and nothing would notice --
a shortcut nobody can check is exactly what a user cannot check either. This
needed one line in CtrlLib: `AutomationBar::Item::Key()` dropped the accelerator
on the floor, so `AutomationElement` had no way to carry it.

### Two defects the shortcut list found, both invisible before

- **Five dead accelerators.** `View > Sort by` bound `K_ALT | 1` .. `K_ALT | 5` --
  the *numbers*, not the characters. `GetKeyDesc()` (`CtrlCore/CtrlKbd.cpp:452`)
  only prints a key code literally when it is above 32, so the menu showed
  `Alt+0001` (hex) and no keypress could ever match `1` against `'1'`. They looked
  bound and were not. Every accelerator in the package is worth reading once
  through `GetKeyDesc`, which is what the list makes cheap.
- **Half a menu in English.** Reading the menu walk in `--dump-ui` -- which prints
  every item as the window renders it -- showed nineteen menu labels with no
  `Explorer.t` entry: New tab, Close tab, Copy to..., Exit, View, Ascending,
  Descending, Toolbars, History, About and the rest. In a Finnish window a menu is
  the first thing anyone reads, and one English word among translated ones reads
  as unfinished. All nineteen now have `fiFI` and `deDE`.

### Reading the menu as evidence

`--dump-ui`'s menu walk (`GuiAutomationVisitor` over the live `MenuBar`, submenus
filled as it goes) prints `6 menus, 82 items` and every item's path, check state
and enable state. It is the only view of a submenu's contents, because submenu
items are built when the user pulls the menu -- they are not in the widget tree
before that, so a missing item is invisible to everything else the dump prints.
It found the untranslated labels, and it is where the "Remove all tabs is disabled
with one tab" rule is checked.

A path-shaped item reads oddly in it (`Siirry/Kirjanmerkit//tmp/kilo/bm`): the
automation path joins with `/` and the item's own text is an absolute path. That is
`AutomationElement::path` doing its job, not a second bug.

## Keyboard and the location bar

Keyboard: `Enter` opens (handled by the list), plus `Delete`, `F2`, `F5`,
`Ctrl+A` and `Ctrl+I` on the window, so they work whichever panel has focus.
`Ctrl+L` is Go to folder; `Ctrl+D` bookmarks the current directory.

The location group is **one row** and it is CtrlLib's **`PathCtrl`** (moved out
of the `CtrlLib/Eon` subpackage for the task's main-header include rule): one
clickable button per path component, an edit toggle, and the editable field --
the old hand-built `BreadBar` + `AddressBar` pair, two classes and two rows,
replaced by the stock ctrl. Enter navigates, a component button navigates to
its own path, and anything that is not a usable path is reported and the row
put back rather than left half-typed looking like the current location.
`Go > Go to folder` (`Ctrl+L`) drops the caret in with the path selected, and
the field carries the layout id `location`, which is what the gui-user gate's
planner addresses it by. `SetPath` updates it *without* taking focus, so
navigating by double-click does not leave the user typing over a caret. The
filter field stays right-anchored in the same row (`RightPos`, not HSizePos
margins, which anchor by the left edge and overlap the path ctrl). The old
row's clear button is gone on purpose: leaving edit mode *is* the cancel, and
the history drop-list is gone with the old field (the history itself stays on
the model and in Back/Forward).

Two `PathCtrl` facts that cost debugging time:

- **A posted callback replaces the one before it under the same id.** The
  rebuild and the caret used the same id and each erased the other, so Ctrl+L
  left the row in button mode. One id per job.
- **A hidden ctrl that still has the caret is an input black hole.** Leaving
  edit mode while the field holds the caret ate every later key and click on
  the GTK backend; leaving edit mode now moves the caret to the ctrl before
  the field hides.

The upper row was the **breadcrumb**: one button per path component, so getting to
an ancestor is one click instead of that many presses of "up". It is *above* the
field rather than instead of it -- a breadcrumb can only offer the ancestors of
where you already are, so replacing the field with one makes an unknown path
unreachable.

Three things in that row cost a build each:

- **Buttons in a variable number go in `Vector<One<Pusher>>`.** A `Ctrl` is not
  `Moveable`, so `Vector<Button>` is a compile error (`is_upp_guest<Button>`
  static_assert), and a `One` needs `Create()` before use.
- **`LeftPos` + `RightPos` on one child does not mean "between them".** It gives a
  fixed width, second one wins: the path field was 160px in a 1000px window because
  of it. `HSizePos(left_margin, right_margin)` is the one that spans the row.
  Verified in the dump -- `Rect: [202, 2] - [682, 21] : (480, 19)`.
- **Children need layout metadata, not rects.** `FrameLayout` positions them with
  `LeftPos`/`TopPos` (what `BarPane::Repos` does for the same reason) and
  `LayoutBar()` is called from `FrameLayout`; a `SetRect` here is overwritten by the
  next `SyncLayout`. The frame also has no virtual `Layout(Rect&)`, so the override
  has to be on `FrameLayout`.

`SyncLocation(path)` is the only place that writes the location, and it writes
**both** rows. Two rows updated from two places are two rows that will eventually
disagree, and a breadcrumb saying `/home` under a field saying `/home/user` is a
window that cannot be trusted about either.

Splitting the path is done by hand, dropping empty components: the leading `/`
belongs to the root, a trailing one to nothing, and a `//` in the middle separates
nothing. Skipping that check gives a button with no name on it, and every path
after it one level too high -- which is what `Breadcrumbs: ... 1://etc/ssl` in the
dump was.

## Exit codes

`CONSOLE_APP_MAIN` expands to a **void** function whose caller does
`return GetExitCode()`. The headless `Main` returns a code, and it has to be handed
over with `SetExitCode(...)` -- a bare `return` is a compile error and *ignoring* the
call's result compiles fine and makes the twin exit 0 for a bad option, a missing
directory, a denied operation and a missing entry alike. Every failure looking like
success to a script is worse than no exit codes at all.

| Code | Meaning |
|------|---------|
| 0 | success |
| 1 | the operation ran and failed: denied, missing directory, missing entry |
| 2 | the command line was wrong: unknown option, missing operand |

## Fork-specific formatting

`FormatTime` is **not** strftime. The specifiers are `Y` `M` `D` `H` uppercase but
`m` and `s` lowercase, and a repeat count pads (`MM` gives a zero-padded month).
`"%Y-%m-%d %H:%M:%S"` prints as `%26-%11-%d %1:%10:%S` -- the percent signs are
swallowed by the printf-style formatter and the date is silently wrong. Use
`"YYYY-MM-DD HH:mm:ss"`, or `Null` for the default rendering. Check the rendering,
not just the build: a wrong format string is not a compile error here.

There is no byte formatter in Core either, so `FormatSize()` scales B/kB/MB/GB the
way a file manager does. It is declared in `ExplorerModel.h` and used by the model,
the window and the twin -- there were three copies, and a window and a script
describing the same free space in two different ways is exactly the drift worth
avoiding. Its one decimal is written out by hand: `Sprintf` follows the locale, so
`"%.1f"` printed `26,2 GB` in a Finnish window and `26.2 GB` in the twin.

## Disk space

`VFS::GetDiskSpace` / `SystemFS::GetDiskSpace` / `MountManager::GetDiskSpace` answer
for the device holding a path, via `statvfs` (POSIX) or `GetDiskFreeSpaceExW`
(Windows). `ExplorerModel::StatusText` appends it, so the window's status bar and
the headless twin's `status:` line carry the same figure.

Two decisions worth keeping:

- It is **not** gated on `MountCapabilities::MUTABLE`. A read-only mount still sits
  on a device of a known size; refusing to say so would be a worse answer than the
  truth.
- A filesystem that reports **zero total** -- procfs, sysfs, some network mounts --
  counts as *not knowing*, not as "0 B free". "0 B free of 0 B" reads as a full
  disk, which is the opposite of the truth. `VfsMountTest` covers the refusal
  message from a synthetic VFS.

### Option parsing is one pass, and it has to stay that way

A bare argument is the directory to list, and an option's operands are *not*. The
parser used to scan for the path in a separate pass, which made `--mkdir newdir`
set the path to `newdir` -- the operand looks exactly like the positional
argument. The failure is silent and lands the operation in the **current
directory** instead of the intended one; a `--mkdir` under `/etc` created a
directory in the repository before this was caught. The `arity()` lambda in
`Main()` is what makes the two distinguishable, and a bare argument must fall
through to `path` rather than being skipped as "unknown".

## Fork-specific GUI idioms

These differ from stock U++ and cost real debugging time. Check them before
suspecting the layout engine.

- `SetTitle("...")`, not `Title("...")` — `Title` is the getter here.
- `Bar::Add(text, callback)` — the two-argument, image-less form — is a
  **MenuBar-only** overload. On a `ToolBar` it returns `NilItem()` and the button
  is dropped with no error and no warning. Toolbar items must pass an image, and
  the argument order is `Add(text, image, callback)`; text comes first.
- A `ToolBar` defaults to `ToolButton::NOLABEL`, so buttons show as bare icons.
  `toolbar.ButtonKind(ToolButton::BOTTOMLABEL)` puts the text underneath.
- There is **no edit-ok-cancel dialog** in this fork's CtrlLib, and `Prompt()`
  renders rich text and returns a button index, so it cannot collect input. New
  folder, new file and rename need the small `NamePrompt` dialog in `Explorer.cpp`.
- `Button` has no label setter at all. `SetLabel(const char*)` is on `Pusher`
  (and `ButtonOption`), so the dialog's buttons are `Pusher`s.
- `TopWindow::Close()` takes **no** argument here. Record the answer in a member
  and close plainly.
- `Ctrl::SetFocus()`, not `Focus()`. `EditField` has `SelectAll()` and `SetText()`
  but no `SetLabel()`.
- **`Rect_(l, t, r, b)` takes edges, not sizes** (`uppsrc/Core/Gtypes.h`: the
  four-arg constructor is `Set(l, t, r, b)`). A width handed to it as the right
  edge gives a rect whose right is left of its own left the moment `left > 0`:
  the column headings drew only "Name" because column 0 (left 0) survived the
  coincidence and every later column was skipped by the `right <= left` clip.
  When a size is what you have, compute the edge (`left + w`) or use raw
  coordinates like `ExplorerRow::Paint` does.
- **A static array of `t_()` results freezes English.** `t_()` runs at
  static-init, before any language is set, so `static const char *titles[] =
  { t_("Name"), ... }` is English forever -- the heading strip and the twin's
  output stayed English in a Finnish window while everything built at call time
  translated. Store the *keys* and call `t_()` per call.
- **A `CtrlFrame` is not a child of the window.** `AddFrame(fr)` gives the frame a
  slot in the layout, but only `fr.FrameAdd(parent)` parents it. Derive from
  CtrlCore's `FrameCtrl<T>`, or override `FrameAdd`/`FrameRemove` yourself, as
  `AddressBar` does. Without it the frame's own rect stays 0x0 and everything inside
  it is laid out against a 0x0 parent: the address bar showed a path field with a
  real width and no height, and nothing crashed.
- **`ctrl <<= rect` is `SetData(Value)`, not `SetRect`.** `Ctrl` has no
  `operator<<=(const Rect&)`, so the `Value` overload wins and hands a rectangle to
  `EditField::SetData`, which asks for it as a `WString` and throws `ValueTypeError`.
  A compile-clean crash at start-up.
- **`SyncLayout` recomputes every child's rect from the parent's**, so a rect set by
  hand inside `FrameLayout` is overwritten on the next relayout. Position children
  with their own layout metadata (`HSizePos`, `SetPosX(PosRight(...))`) instead. A
  negative margin is not right-anchoring here: `HSizePos(-240, 0)` puts the *left*
  edge off the window. `PosRight(p, size)` is the right-anchored form.
- **`RowHeight()` must come from `Font::GetStdFontSize().cy`**, not
  `GetStdFont().GetHeight()`: the per-font info is not realized while a frame is
  first laid out, and the row comes out one pixel tall.
- On the GTK backend `Ctrl::Close()` does not end `Run()`: `IsWndOpen()` asks
  `gtk_widget_get_window()`, which keeps answering for a destroyed widget, so
  `EventLoopIteration` never sees the loop end. `Ctrl::EndSession()` is a no-op and
  `IsEndSession()` is always false. Anything that has to end the app by itself uses
  `_exit(0)` after flushing.
- **`String::Trim(int pos)` truncates the string at `pos`.** It is not whitespace
  trimming, and there is no no-arg overload, so `Trim(0)` silently produces the
  empty string. Use the free functions `TrimLeft(s)` and `TrimRight(s)` to strip
  whitespace. This is not a compile error and not obvious at the call site: it made
  the new-folder and rename dialogs impossible to accept and made Enter in the
  address bar always reset.
- `Bar::Item` has `Enable(bool)`, not `Disable()`. An informational row still needs
  a callback: `bar.Add(text, [] {}).Enable(false)`.
- `VSizePos(a, b)` takes **margins**, not positions. To pin a height use
  `TopPos(offset, height)`, or `TopPos(offset)` plus `HSizePos()` for the rest.
  A caption written as `VSizePos(2, ccy)` silently stretches to fill.
- A `TabCtrl` slave only gets a rect if it is passed already sized:
  `tabs.Add(panel.SizePos(), "Files")`. Without `SizePos()` the slave stays 0x0.
- `Splitter` divider positions live in a fixed **10000-unit** space across the
  splitter, not in pixels. `SetMinPixels(i, px)` is the pixel-valued minimum;
  a pixel divider has to be scaled by hand.
- A splitter cannot hold a divider position before it knows its own width —
  `Layout()` clamps it away. `Explorer::Paint` applies it on the first paint.
- `Sprintf` is printf-style here, so a `String` argument needs an explicit
  `(const char *)` cast or the build fails on `-Wnon-pod-varargs`.
- `Paint(Draw& w)`, one parameter, not `(Rect, const void*)`.
- There is no `Core/Timer.h` in this fork. For one-shot work after layout, do it
  in the first `Paint` and `Refresh()`.
- **`ImageBuffer::operator[](int)` returns a line (an `RGBA*`), not a pixel.**
  `ib[i] = c` assigns the pointer and the pixels keep whatever the buffer was
  cleared to. Use `Fill(ib, rect, color)` and then `RGBA *px = ~ib; px[i] = c;`.
- **`InfoCtrl::GetTabOffset(int t)` used to ignore `t`** — the loop body declared a
  `const Tab& t` that shadowed the parameter, so there was no early return and every
  index returned the last offset. Fixed in `uppsrc/CtrlLib/StatusBar.cpp`; a caller
  asking for each cell's geometry got the same number every time.
- **The hover record held a `Display` that a re-render frees** — segfault on the
  next mouse move. `DisplayPopup`'s record is global static with a *raw*
  `const Display*` in it; hovering a listing row arms it (`ColumnList::SyncInfo`
  calls `Set` unconditionally, the popup only *opens* if the display overflows the
  item), and every `Render` frees the row's display — `row_cells` / `cells` — while
  the list ctrl itself lives on. The next mouse event ran
  `display->GetStdSize(value)` in `DisplayPopup::Check` on freed memory: hooks run
  before the ctrl's own `MouseMove`, so no re-`Set` could save it. A dump run can
  never reproduce it, because a dump delivers no mouse events — it took real
  pointer input to find. `Cancel()` now forgets the whole record, unpainting
  first, which covers every `ColumnList` path that destroys items (`Clear` already
  routed through `CancelMode`), and `TreeCtrl::Clear` / `RemoveSubtree` cancel too
  — its per-node displays have the same lifetime. Reproduced with xdotool on
  Xvfb `:99` and once on the live `:10` (hover a row, re-render via the breadcrumb
  or F5, move 2 px) before the fix, and the same rounds after: detailed, icon and
  tree hovers all survive. The rule for new code: a ctrl that hands `Display*` to
  `ColumnList::Add` must keep those displays alive for as long as the items
  reference them, and must route any item destruction through something that
  cancels the hover record.
- On the GTK backend an X grab of a window returns **black for regions the app has
  not painted** (no backing store), and `import -window root` returns the desktop
  wallpaper because client windows are not composited into it. A black region is
  therefore not evidence of anything in either direction -- not that a control is
  missing, and not that it painted black. `xdotool getwindowgeometry` plus
  `import -window <id>` is the way to get pixels, and `DISPLAY=:10` needs
  `XAUTHORITY=$HOME/.Xauthority` or the window never opens *and the process still
  exits 0*.
- **The same lesson one layer up: relayouts posted, never inside a popup
  menu's event.** Every verb that re-lays window frames (`SetBarShown`, the
  "One row per group" option, the split's dismissal) runs from a popup menu or
  a checkable toolbar button; doing the `RefreshLayout()` inside that event
  silenced every later click exactly like the split teardown did. `RelayoutSoon()`
  is the one door: it posts the relayout with `this` as the callback id, so a
  window closed in between drops it.
- **The toolbar groups are items in one bar, not bars of their own.** Since the
  compact-mode change (task 12) there is a single `all_bars` ToolBar frame fed
  by `CombinedBars`: the shown groups in xfe's order, a separator between them
  in compact (the factory state -- the pane wraps items across rows itself,
  which is how a 1000px window holds 33 buttons in one 26px row and a 560px
  window wraps to two), and a `Bar::Break()` between them in the "One row per
  group" option. The per-group View > Toolbars toggles decide which group's
  items the fill puts in; a hidden group leaves no stub behind. The location
  group is still its own frame: it is two rows tall (breadcrumb + field) and
  cannot merge into a button row at all.
- **Removing a split subtree inside the event loop kills the window's input.**
  On the GTK backend, taking `panel_split` (the two-pane splitter) out of the
  window's ctrl tree while the loop runs -- synchronously, deleted, or even
  *between* events via a posted callback -- left the window drawing and
  answering to the pointer, and every later click went nowhere, with no error
  printed on any stream. The mechanism that works is the splitter's own
  **zoom**: `panel_split.Zoom(1)` hides the second pane and gives the tabbed
  listing the whole main area with `panel_split` exactly where it was;
  `NoZoom()` brings the split back. The second pane is therefore built once
  per window and reused, and its model (`split_model`) keeps its own history
  across off/on cycles -- which is also the honest semantics: a split that
  comes back remembers where it was.
- **A conditional used as an lvalue can bind to a temporary.**
  `(cond ? a : b) = value` with two `String` lvalues silently assigned to a
  temporary in the GUI's option parsing -- both flags stayed empty, nothing
  warned, and the identical construct in the headless twin *worked*. Plain
  `if/else` arms everywhere; do not reach for the ternary as an lvalue.
- **But a dark 2px rule is not always unpainted.** `ColumnList` installs
  `ViewFrame()` by default and draws a sunken 98 client edge around the listing
  -- the dark lines under the heading strip and under the last row are that
  border, painted on purpose. Before reading any dark line as the unpainted-black
  bug, compare against a screenshot from before the change.
- **"Flat" is a colour, not Null.** The skin's first toolbar attempt wrote
  `look = Null; arealook = Null` to mean flat face; `ChPaint` paints nothing for
  Null and the GTK backend showed the whole bar row black. A colour is a legal
  look (`StdChLookFn` draws it with one `DrawRect`), so flat face is
  `SColorFace()` in both `ToolBar::StyleDefault().look` and `.arealook`, and
  every bare `ParentCtrl` row (the two location rows, the columns widget) needs
  its own face-fill `Paint`, or the same black stripe comes back. The dump's
  `Skin:` probe prints `toolbar look` / `toolbar area` precisely so
  *installed* and *unpainted* are distinguishable strings.
- The window title comes from `SetTitle`; the images come from
  `#define IMAGECLASS ExplorerImg` plus `<Draw/iml_header.h>` in `Explorer.h`
  and `<Draw/iml_source.h>` in `main.cpp`.

## The GUI user gate (`--gui-toon`, `--gui-access-gate`, `--gui-render-gate`, `--gui-llm-gate`)

`gui-user-gate` (`ai/docs/gates/gui-user-gate.md`) is mandatory for every GUI
program, and Explorer passes it (2026-10-03): structural TOON over seven window
states, rendered checks on a real X server, and six planner goals at score 1.00
with the scripted planner. The implementation is `ExplorerGate.cpp`, copying the
shape of `uppsrc/Game/PokerPanel`. `--gui-render-gate` and `--gui-llm-gate` need
a display: run them on the throwaway Xvfb (below) or on `:10` with the user's
say-so.

- `--gui-toon` prints the live Access surface as TOON plus its state hash. The
  walk is `GuiAutomationVisitor` -- the same walker the dump's menu walk uses --
  so the menu arrives with full paths ("Näkymä/Järjestä/Koko"), check states and
  enable states, the toolbar buttons arrive by their **tips** (they are
  NOLABEL), the breadcrumb buttons by their text, the listing rows as
  `listing/Select "<name>"` + `listing/Open "<name>"`, the sidebar tree as
  per-node Open/Select, and the location field as `label: Sijainti` /
  `label: location` + `value: <path>`.
- `--gui-access-gate` walks the window through seven named states (default,
  sidebar without tree, split on, big icons, detailed, location group hidden,
  toolbar groups hidden) and asserts required/absent TOON lines in each. A
  failing state prints its TOON. Hidden groups must not advertise their
  buttons: `Explorer::Access` declares only what is shown, and the sidebar
  declares **by mode, not by `IsVisible()`** -- a splitter's relayout can leave
  a child's visible flag stale until the next paint, which once let the whole
  collapsed tree through.
- `--gui-render-gate` opens a real window and checks visibility inside the
  window, frame overlaps (menu vs listing, status vs listing, sidebar vs
  listing, nav/addr vs listing) and text/paper contrast (WCAG 4.5:1).
- `--gui-llm-gate --goal=<text>` drives the live state to the goal. Without
  `--planner-mock` it asks the openai-compatible endpoint from
  `~/.config/game_screen_parser/ai.json` (the shared config; this machine has
  none, so the endpoint path reports `llm-config-missing`). The mock is a
  scripted decision list and runs everywhere, CI included:

  ```json
  {"goal_marker": "listing/Select \"portti\"",   // present => reached
   "goal_absent":  "listing/Select \"vanha.txt\"", // absent  => reached (delete)
   "answers": ["portti", "yes"],                   // modal answers, in order
   "steps": [{"action": "Tiedosto/Uusi kansio"},   // named through the TOON
             {"set": {"location": "/tmp/kilo"}}]}  // field writes too
  ```

  A planner decision is applied by a **second walk in write mode** with the
  named path as target -- `AutomationVisitor` fires the matching callback
  there -- so the planner can only name lines the walk itself produced, and
  naming an action that does not exist is a hard FAIL.

Three things the gate cost, worth knowing before the next gate:

- **The gate's confirmation channel must be installed after the window is
  built.** The Explorer constructor's `WirePolicyConfirmation()` wires the GUI
  prompt channel; the gate installs its auto-accept channel (the same
  authorization the twin gets from `--confirm-yes`) *after* `OpenGateWindow`,
  or the constructor's overwrite brings a modal prompt back and the planner
  hangs inside it.
- **Modal dialogs take scripted answers.** A modal has no event loop the outer
  planner can drive, so `NamePrompt` and the delete prompts consult
  `ExplorerGate::TakeDialogAnswer`/`ScriptedYesNo` before running. With an
  empty queue -- every normal run -- they behave exactly as before.
- **A `.Check()` menu item is an option, not an action.** The automation path
  filled bars through `AutomationBar::AddItem`, which always emitted
  `AccessAction`; `AutomationBar::Item::Check/Radio` now set the `Option`
  semantic (CtrlLib), because the gate could otherwise not tell a state toggle
  from a command, nor unchecked from checked.

The gate runs on the fixture `/tmp/kilo/gatework` (a directory it may mutate),
and it writes one bookmark per bookmark-goal run -- remove it after
(`~/.config/u++/Explorer/bookmarks.txt`, one directory per line) or the next
run hangs in `PromptOK` over "already bookmarked".

## Delete is permanent, and says so everywhere

There is **no trash operation** in this layer: `ExplorerModel::Delete` reaches
`VFS::Remove`, and `SystemFS::Remove` deletes. No `trashinfo` is read or written,
nothing is copied to `~/.local/share/Trash/files`, and nothing can be restored --
in the window or in the twin. That is the state of the code, and the decision
recorded here is to **keep it and be truthful about it** rather than fake it.

What "truthful" means here, in all four places a user or a script can be told:

| where | what it says |
|---|---|
| `File > Delete permanently` | "permanently" is in the menu text, not only in a prompt |
| the confirmation | "This cannot be undone." (and the folders-inside question on top of it) |
| the toolbar button | same label and the same tip as the menu item |
| the twin's `--rm` | `ok      rm gone.txt (permanent)` |

A menu item called plain "Delete" next to a sidebar row called "Trash can" tells a
user the window has a trash; it does not. The word costs eight pixels.

**The Trash place stays in the sidebar anyway**, and that is a decision rather than
an oversight. `~/.local/share/Trash/files` exists on a lot of systems and may hold
files that *another* application trashed, or files a user put there by hand. The
window can browse that directory and copy out of it like any other, so listing it
is true; removing it would hide real content behind a promise the window cannot
keep. What the window must not do is offer a verb that implies the trash is its
own, which is why there is no "Restore" and no "Empty Trash".

**What this forfeits, on purpose:** xfe's Move to Trash, Restore From Trash,
Trash Size and Empty Trash, and the last two of its six metadata columns --
**Deletion Date** and **Original Path** only mean anything for an entry that has
been through a trash, so they are unimplementable until one exists. `notes/06`
therefore closes to four of six columns, and that is the honest number.

Implementing the FreeDesktop.org trash spec later is an *addition*, not a
correction, and its smallest honest version is freedesktop-only: macOS has
`~/.Trash` with a different layout and Windows has the Recycle Bin behind
`SHFileOperation`. A `trashinfo` reader that silently does nothing on the other two
platforms is worse than no trash, because the user believes the file is
recoverable. A per-platform trash is a project of its own, not a menu entry.

## Two panes, and what "synchronized" means

`ActiveModel()` / `ActivePanel()` resolve the pane a command acts on:
`pane == 1 && split_panel ? the split : the active tab`. Every command goes through
them, which is what lets two listings exist without any of them knowing about two.
A click in either pane sets `pane` (`WirePanel`), because a file manager that acts
on the left pane while the user is clicking in the right one deletes the wrong
thing.

**Reading (3), and why not (1).** "Synchronize panels" shares the *path* and
nothing else. A navigation in one pane shows the same directory in the other;
**Back, Forward and Up never do** — they walk one pane's own history and the other
pane stays where it is. Reading (1), one path *and* one history, was rejected: it
makes the split useless for comparing two directories, which is what a split pane
is for, and leaves "Back" ambiguous — back from where? The two models keep their
own `ExplorerModel` histories precisely so that this holds.

Every navigation goes through one funnel, `NavigateTo(target, path, follow)`:

- `target` is the pane to navigate, because the split pane's own model has to be
  navigable. Before this, `ActivateSelection()` computed the next path from the
  split pane and then opened it in the *tab*: the split was a second copy of the
  first listing that could not be moved anywhere.
- `follow` is false for Back/Forward/Up, in the **Go menu as well as the toolbar**.
  A user's Back is a question about where *they* were; an answer that moves the
  other pane too is a question about two places at once.
- `sync_panels` is a **window** member, not a model field, so it survives the split
  being switched off and on again. Turning it on aligns the other pane at once: a
  checkbox that changes nothing until the next navigation reads as a checkbox that
  does not work.

`SyncChrome(model)` holds the seven things one navigation updates (breadcrumb,
address bar, filter field, places panel, history list, tree, status cells) and is
called with **either** model's reference. Seven lines copied for the split pane is
how two sets of seven drift.

**Switch Panels (`Ctrl+Tab`) moves the focus, not just the target.** Making pane 1
the pane a command acts on while the keyboard focus is still in pane 0 is the
version of this bug that deletes the wrong thing: the status line would say one
directory and `Delete` would act on another. `ExplorerPanel::FocusList()` focuses
whichever list the panel is *drawing* — the icon list is filled in the detailed
mode too, so focusing "the icon list" unconditionally would put the caret in a
control the user cannot see.

Both verbs are in the **View** menu, because the split buttons are in the view
toolbar and View is the menu that already owns what that toolbar switches. Both
are disabled with one pane: there is nothing to synchronize with and nothing to
switch to.

The dump's `sync panels:` line is the evidence, because the property that matters
is invisible everywhere else — the widget tree shows one splitter, and a screenshot
of two panes on the same directory cannot tell synchronized from independent:

```
sync panels: sync on: split pane -> "/tmp/kilo/sortcheck/..", tab followed ->
"/tmp/kilo/sortcheck/.."; back in the split pane -> "/tmp/kilo/sortcheck",
tab stayed -> "/tmp/kilo/sortcheck/.."
```

The probe runs in the constructor (the split adds a second panel, which is not
something to do during the paint loop) and switches the split off again, so the
rest of the dump describes a one-pane window.

**One thing the probe showed and nothing fixed:** a path is stored as typed, not
canonicalized — `sortcheck/..` stays in the model, the listing resolves it, and the
breadcrumb shows the component. Normalizing would touch the history as well as the
display, so it is recorded rather than changed here.

## Help > Contents: the manual

`ShowHelp` opens one scrolled QTF page (a `RichTextView` + `SetQTF`, never HTML)
with the manual's paragraphs, each through `Explorer.t` in its own key so a
translation carries its own emphasis; F1 is the item's key and the generated
shortcut list carries the row. Three things the page cost:

- **QTF here is single-star bold and one `\n` per paragraph** (`ParseQtf.cpp`
  toggles `format.Bold()` on each `*`). A double star is two toggles around an
  empty span, and a double `\n` is an empty paragraph between -- markdown
  habits render as prose with the markup eaten.
- **The `.t` compiler's key extraction breaks on escaped quotes.** A `T_` key
  containing `\"` made the fiFI entry fail to match its own key, and the
  window rendered half English. The manual's keys use single quotes for
  quoted words.
- **TopWindow's Key is `Key(dword key, int count)` here** -- two parameters,
  and the override chains with `TopWindow::Key(key, cnt)`.

## The location rows: one per pane, inside the panel

The buttoned PathCtrl + filter row is `ExplorerPanel::location` -- every tab
and the split pane own theirs. A pane's row navigates and filters *that pane*;
the owner resolves which pane a panel is the way `WhenPicked` does, because a
panel must not know its tab index (tabs re-index when one closes). The row is
the fourth switchable group (`View > Toolbars > Sijainti`): the switch shows
and hides every pane's row. Two lessons the move cost:

- The toolbar layout (which groups, compact vs one-row-per-group) is the
  user's configuration: `toolbars.cfg` next to the bookmarks, loaded before
  the bars are first filled and saved on every change. The first panel is
  built before the load, so the ctor re-applies the loaded flag to it.
- The gate's state walks toggle the groups as part of what they measure;
  `ExplorerGate` snapshots the config at entry and `ExitGate` restores it --
  scaffolding must not become the user's configuration.

## Sidebar: the tree, places, mounts, bookmarks

One vertical splitter owns all three sidebar panes, top to bottom: the
**directory tree** (which follows the listing), then **Places** (File system,
Home, the XDG directories, Trash), then **Mounts** (every browsable mount with
its free space) and **Bookmarks**. The mode chooses which panes are visible:
`sidebar-tree-only` is the **factory default** -- the classic Windows Explorer
sidebar is the folders tree and nothing else, which is what the review called
"the default win98 explorer side-panel" -- and `Alt+6..0` walk through the five
modes (tree-only, one column, tree+one, two columns, tree+two), the digit row's
upper half, because the sort items own Alt+1..5. The divider positions are
remembered per mode, so coming back to a mode restores the shape it was left
in, and a splitter's layout re-shows every child it lays out, which is why the
mode is re-asserted in `ExplorerNav::Layout()` after the splitter has had its
say.

The rows are built by `ExplorerModel::GetPlaces()`, never in the window. Which
directories exist is a question about the *filesystem being browsed*, so existence
goes through `MountManager::DirectoryExists` rather than the host `DirectoryExists`
-- a mount that shadows `/home` is exactly the case where the two disagree. The
headless twin prints the same list with `--places`, mount by mount, with free space
and `unknown` for a filesystem that cannot report a device.

Two decisions in there are deliberate:

- `$HOME` and `$HOME/` are the same directory, and `user-dirs.dirs` commonly spells
  a variable as `"$HOME/"`. The trailing slash has to go *before* the "is this just
  home?" test, or `Documents` comes back as a second row pointing at home.
- Places that resolve to the same path are listed once. Three XDG variables aimed
  at one directory is a normal configuration, not three places to go.

**Mounts is its own tab, not a separator inside Places.** Drives listed among
folders read as debug output rather than as somewhere to go, which is how the mount
list looked when it shared the Places tab.

## The three view modes

`ViewMode` is `VIEW_LIST` (the detailed columns), `VIEW_BIG_ICONS` and
`VIEW_SMALL_ICONS`. It lives in `ExplorerModel`, per tab, next to the sort order,
and the twin reads it with `--view list|big|small --view-width <px>`, which prints
the cell size, the column count and every row's grid position. The window's column
count and the twin's have to agree for the same width -- that is the check.

A panel holds **both** lists: `FileList list` for the columns (it knows how to
rename in place), `ColumnList icons` in `RowMode` for the grid. Both are filled on
every render and one is shown, because a list left stale from the last time its mode
was active comes back showing a listing from two navigations ago.

Everything above the panel goes through `Count()`, `Cursor()`, `IsSelected()`,
`SelectOne()`, `SetCursor()` and `NameAt()`. A command that reaches for
`p.list.GetCursor()` silently acts on a list that is not even visible once icon
mode is switched on.

Three things that are not obvious and cost a crash each:

- `ColumnList` stores a **pointer** to a row's `Display`. `One<IconCell>`
  per cell, `Create()`d, with `pick()` into the vector; a temporary on the stack is
  a dangling pointer, and `cells.Add(c)` copies nothing because `One` is not
  copyable.
- `One<T>` is a pointer: `c.Create()` allocates, `c->x = ...` asserts on a default
  constructed one.
- `ColumnList::SetSb` divides by the column count in `RowMode`, so `Columns(0)` is a
  divide-by-zero crash. Set the panel's mode **before** filling the lists: the fill
  asks the panel which mode it is drawing in order to size the cells, and a panel
  still reporting the previous one computes zero columns.

## Verifying the window without a human

`--dump-ui` prints the widget tree, the sidebar rows, the listing rows and the
address bar's geometry after the first layout, and exits. The window accepts
`--view big|small|list` to open the first tab in a chosen mode, so the icon view can
be inspected the same way. What the dump can *never* check is input: for that, run
the window on a **throwaway Xvfb** and drive it with `xdotool` -- no window manager
is needed (use `windowfocus` to set the X input focus directly, since there is no
WM to activate the window), and the user's live session is never touched. The
Xvfb-only gotcha is that `xdotool windowactivate` fails without a WM, which is
harmless; what matters is that every mouse event is delivered.

```
/usr/bin/Xvfb :99 -screen 0 1280x900x24 -nolisten tcp &
DISPLAY=:99 marco --no-composite --sm-disable &      # a WM: without it GTK popups never paint
DISPLAY=:99 ./bin/CLANG/Explorer /tmp/kilo/sortcheck &
xdotool mousemove 330 295 && xdotool key F5     # hover a row, refresh, ...
```

The **window manager is not optional for menu work.** On a WM-less server the
main window paints and takes input, but a popped-up GTK menu is an
override-redirect window that stays **unpainted** -- `import -window <menu-id>`
produces an empty PNG, and driving it blind by counting arrow presses navigates
the listing instead (arrow keys went to the cursor, Enter opened a directory).
With marco running the menu paints, `xdotool windowactivate` works, and every
step can be read before it is clicked: OCR the open menu, locate the item,
click it. `tesseract` is installed and reads the app's rendered text reliably
from 3x-scaled captures (PSM 6); for word coordinates use `tsv` output and
divide the box values by the scale factor. A screenshot the model itself can
read is not needed -- pixel bands (`-colorspace gray -threshold` + dark
fraction) and OCR excerpts are the evidence.

`:10` is the user's *live* session (xrdp Xorg, MATE/marco): driving its pointer or
keyboard from a test hijacks the user's machine, so it is for observation and one
deliberate repro at most, and always with `XAUTHORITY=$HOME/.Xauthority` --
without it the window never opens *and the process still exits 0*.

It leaves through `_exit(0)` -- see the GTK `Close()` note in the GUI idioms.

It also prints the skin (`Skin:` -- the palette as numbers, `gui style`, and the
field edge / toolbar / splitter / status looks, because a skin that failed to install
is otherwise indistinguishable from one that installed and drew nothing: both are a
flat window), the status strip's cell rects and how many times its panel frame was
reached by the paint loop (`panels painted`), the breadcrumb row's segments and
rects, and the four toolbar groups' rects, which is how a row that is *shown*
but the wrong height was found:

```
Toolbars: [0, 20] - [1000, 46] : (1000, 26) nav, [0, 46] - [1000, 72] : (1000, 26) tool,
          [0, 72] - [1000, 98] : (1000, 26) view, [0, 98] - [1000, 121] : (1000, 23) location
  shown: nav tool view location
  items: 14/8/11, one row is 25px, window 1000x498
```

and, with `--lang`, the language and what `t_()` returns for three keys -- a
translation table that is loaded but not consulted looks exactly like one that is
missing, so the evidence has to be `t_()`'s own answer.

## Translation

`Explorer.t` holds the window's and the model's strings; both binaries read it, and
the twin's `--lang` makes a translation checkable without a window at all. See
`ai/skills/upp-translation.md` for the mechanism. The two things this package had
to learn:

- **`SetLanguage` needs the charset.** `SetLanguage(LNGFromText("fiFI"))` matches no
  entry in a UTF-8 table, so every string comes back as its key and the window looks
  like it has no translation at all. `SetLanguage(SetLNGCharset(lang, CHARSET_UTF8))`.
- **`LNGFromText` accepts any four letters.** `xxYY` is a language number, not a
  language; `GetAllLanguages()` is what tells them apart.
- **The twin is English unless told otherwise.** It follows `--lang` and
  `$EXPLORER_LANG` but not `$LANG`, because a script's output must not depend on
  whose account ran it. It prints `language:` in its header either way.

## Icons are drawn, not borrowed

`ExplorerIcons` draws a picture per `IconKind` at the exact size its cell uses,
with `SImageDraw`, and caches by kind and size. Drawing at the size that is used
matters: `CtrlImg`'s 16x16 glyphs scaled into a 64px cell are the grey, mushy look
that "borrowed icons" means. `Explorer.iml` still holds only the app icon.

Which icon an entry gets is `ExplorerModel::EntryIconKind()`, in the model, and the
twin prints it next to every row -- so "`.tar.gz` gets the archive icon" is a test,
not a thing to look at.

## Toolbar icons are borrowed, and their *sizes* were a layout bug

`Explorer.iml` still contains only the app icon, so every **toolbar** glyph is taken
from `CtrlLib`'s `CtrlImg` (`MkDir`, `new_doc`, `remove`, `copy`, `cut`, `write`,
`go_back`, `redo`, `Home`). The writer is `uppsrc/ide/IconDes/ImlFile.cpp` and the
ids are `IMAGE_ID(...)` in `Ctrl.iml`.

What is left is not only cosmetic. `CtrlImg` ids are **not all 16x16**, and a bar
row is as tall as its tallest button:

| id | size | | id | size |
|----|------|-|----|------|
| `copy`, `go_back`, `write` | 16x16 | | `Numbers1` | 40x6 |
| `write` (once) | 48x48 | | `Numbers2` | 60x8 |
| `SmallDot`, `BigDot` | 3x3 | | `plus`, `minus` | 8x8 |
| `treeplus` | 11x9 | | `Dir` | 13x10 |

So `CtrlImg::write()`'s 48x48 set the height of the whole navigation row, and the
window spent **80px of its height on three empty toolbar rows** -- invisible in the
code, obvious in `--dump-ui`, and invisible again in a screenshot nobody could take.
`Numbers1` and the two dots, chosen as "panel mode" glyphs, were a 40x6 strip and a
3x3 dot.

Three settings on each of the three button groups fix the size half of it:

```cpp
g.bar->ButtonKind(ToolButton::NOLABEL);   // untitled, as xfe's groups are
g.bar->MaxIconSize(Size(16, 16));         // one 48x48 icon cannot set the row height
g.bar->ButtonMinSize(Size(22, 22));       // every button the same square
```

and the view group picks from the ids that are actually 16x16 (`DirSmall`,
`menu_window`, `tab_wheel`, `Var`, `HorzSplit`, `HorzPos`, `menu_check`,
`FileSmall`, `sort`, `Plus`, `Minus`). Measure an id before using it:
`CtrlImg::X().GetSize()` in `--dump-ui` is cheaper than a guess.

## The look: one skin function, and what it had to add

`ExplorerSkin()` in `ExplorerSkin.cpp`, installed with `Ctrl::SetSkin(ExplorerSkin)`
from `ExplorerStartup::Prepare()`. Five things about it that are not obvious:

- **It is one function because `ReSkin()` runs one function.** `ChReset()` clears
  every registered style, the installed function runs, `ChFinish()` re-inits
  anything still marked uninitialised. A skin that were "call `ChClassicSkin()`, then
  patch a few styles" would have its patches wiped by the next display change, with
  nothing logged.
- **`ChClassicSkin()` already exists and is a Win95/98 skin**
  (`uppsrc/CtrlLib/Ch.cpp:139`, registered in `GetAllChSkins()` as "Classic"). It
  was never called by anything in this repository. It covers buttons, scrollbars,
  tabs, separators and menus; it does **not** touch `EditField`, `ToolBar`,
  `Splitter`, `TreeCtrl` or `StatusBar`, which is exactly what `ExplorerSkin`
  adds. There is no `.upt` theme file to load -- `Normal.upt` and `Complex.upt` are
  *new-application templates* for the IDE and nothing in `Ch.cpp` reads them.
- **The palette goes first.** `ChClassicSkin()` reads `SColorFace()` while it runs
  and bakes the button faces from it.
- **`SColorLtFace` and `SColorDkShadow` have to be written too.** They are declared
  `CH_COLOR(SColorLtFace, Blend(SColorFace, SColorLight))`, and that blend is
  evaluated **once at static-init time** against the stock palette. Nothing
  recomputes it. Write the palette without them and every light bevel is a mix of
  the new face and a blend of the old one.
- **There is no `Ch(ctrl)`, no `ChStyleOf`, no `SetChStyle`** in this fork. Style
  is reached as `Klass::StyleDefault().Write()`, installed into a control with
  `SetStyle()`, and a look is an untyped `Value` in one of four shapes -- see
  `uppsrc/Draw/Cham.cpp:339` for the dispatch.

Two of the five looks are **stripes**, not rings: the splitter divider and the top
line of the status strip. `ChBorder()` paints one ring around a rect, which puts a
line where 98 has none, so those are `Image` looks with a hot spot -- `ChLookFnImage`
nine-slices a 3x3 image into a stripe of any size.

### A `Value` look prints as nothing when it is a `ChBorder`

`EditField::StyleDefault().edge[0] << ...` prints as an empty string. So the probe
that `--dump-ui` prints describes composite looks itself (`DescribeLook`), because a
probe that cannot distinguish *installed* from *broken* is not evidence: "the field
has no edge" and "the field has an edge this printer cannot show" are the same
string.

### The status strip's panels are a frame, not a style

`StatusBar::Style` has one `Value look` for the whole strip, and a cell is not a
member of any `Style` struct. So the 98 panels around the cells are
`ExplorerStatusCells`, a `CtrlFrame` added at index **1** of the status bar --
index 1 because the size grip is already a frame and appending would draw a panel
border across it.

Its `FrameLayout` and `FrameAddSize` deliberately do nothing: a frame's view is what
is *left* of the rect after its `FrameLayout` runs, so a frame that insets nothing
draws over the whole strip without taking any of it away, and the grip behind it
still gets the view it would have got.

`FrameAdd`/`FrameRemove` are **not** overridden. `Ctrl::AddFrame()` already calls
`FrameAdd()`, so an override calling `parent.AddFrame(*this)` back is infinite
recursion. The `AddressBar` frames override it because they add themselves as a
*child Ctrl*, which is a different call.

`ExplorerModel::StatusCells()` returns the count text and the free-space text as two
strings and `StatusText()` is their join, so the twin's `status:` line and the
window's two panels are the same bytes by construction. The right-hand cell is the
size grip wider than its own text, because the grip sits in the corner of the last
panel; `UpdateStatus()` asks the grip rather than repeating its `DPI(14)`.

## The overlay demonstration: any VFS under any path, shown

Phase-01 built MountManager so that *any* VFS mounted under any path replaces
what the panels show there, and phase-02 task 04 proves it **through Explorer
itself**, with Explorer knowing nothing about the mounted thing. The vehicle is
a demo flag, not a production VFS: `DemoOverlayVfs`
(`ExplorerDemoVfs.{h,cpp}`) is a fixed seven-entry tree, read-only because
nothing ever opts into `MUTABLE`, and the flags mount it:

- `--demo-overlay <path>` -- mounts before the window is built (or before the
  twin opens its directory), so the whole run happens on top of the overlay:
  the listing, the tree, the breadcrumb, the status line, `--props`, and the
  twin's `--mounts`, which lists the demo mount deepest-first with
  `caps: filesystem [] demo overlay (read-only)`.
- `--demo-cycle <path>` -- one run, three stages: the baseline listing as it
  is, then mount + re-read, then unmount + re-read. The twin prints the three
  listings back to back; the window's `--dump-ui` runs the same cycle at the
  end of the dump. That one artifact is the whole claim, diffable instead of
  believed.

The demonstrations recorded 2026-10-04, all reproducible with the commands
below (`/tmp/kilo/gatework` is any real directory with a file or two in it):

```
./bin/CLANG/Explorer /tmp/kilo/gatework --demo-cycle /tmp/kilo/gatework   # twin, 3 stages
./bin/CLANG/Explorer /tmp/kilo/gatework --demo-overlay /tmp/kilo/gatework --props overlay.txt
./bin/CLANG/Explorer /tmp/kilo/gatework --demo-overlay /tmp/kilo/gatework --mkdir new --confirm-yes
DISPLAY=:99 ./bin/CLANG/Explorer /tmp/kilo/gatework --demo-overlay /tmp/kilo/gatework --dump-ui
```

- **The listing replaces**: host files in stage 1, `OverlayData`,
  `OverlayNotes`, `overlay.txt` in stage 2, host files back in stage 3 -- the
  model's own `mount:` line reads `/`, then `/tmp/kilo/gatework
  (demo-overlay)`, then `/` again.
- **What the overlay cannot answer is unknown, not zero**: the status line's
  free space is `unknown` (the demo VFS refuses `GetDiskSpace`, the default),
  sizes and times are `tuntematon`/`-` in the window.
- **Every mutation is refused by the layer that owns the capability**: with
  `--confirm-yes` so the *policy* is not the refuser, all six operations
  (mkdir, touch, rm, rename, cp, mv) fail with `Cannot modify <path>: the
  mount at '<path>' is read-only` and exit 1 -- the mount is named.
- **A mount target that does not exist is a typo**: `--demo-overlay
  /nope/nothere` prints `demo overlay: '/nope/nothere' does not exist under
  the current mounts` and exits 2, in both binaries. An overlay replaces a
  directory the user could browse; validation is the demo's own because
  `MountManager::Mount` deliberately accepts any path.
- **The window built on the overlay is chrome-honest too**: the sidebar tree
  collapses to what the overlay can answer (1 line instead of 229), the
  breadcrumb and tabs are normal, and the screenshot shows the overlay rows
  under a translated heading strip.

Two things the demo cost, both now idioms:

- **A conditional used as an lvalue bound to a temporary.**
  `(cond ? a : b) = value` with two `String` lvalues silently assigned to a
  temporary in the GUI entry point -- both demo flags stayed empty, the mount
  never happened, and nothing warned. The twin's identical construct *worked*,
  which is the worst part: the same parse, same fork, two outcomes. Both
  parses are plain `if/else` arms now.
- **`One<Derived>` is not a `One<Base>`**, and `MountOwned` takes the base
  `One` by value: `Detach()` the pointer and hand the ownership over in the
  same expression (`MountOwned(path, One<VFS>(owned.Detach()), type)`).

## Startup contract

`ExplorerStartup::Prepare()` in `main.cpp` runs in both binaries: it opens the
log and mounts a `SystemFS` at `/` **only if `MountManager` has no root**, so an
embedding host that already mounted things keeps its own namespace.

## Adding a listing feature

Put it in `ExplorerModel`, not in the panel. `ExplorerPanel::Render` only turns
model state into widgets, and the headless twin prints the same state, so a
feature that lives in the window is invisible to automation.
