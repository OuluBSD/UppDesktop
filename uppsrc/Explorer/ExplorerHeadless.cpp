#include "ExplorerLang.h"
#include "ExplorerModel.h"
#include "ExplorerPolicy.h"
#include "ExplorerBookmarks.h"
#include "ExplorerDemoVfs.h"

NAMESPACE_UPP

// The headless twin of the Explorer GUI.
//
// Same ExplorerModel, same ExplorerPolicy, same ExplorerLog as the window: every
// listing and every decision reported here is the one the GUI would reach. The
// only difference is that there is no confirmation channel, so requests that need
// the user resolve to DENY. That is deliberate -- an automated run must never be
// able to talk its way past an authorization prompt.
namespace ExplorerHeadless {

// Detaches every mount this run created, releasing the owned VFS objects. Without
// this the debug heap checker reports the singleton's contents as leaked at exit.
static void ReleaseMounts()
{
	MountManager& mm = MountManager::System();
	while(mm.GetMountCount() > 0)
		if(!mm.Unmount(mm.mounts[0].path.AsSysPath("/")))
			break;
}

// Right-pad or truncate-free left-pad for the fixed-width text columns.
// Characters, not bytes. String in this fork is a byte string and both
// GetCount() and GetLength() are the byte length, so there is no accessor for the
// character count and it has to be counted: a UTF-8 character is a lead byte --
// anything that is not 10xxxxxx -- followed by continuation bytes. Byte padding
// makes every Finnish label three characters too wide, because Finnish is full of
// two-byte letters, and the paths stop lining up. In a translated listing, column
// alignment is the only alignment there is.
static int Chars(const String& s)
{
	int n = 0;
	for(const char *p = ~s; *p; p++)
		if((static_cast<unsigned char>(*p) & 0xC0) != 0x80) n++;
	return n;
}

static String Pad(const String& s, int width)
{
	String r = s;
	while(Chars(r) < width) r << ' ';
	return r;
}

static void PrintMounts(ExplorerModel& model)
{
	Cout() << "Mounts (" << model.GetMounts().GetMountCount() << "):\n";
	for(MountPoint* mp : model.GetAllMounts()) {
		Cout() << "  " << mp->path.AsSysPath("/") << "\n";
		Cout() << "      path:      " << mp->path.AsSysPath("/") << "\n";
		Cout() << "      type:      " << (mp->type.IsEmpty() ? "-" : mp->type) << "\n";
		Cout() << "      caps:      " << mp->Caps().ToString() << "\n";
	}
}

// The twin of the sidebar's Places and Mount points lists. Without it the sidebar is
// the one part of the window no run can check: a row pointing at a directory that is
// not there only fails when a user double-clicks it.
static void PrintPlaces(ExplorerModel& model)
{
	Vector<ExplorerModel::Place> all = model.GetPlaces();
	int mounts = 0;
	for(const ExplorerModel::Place& p : all)
		if(p.kind == "mount") mounts++;

	Cout() << "Places:\n";
	for(const ExplorerModel::Place& p : all)
		if(p.kind != "mount")
			// 24, not 20: the column was sized for "Tiedostojärjestelmä" being an
			// English string, and a translated label that fills the column exactly
			// runs into the path with no gap, which reads as one field.
			Cout() << "  " << Pad(p.kind, 12) << Pad(p.label, 24) << p.path << "\n";

	Cout() << "\nMount points (" << mounts << "):\n";
	for(const ExplorerModel::Place& p : all)
		if(p.kind == "mount") {
			Cout() << "  " << Pad(p.label, 30) << p.path << "\n";
			// A drive the user cannot write to should say so here, not when a
			// create fails. Free space is reported the same way the status bar
			// reports it, including "unknown" for a filesystem that cannot say.
			VfsPath vp;
			vp.Set(p.path);
			int64 total, free_space;
			if(model.GetMounts().GetDiskSpace(vp, total, free_space))
				Cout() << "      free:   " << FormatSize(free_space)
				       << " of " << FormatSize(total) << "\n";
			else
				Cout() << "      free:   unknown\n";
		}
}

// The twin of the panel's icon mode: the arrangement a window of `panel_width`
// pixels would use, and the icon kind every row would get. Neither is checkable by
// looking at the window -- a screenshot cannot be read back as text, and KWin here
// refuses synthetic input -- so without this the icon view would be the one part of
// the listing with no automated check at all.
static void PrintView(ExplorerModel& model, int panel_width)
{
	ViewMode mode = model.GetViewMode();
	Cout() << "View: " << ViewModeName(mode) << "\n";
	if(mode == VIEW_LIST) {
		// The detailed view has a layout too -- which columns, how wide, and what
		// every row says in each -- and none of it is visible in a widget dump.
		// The window and the twin read the same column list, so "the Size column
		// shows a dash for a directory" is checkable without a GUI.
		Vector<ListColumn> cols = model.VisibleColumns();
		Cout() << "  columns:  " << cols.GetCount() << "\n";
		for(int i = 0; i < cols.GetCount(); i++)
			Cout() << "    " << i << " " << ListColumnTitle(cols[i])
			       << "  width " << ListColumnWidth(cols[i])
			       << "  sort " << (ListColumnSortKey(cols[i]) < 0
			                          ? String("no") : String(ExplorerModel::SortKeyName(
			                              (ExplorerModel::SortBy)ListColumnSortKey(cols[i]))))
			       << "\n";
		Cout() << "  rows:\n";
		for(int i = 0; i < model.GetCount(); i++) {
			Cout() << "    ";
			for(int c = 0; c < cols.GetCount(); c++) {
				if(c) Cout() << " | ";
				Cout() << ListColumnValue(cols[c], model.Entries()[i]);
			}
			Cout() << "\n";
		}
		return;
	}

	int zoom = model.GetViewZoom();
	Size cell = ViewCellSizeAt(mode, zoom);
	int columns = model.IconColumns(panel_width);
	Cout() << "  zoom:     " << zoom << " px\n"
	       << "  cell:     " << cell.cx << "x" << cell.cy << "\n"
	       << "  panel:    " << panel_width << " px\n"
	       << "  columns:  " << columns << "\n"
	       << "  rows:     " << (columns > 0 ? (model.GetCount() + columns - 1) / columns : 0) << "\n";
	for(int i = 0; i < model.GetCount(); i++) {
		const ExplorerEntry& e = model.Entries()[i];
		Cout() << "    [" << ViewRow(i, columns) << "," << ViewColumn(i, columns) << "] "
		       << Pad(IconKindName(EntryIconKind(e)), 12) << e.name << "\n";
	}
}

static void PrintEntries(ExplorerModel& model)
{
	Cout() << "Listing '" << model.GetPath() << "'\n";
	Cout() << "  mount:     " << (model.GetMount() ? model.GetMount()->path.AsSysPath("/") : String("-")) << "\n";
	Cout() << "  relative:  " << model.GetMountRelativePath() << "\n";
	if(model.GetShadowingMount())
		Cout() << "  shadowed by mount: " << model.GetShadowingMount()->path.AsSysPath("/") << "\n";
	// The sort state in the model's own wording, so this and the window's --dump-ui
	// line are the same string rather than two descriptions of the same order. A
	// listing whose header does not say which of the four sort settings are in force
	// cannot be compared against another one.
	Cout() << "  sort:      " << model.DescribeSort() << "\n";
	Cout() << "  status:    " << model.StatusText() << "\n\n";

	for(const ExplorerEntry& e : model.Entries()) {
		Cout() << "  ";
		if(e.IsDir())       Cout() << 'd';
		else if(e.IsMount()) Cout() << 'm';
		else                Cout() << '-';
		Cout() << ' ' << Pad(e.name, 30);
		if(!e.IsDir()) {
			Cout() << ' ' << Pad(FormatSize(e.size), 12);
			Cout() << ' ' << Pad(IsNull(e.modified) ? String("-") : Format(e.modified, true), 20);
		}
		if(!e.ext.IsEmpty())  Cout() << " ." << e.ext;
		if(e.hidden)          Cout() << "  (hidden)";
		Cout() << '\n';
	}
}

// --check-policy <action> <path> -- decide a mutation without performing it.
static int CheckPolicy(ExplorerPolicy& policy, const Vector<String>& args)
{
	if(args.GetCount() < 3) {
		Cerr() << "usage: --check-policy <create-dir|create-file|delete|rename|write|chmod|mount|unmount|execute> <path>\n";
		return 2;
	}
	String action = args[1];
	String path   = args[2];

	ExplorerPolicy::Action a;
	if(action == "create-dir")  a = ExplorerPolicy::CREATE_DIR;
	else if(action == "create-file") a = ExplorerPolicy::CREATE_FILE;
	else if(action == "delete")      a = ExplorerPolicy::DELETE;
	else if(action == "rename")      a = ExplorerPolicy::RENAME;
	else if(action == "write")       a = ExplorerPolicy::WRITE;
	else if(action == "chmod")       a = ExplorerPolicy::CHMOD;
	else if(action == "mount")       a = ExplorerPolicy::MOUNT;
	else if(action == "unmount")     a = ExplorerPolicy::UNMOUNT;
	else if(action == "execute")     a = ExplorerPolicy::EXECUTE;
	else {
		Cerr() << "unknown action '" << action << "'\n";
		return 2;
	}

	// A DELETE is destructive regardless of where it lands.
	ExplorerPolicy::Trust trust = a == ExplorerPolicy::DELETE
		? ExplorerPolicy::DESTRUCTIVE
		: policy.Classify(path);

	ExplorerPolicy::Request req(a, path, trust, "requested on the command line");
	String reason;
	ExplorerPolicy::Decision d = policy.Evaluate(req, reason);

	Cout() << "request:  " << req.ToString() << "\n";
	Cout() << "decision: " << (d == ExplorerPolicy::ALLOW ? "ALLOW"
	                         : d == ExplorerPolicy::CONFIRM ? "CONFIRM" : "DENY") << "\n";
	Cout() << "reason:   " << reason << "\n";
	Cout() << "authorize in a headless run: "
	       << (policy.Authorize(ExplorerPolicy::Request(a, path, trust), reason) ? "yes" : "no") << "\n";
	return d == ExplorerPolicy::ALLOW ? 0 : 1;
}

int Main(const Vector<String>& args)
{
	ExplorerLog::SetupFile();
	// The twin prints the same strings the window draws, through the same t_(), so
	// it can be given a language and the translation can be checked without a
	// window at all.
	//
	// What it does *not* do is follow the desktop's LANG the way the window does.
	// Here that would make a script's output depend on whose account ran it: the
	// column headings would come back in Finnish on this machine and in English
	// on the next one, and every comparison in a test suite would have to be
	// written twice. So the twin is English unless it is told otherwise, and says
	// which it is in its header.
	{
		if(!GetEnv("EXPLORER_LANG").IsEmpty())
			ExplorerSetLanguage(GetEnv("EXPLORER_LANG"));
		else
			for(int i = 0; i < args.GetCount() - 1; i++)
				if(args[i] == "--lang")
					ExplorerSetLanguage(args[i + 1]);
	}
	EDEBUG(ExplorerChannels::GUI, "headless explorer starting");

	// Mount the host filesystem exactly as the GUI does, so both read the same
	// namespace. MountManager is a process-wide singleton and may already be
	// populated by an embedding host, so only mount when there is no root.
	if(!MountManager::System().HasRoot()) {
		MountManager::System().MountOwned("/", new SystemFS, "sysfs");
		EINFO(ExplorerChannels::MOUNT, "mounted host filesystem at /");
	}

	ExplorerModel model;
	ExplorerPolicy& policy = GetExplorerPolicy();

	bool show_mounts = false, show_policy = false, show_log = false, show_places = false;
	bool show_view = false;
	int  view_width = 800;   //< a plausible panel width, so the arrangement is real
	int  view_zoom = 0;      //< 0: leave the model's own zoom alone
	String columns;          //< comma-separated column names; empty: the defaults
	ViewMode view_mode = VIEW_LIST;
	ExplorerModel::SortBy sort_by = ExplorerModel::NAME;
	ExplorerModel::SortDir sort_dir = ExplorerModel::ASCENDING;
	// The two sort toggles, defaulting to the model's own defaults. Both are flags
	// rather than options with values: one of them turns a default *off*, and an
	// option spelled "--folders-first=..." would have to invent a value nobody asks
	// for. Between them a script can express all four orderings the window offers.
	bool folders_first = true;
	bool ignore_case = false;
	String filter;
	bool show_hidden = false;
	String path;

	// One operation to run once the listing has been opened. Collected during
	// parsing so that a bad option is reported before anything is modified, and so
	// that the directory is opened exactly once regardless of how many operations
	// were asked for.
	struct Op : Moveable<Op> {
		String kind;   //< mkdir, touch, rename, rm, cp, mv
		String arg;    //< the entry name
		String arg2;   //< the new name (rename) or the destination directory (cp, mv)
		bool   recursive = false;
	};
	Vector<Op> ops;

	// Navigation to perform before anything else. Kept separate from the Op list
	// because navigation is not a mutation: it needs no authorization, and it
	// changes where the operations below will land.
	Vector<String> nav_ops;

	// Bookmark changes to apply. Separate from the Op list because these do not
	// touch the directory being listed -- they edit the user's stored places -- and
	// running them through the operation path would make them look like filesystem
	// mutations needing authorization.
	Vector<String> bookmark_add, bookmark_remove;
	bool list_bookmarks = false;

	// A name to describe with --props. Not an Op: it changes nothing, so it must not
	// be able to fall through to the "does this need authorization" path.
	String props_entry;

	// The overlay demonstration (phase-02 task 04). --demo-overlay mounts the
	// synthetic VFS before the directory is opened, so the whole run happens on
	// top of it; --demo-cycle leaves the host files alone until after the first
	// listing, and then mounts, re-reads and unmounts inside this one run, so
	// the three stages are one diffable artifact.
	String demo_overlay, demo_cycle;

	// How many arguments an option consumes after its own name. -1 marks an
	// unknown option, 0 a pure flag.
	//
	// This exists because a bare argument is the directory to list, and an
	// option's operands are *not*. Scanning for the path in a separate pass, the
	// way this used to, makes "--mkdir newdir" set the path to "newdir": the
	// operand looks exactly like the positional argument. One pass cannot get that
	// wrong, because the parser that consumed the operand knows it was an operand.
	auto arity = [](const String& a) -> int {
		if(a.StartsWith("--")) {
			// The four navigation verbs are recognised purely so that the arity table
			// stays the single source of truth for what consumes the next argument;
			// they are collected in the pass below.
		}
		if(a == "--sort" || a == "--filter" || a == "--rm"
		|| a == "--mkdir" || a == "--touch" || a == "--props"
		|| a == "--bookmark" || a == "--unbookmark") return 1;
		// --lang is consumed here and acted on before this point: it only decides
		// which strings t_() returns, and it is the window's option, not an
		// operation on the filesystem.
		if(a == "--view" || a == "--view-width" || a == "--zoom" || a == "--columns"
		|| a == "--lang" || a == "--demo-overlay" || a == "--demo-cycle") return 1;
		if(a == "--cp" || a == "--mv" || a == "--rename") return 2;
		if(a == "--mounts" || a == "--policy" || a == "--log" || a == "--all"
		|| a == "--desc" || a == "--recursive" || a == "--check-policy"
		|| a == "--confirm-yes" || a == "--bookmarks" || a == "--places"
		|| a == "--no-folders-first" || a == "--ignore-case") return 0;
		if(a == "--back" || a == "--forward" || a == "--up" || a == "--home"
		|| a == "--refresh") return 0;
		return -1;
	};

	// The path is the only bare argument; everything else is an option or an
	// option's operand.
	for(int i = 0; i < args.GetCount(); i++) {
		String a = args[i];
		if(a == "--check-policy") {
			Vector<String> rest;
			for(int j = i; j < args.GetCount(); j++) rest.Add(args[j]);
			int rc = CheckPolicy(policy, rest);
			ReleaseMounts();
			return rc;
		}
		int n = arity(a);
		if(n > 0) { i += n; continue; }   // its operands are consumed, not paths
		if(n == 0) continue;              // a pure flag
		// arity returns -1 for anything it does not know. That is only "unknown
		// option" when the argument actually looks like one; a bare argument is the
		// path, and must fall through rather than being skipped. Getting this
		// backwards silently sends the run to the current directory instead.
		if(a.StartsWith("--")) continue;
		path = a;
	}

	for(int i = 0; i < args.GetCount(); i++) {
		String a = args[i];
		if(!a.StartsWith("--"))
			continue;
		if(a == "--mounts")   show_mounts = true;
		else if(a == "--places")   show_places = true;
		else if(a == "--policy")   show_policy = true;
		else if(a == "--log")      show_log = true;
		else if(a == "--all")      show_hidden = true;
		else if(a == "--desc")     sort_dir = ExplorerModel::DESCENDING;
		else if(a == "--no-folders-first") folders_first = false;
		else if(a == "--ignore-case")     ignore_case = true;
		else if(a == "--sort") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--sort needs a key\n"; return 2; }
			String k = args[++i];
			if(k == "name") sort_by = ExplorerModel::NAME;
			else if(k == "ext") sort_by = ExplorerModel::EXTENSION;
			else if(k == "size") sort_by = ExplorerModel::SIZE;
			else if(k == "time") sort_by = ExplorerModel::TIME;
			else if(k == "type") sort_by = ExplorerModel::TYPE;
			else { Cerr() << "unknown sort key '" << k << "'\n"; return 2; }
		}
		else if(a == "--filter") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--filter needs a pattern\n"; return 2; }
			filter = args[++i];
		}
		else if(a == "--view") {
			show_view = true;
			if(i + 1 >= args.GetCount()) { Cerr() << "--view needs a mode\n"; return 2; }
			String k = args[++i];
			if(k == "list")            view_mode = VIEW_LIST;
			else if(k == "big" || k == "big-icons")   view_mode = VIEW_BIG_ICONS;
			else if(k == "small" || k == "small-icons") view_mode = VIEW_SMALL_ICONS;
			else { Cerr() << "unknown view mode '" << k << "'\n"; return 2; }
		}
		else if(a == "--columns") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--columns needs a list\n"; return 2; }
			columns = args[++i];
		}
		else if(a == "--zoom") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--zoom needs pixels\n"; return 2; }
			view_zoom = atoi(args[++i]);
			if(view_zoom < 1) { Cerr() << "--zoom must be positive\n"; return 2; }
		}
		else if(a == "--view-width") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--view-width needs pixels\n"; return 2; }
			view_width = atoi(args[++i]);
			if(view_width < 1) { Cerr() << "--view-width must be positive\n"; return 2; }
		}
		else if(a == "--back" || a == "--forward" || a == "--up"
		      || a == "--home" || a == "--refresh")
			nav_ops.Add(a.Mid(2));
		else if(a == "--bookmarks")  list_bookmarks = true;
		else if(a == "--bookmark") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--bookmark needs a path\n"; ReleaseMounts(); return 2; }
			bookmark_add.Add(args[++i]);
		}
		else if(a == "--unbookmark") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--unbookmark needs a path\n"; ReleaseMounts(); return 2; }
			bookmark_remove.Add(args[++i]);
		}
		else if(a == "--props") {
			if(i + 1 >= args.GetCount()) { Cerr() << "--props needs a name\n"; return 2; }
			props_entry = args[++i];
		}
		else if(a == "--demo-overlay") {
			if(i + 1 >= args.GetCount()) { Cerr() << a << " needs a path\n"; return 2; }
			// Plain arms: the conditional-as-lvalue form of this assignment
			// bound to a temporary in the GUI entry and both flags stayed
			// empty, silently. Same parse, same fork, two outcomes -- not a
			// construct to trust.
			demo_overlay = args[++i];
		}
		else if(a == "--demo-cycle") {
			if(i + 1 >= args.GetCount()) { Cerr() << a << " needs a path\n"; return 2; }
			demo_cycle = args[++i];
		}
		else if(a == "--confirm-yes") {
			// Installs a confirmation channel that accepts everything. Without it
			// every DESTRUCTIVE operation denies, which is the correct default but
			// makes delete unreachable from a script -- and an untestable path is
			// how "delete quietly does nothing" ships. This is the deliberate opt-in
			// that automation needs, and it is loud in the log because it removes the
			// one safeguard the headless run normally has.
			policy.confirm = [](const ExplorerPolicy::Request&, const String&) { return true; };
			EWARN(ExplorerChannels::POLICY,
			      "--confirm-yes: every authorization prompt will be auto-accepted");
		}
		else if(a == "--mkdir" || a == "--touch" || a == "--cp" || a == "--mv"
		      || a == "--rename" || a == "--rm") {
			// Arity differs per option: only the transfer and rename forms take a
			// second argument. Demanding two for everything would reject a perfectly
			// good "--mkdir name" as a missing argument.
			const bool wants_two = a == "--cp" || a == "--mv" || a == "--rename";
			int need = wants_two ? 2 : 1;
			if(i + need >= args.GetCount()) {
				Cerr() << a << " needs " << (wants_two ? "two arguments" : "an argument") << "\n";
				ReleaseMounts();
				return 2;
			}
			Op op;
			op.kind = a.Mid(2);
			op.arg  = args[++i];
			if(wants_two) op.arg2 = args[++i];
			ops.Add(op);
		}
		else if(a == "--lang") {
			// Already acted on above, before anything was printed. Listed here so
			// the dispatch chain does not reach the "unknown option" arm for an
			// option the arity table and the startup both know about.
		}
		else if(a == "--recursive") {
			// Applies to the preceding --rm. Refused on its own rather than
			// silently meaning nothing, since a recursive delete that quietly did
			// not recurse is worse than one that was rejected.
			if(!ops.GetCount() || ops[ops.GetCount() - 1].kind != "rm") {
				Cerr() << "--recursive only applies to a preceding --rm\n";
				return 2;
			}
			ops[ops.GetCount() - 1].recursive = true;
		}
		else {
			Cerr() << "unknown option '" << a << "'\n";
			ReleaseMounts();
			return 2;
		}
	}

	// The overlay demonstration, before anything is printed: the run happens
	// entirely on top of the overlay, which is what a user sees when they
	// browse into a mounted path -- and a --mounts that listed only the root
	// while the listing below used the overlay would be two answers in one
	// output. The mount target has to exist under the current mounts -- that
	// validation, and its message, are the demo's own.
	if(demo_overlay.GetCount()) {
		String error;
		if(!MountDemoOverlay(demo_overlay, error)) {
			Cerr() << "error: " << error << "\n";
			ReleaseMounts();
			return 2;
		}
	}

	Cout() << "Explorer (headless)\n";
	Cout() << "log file: " << ExplorerLog::FilePath() << "\n";
	// The language, so that a run is self-describing: a translated heading is
	// evidence that the table was loaded, and "English (default)" is evidence that
	// nothing was. Neither can be read off the output otherwise.
	{
		String lang = ExplorerGetLanguage();
		Cout() << "language:  " << (lang.IsEmpty() ? String("English (default)") : lang)
		       << "\n";
	}
	Cout() << "\n";

	if(show_policy) {
		Cout() << "Authorization policy:\n" << policy.Describe() << "\n\n";
	}
	model.SetViewMode(view_mode);
	if(show_mounts)
		PrintMounts(model);
	if(show_places)
		PrintPlaces(model);

	if(path.IsEmpty())
		path = GetCurrentDirectory();

	model.SetShowHidden(show_hidden);
	model.SetFilter(filter);
	// The toggles before SetSort, and SetPath last of all: each of these re-applies
	// sort and filter to the entries already read, so the order they arrive in is
	// invisible to the result -- but SetPath re-reads the directory afterwards, and
	// a flag applied after that would be applied to a listing nobody printed.
	model.SetDirsFirst(folders_first);
	model.SetIgnoreCase(ignore_case);
	model.SetSort(sort_by, sort_dir);
	// Before SetPath, like the mode: the listing is rendered from the model, and
	// a zoom applied afterwards is one navigation of cell geometry behind.
	if(view_zoom > 0)
		model.SetViewZoom(view_zoom);
	if(!columns.IsEmpty()) {
		// Named columns *replace* the set rather than adding to it, so "one
		// column" is expressible: turning Hidden on top of four defaults would
		// give five, and a script asking for a layout has to get the layout it
		// asked for. Every column is first cleared, then the named ones set;
		// SetColumnVisible refuses to hide the name column, so a request that
		// names nothing else still gets a usable listing.
		Vector<String> names = Split(columns, ',');
		Vector<ListColumn> wanted;
		for(const String& n : names) {
			int found = -1;
			// Case-insensitive: the headings are capitalised because they are
			// headings, and a script should not have to know that.
			for(int c = 0; c < LCOL_MAX; c++)
				if(n == ToLower(String(ListColumnTitle((ListColumn)c)))) found = c;
			if(found < 0) {
				Cerr() << "unknown column '" << n << "'\n";
				ReleaseMounts();
				return 2;
			}
			wanted.Add((ListColumn)found);
		}
		for(int c = 0; c < LCOL_MAX; c++)
			model.SetColumnVisible((ListColumn)c, false);
		for(ListColumn c : wanted)
			model.SetColumnVisible(c, true);
	}

	String error;
	if(!model.SetPath(path, error)) {
		Cerr() << "error: " << error << "\n";
		ReleaseMounts();
		return 1;
	}

	PrintEntries(model);

	// After the listing, not before: the icon mode prints an arrangement of the rows
	// that are actually there, and an empty arrangement verifies nothing.
	if(show_view)
		PrintView(model, view_width);

	// The overlay cycle: mount, re-read, unmount, re-read, all in this one run.
	// Three listings in one artifact, so the claim "the overlay replaces the
	// directory and unmounting gives the files back" can be diffed instead of
	// believed. The model's own mount line in each block is the evidence that
	// the deepest mount resolved differently in each stage.
	if(demo_cycle.GetCount()) {
		String error;
		if(!demo_overlay.IsEmpty()) {
			Cerr() << "error: --demo-cycle and --demo-overlay are two forms of the same demo; give one\n";
			ReleaseMounts();
			return 2;
		}
		Cout() << "\n== demo cycle, stage 2: mounting the demo overlay at '" << demo_cycle << "' ==\n";
		if(!MountDemoOverlay(demo_cycle, error)) {
			Cerr() << "error: " << error << "\n";
			ReleaseMounts();
			return 1;
		}
		if(!model.SetPath(demo_cycle, error)) {
			Cerr() << "error: " << error << "\n";
			ReleaseMounts();
			return 1;
		}
		PrintEntries(model);
		Cout() << "== demo cycle, stage 3: unmounted, the host files are back ==\n";
		if(!UnmountDemoOverlay(demo_cycle, error)) {
			Cerr() << "error: " << error << "\n";
			ReleaseMounts();
			return 1;
		}
		if(!model.SetPath(demo_cycle, error)) {
			Cerr() << "error: " << error << "\n";
			ReleaseMounts();
			return 1;
		}
		PrintEntries(model);
	}

	// Navigation, in the order given. Each verb reports where it ended up, and a verb
	// that cannot go anywhere is a failure -- which is what makes CanBack/CanForward
	// testable without a window, since the toolbar's enable state is the same
	// predicate.
	{
		int rc = 0;
		for(const String& verb : nav_ops) {
			String error;
			bool ok = true;
			if(verb == "back")          ok = model.Back(error);
			else if(verb == "forward")  ok = model.Forward(error);
			else if(verb == "up")       ok = model.Up(error);
			else if(verb == "home")     ok = model.SetPath(GetHomeDirectory(), error);
			else if(verb == "refresh")  ok = model.Refresh(error);
			Cout() << (ok ? "ok      " : "failed  ") << verb;
			if(ok) Cout() << " -> " << model.GetPath();
			else Cout() << "\n        " << error;
			Cout() << "\n";
			if(!ok) rc = 1;
		}
		if(nav_ops.GetCount()) {
			Cout() << "\n";
			PrintEntries(model);
		}
		if(rc) { ReleaseMounts(); return rc; }
	}

	// Bookmarks, then the changes to them. Reported before the listing so a script
	// reading the output sees what it asked for and what it got.
	{
		ExplorerBookmarks& bm = GetExplorerBookmarks();
		int rc = 0;
		for(const String& b : bookmark_add) {
			String error;
			if(bm.Add(b, error)) Cout() << "ok      bookmark " << b << "\n";
			else { Cout() << "failed  bookmark " << b << "\n        " << error << "\n"; rc = 1; }
		}
		for(const String& b : bookmark_remove) {
			String error;
			if(bm.Remove(b, error)) Cout() << "ok      unbookmark " << b << "\n";
			else { Cout() << "failed  unbookmark " << b << "\n        " << error << "\n"; rc = 1; }
		}
		if(list_bookmarks) {
			Cout() << "\nBookmarks (" << ExplorerBookmarks::FilePath() << "):\n";
			Cout() << bm.Describe();
		}
		if(rc) { ReleaseMounts(); return rc; }
	}

	// The headless twin of the properties dialog: the same EntryProperties() rows,
	// so every value the dialog can show is verifiable without a GUI.
	if(!props_entry.IsEmpty()) {
		String reason;
		bool can_modify = model.CanModify(reason);
		String mount = model.GetMount() ? model.GetMount()->path.AsSysPath("/")
		                                : String("none");
		Cout() << "\nProperties of '" << props_entry << "':\n";
		bool found = false;
		for(const ExplorerEntry& e : model.Entries())
			if(e.name == props_entry) {
				found = true;
				for(const ExplorerEntry::PropertyRow& row :
				    EntryProperties(e, model.GetPath(), mount, can_modify))
					Cout() << "  " << row.label << ": " << row.value << "\n";
				break;
			}
		if(!found) {
			Cerr() << "no entry named '" << props_entry << "' here\n";
			ReleaseMounts();
			return 1;
		}
	}

	// Operations run after the listing, so the printed rows show the result rather
	// than the state before it. The listing is re-read by each model operation, so
	// the printed summary below reflects whatever actually happened.
	int op_rc = 0;
	if(ops.GetCount()) {
		String reason;
		if(!model.CanModify(reason)) {
			Cout() << "\nCannot modify " << model.GetPath() << ": " << reason << "\n";
			op_rc = 1;
		}
		else {
			Cout() << "\n";
			for(const Op& op : ops) {
				String error;
				bool ok = false;
				if(op.kind == "mkdir")         ok = model.CreateDir(op.arg, error);
				else if(op.kind == "touch")     ok = model.CreateFile(op.arg, error);
				else if(op.kind == "rm") {
					Vector<String> names;
					names.Add(op.arg);
					ok = model.Delete(names, op.recursive, error);
				}
				else if(op.kind == "rename") {
					int row = -1;
					for(int i = 0; i < model.GetCount(); i++)
						if(model.Entries()[i].name == op.arg) { row = i; break; }
					if(row < 0) error = "no entry named '" + op.arg + "' here";
					else        ok = model.Rename(row, op.arg2, error);
				}
				else if(op.kind == "cp" || op.kind == "mv") {
					Vector<String> names;
					names.Add(op.arg);
					ok = op.kind == "cp" ? model.Copy(names, op.arg2, error)
					                     : model.Move(names, op.arg2, error);
				}
				Cout() << (ok ? "ok      " : "failed  ") << op.kind << " " << op.arg;
				if(!op.arg2.IsEmpty()) Cout() << " -> " << op.arg2;
				if(op.recursive) Cout() << " (recursive)";
				// The window's menu says "Delete permanently" and its prompt says the
				// delete cannot be undone. A script reading "ok rm x" learns nothing
				// about that, and a log line that hides it is the same lie in a
				// different place.
				if(op.kind == "rm") Cout() << " (permanent)";
				if(!ok) { Cout() << "\n        " << error; op_rc = 1; }
				Cout() << "\n";
			}
			PrintEntries(model);
		}
	}

	if(show_log) {
		const Vector<AppLogRecord>& records = ExplorerLog::Get().Records().GetRecords();
		Cout() << "\nLog records (" << records.GetCount() << "):\n";
		for(const AppLogRecord& r : records)
			Cout() << "  " << r.channel << ": " << r.message << "\n";
	}

	if(op_rc) return op_rc;

	Cout() << "\nHistory:";
	for(const String& h : model.GetHistory())
		Cout() << " " << h;
	Cout() << "\n";

	ReleaseMounts();
	return 0;
}

} // namespace ExplorerHeadless

END_UPP_NAMESPACE
