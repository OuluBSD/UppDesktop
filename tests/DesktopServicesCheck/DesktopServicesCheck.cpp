// DesktopServicesCheck.cpp
//
// Headless fixture check for the Core/Desktop FreeDesktop backend, run with no
// display, no GUI flag and no event loop:
//
//	bin/build -m CLANG -M HEADLESS DesktopServicesCheck && ./bin/CLANG/DesktopServicesCheck
//
// The point of this test is that the whole FreeDesktop layer is driven through
// its *injectable* configuration: a synthetic root under the system temp
// directory holds a user-dirs.dirs, a MIME database (globs, globs2, aliases,
// subclasses, magic), an icon theme and a set of desktop entries. No assertion
// reads the host environment, so the same result holds on a desktop with XDG_*
// set and on a bare build container.
//
// It prints with printf rather than LOG on purpose: the backend must stay
// silent, and LOG does not reliably reach stdout in these builds.

#include <Core/Core.h>
#include <Core/Desktop/Desktop.h>
#include <Core/Desktop/Free/DesktopFree.h>

using namespace Upp;

namespace Upp { extern bool NoMemoryLeaksCheck; }

static int failures = 0;
static int checks = 0;

//! Report one check and return whether it passed, so a caller can stop a loop
//! on the first failure instead of running every step after it.
static bool Report(bool ok, const char* what, const String& detail = String())
{
	checks++;
	// State is printed only when a check fails: the point of the detail is to
	// explain the failure, and printing it on every line buries the report.
	if(ok)
		printf("%-5s %s\n", "ok", what);
	else {
		printf("%-5s %s\n     got: %s\n", "FAIL", what, detail.Begin());
		failures++;
	}
	return ok;
}

static String g_root;

static String FixtureFull(const String& rel)
{
	return NormalizePath(AppendFileName(g_root, rel));
}

//! Write one fixture file, creating its parent directories. Every write is
//! under g_root, so a rerun replaces the tree rather than depending on the
//! previous one.
static void WriteFixture(const String& rel, const String& content)
{
	String path = FixtureFull(rel);
	RealizeDirectory(GetFileFolder(path));
	SaveFile(path, content);
}

static void BuildFixture()
{
	if(DirectoryExists(g_root))
		DeleteFolderDeep(g_root);
	RealizeDirectory(g_root);

	// Two of the three user directories exist, one does not: the loader must
	// drop the missing one rather than invent it.
	WriteFixture("home/Desktop/.keep", "");
	WriteFixture("home/Documents/.keep", "");
	WriteFixture("home/.config/user-dirs.dirs",
		"# a comment line\n"
		"XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
		"XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"
		"XDG_DOWNLOAD_DIR=\"$HOME/Download\"\n");

	// MIME: a legacy glob, a weighted glob2, a case-insensitive glob2, an
	// alias chain, a subclass chain, and one magic rule.
	WriteFixture("data/mime/globs",
		"text/plain:*.txt\n"
		"image/png:*.png\n");
	WriteFixture("data/mime/globs2",
		"50:text/plain:*.log\n"
		"80:text/x-markdown:*.md\n"
		"50:application/x-upp:CASE*.UPP:ci\n");
	WriteFixture("data/mime/aliases",
		"text/x-script:application/x-shellscript\n");
	WriteFixture("data/mime/subclasses",
		"application/x-shellscript:application/x-executable\n"
		"application/x-executable:application/octet-stream\n");
	WriteFixture("data/mime/magic",
		"50:image/x-magic-fixture:4:HELLO\n");

	// A file whose content matches the magic rule above: four filler bytes so
	// "HELLO" sits at offset 4.
	WriteFixture("data/magic-sample.dat", "....HELLO and more");

	// Icon theme: one fixed 48x48 entry, and one icon file. The check only
	// resolves the path; the bytes need not be a real PNG.
	WriteFixture("data/icons/testtheme/index.theme",
		"[Icon Theme]\n"
		"Name=TestTheme\n"
		"Directories=48x48/apps\n"
		"\n"
		"[48x48/apps]\n"
		"Size=48\n"
		"Type=Fixed\n"
		"Context=Applications\n");
	WriteFixture("data/icons/testtheme/48x48/apps/desktop-fixture-icon.png", "not a real png");

	// A desktop entry and a mimeapps.list that names it as the default for a
	// fixture MIME type.
	WriteFixture("data/applications/test-app.desktop",
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=Test App\n"
		"GenericName=Test Application\n"
		"Comment=A fixture entry\n"
		"Icon=desktop-fixture-icon\n"
		"Exec=testapp %f\n"
		"MimeType=text/x-test;text/plain;\n"
		"Categories=Utility;\n"
		"OnlyShowIn=TestDE;\n");
	WriteFixture("data/applications/mimeapps.list",
		"[Default Applications]\n"
		"text/x-test=test-app.desktop;\n");
}

static void CheckXdgDirs()
{
	printf("\n-- XdgDirs: user-dirs.dirs and $HOME expansion\n");

	XdgRoots roots;
	roots.home = FixtureFull("home");
	roots.config_home = FixtureFull("home/.config");

	Vector<XdgUserDir> dirs = LoadUserDirs(roots);
	Report(dirs.GetCount() == 2, "two declared directories exist", Format("count %d", dirs.GetCount()));
	bool desktop_ok = false, documents_ok = false, download_absent = true;
	for(const XdgUserDir& d : dirs) {
		if(d.var == "XDG_DESKTOP_DIR") {
			desktop_ok = d.path == FixtureFull("home/Desktop") && d.label == "Desktop";
		}
		if(d.var == "XDG_DOCUMENTS_DIR") {
			documents_ok = d.path == FixtureFull("home/Documents") && d.label == "Documents";
		}
		if(d.var == "XDG_DOWNLOAD_DIR")
			download_absent = false;
	}
	Report(desktop_ok, "the desktop directory expands $HOME and keeps its label");
	Report(documents_ok, "the documents directory expands $HOME too");
	Report(download_absent, "a directory that does not exist is dropped, not invented");

	Report(NormalizePath(ResolveUserDir(roots, "XDG_DESKTOP_DIR", "Desktop")) == FixtureFull("home/Desktop"),
	       "ResolveUserDir returns the declared path");
	Report(NormalizePath(ResolveUserDir(roots, "XDG_MUSIC_DIR", "Music")) == FixtureFull("home/Music"),
	       "an undeclared variable falls back to $HOME/<fallback>");
}

#if defined(PLATFORM_POSIX) && !defined(PLATFORM_MACOS)

// -- Shell-folder chain ------------------------------------------------------
//
// GetShellFolder in Core now reads whichever desktop backend was linked into
// the process (Core/Desktop/Free here). These checks compare the production
// function against an exact port of the reader it used to contain, so the
// delegation is proven at the byte level instead of trusted. The port lives in
// the test, not in production, on purpose: once the comparison passes, the old
// reader is allowed to disappear from Core.

// The old reader, ported line by line from Core/App.cpp: it picks one file
// (config-home user-dirs.dirs, then config-dirs user-dirs.defaults, then
// config-dirs user-dirs.dirs), asks two keys of it, strips quotes
// independently, and expands $IDENTIFIER through the environment.
static String LegacyGetShellFolder(const char *local, const char *users)
{
	String xdgConfigHome = GetEnv("XDG_CONFIG_HOME");
	if(xdgConfigHome == "")
		xdgConfigHome = AppendFileName(GetHomeDirectory(), ".config");
	String xdgConfigDirs = GetEnv("XDG_CONFIG_DIRS");
	if(xdgConfigDirs == "")
		xdgConfigDirs = "/etc/xdg";

	VectorMap<String, String> kv;
	String file;
	if(FileExists(file = AppendFileName(xdgConfigHome, "user-dirs.dirs")))
		;
	else if(FileExists(file = AppendFileName(xdgConfigDirs, "user-dirs.defaults")))
		;
	else if(FileExists(file = AppendFileName(xdgConfigDirs, "user-dirs.dirs")))
		;
	else
		file.Clear();
	if(!file.IsEmpty()) {
		FileIn in(file);
		bool in_default_group = true;
		while(!in.IsEof()) {
			String ln = in.GetLine();
			const char *s = ln;
			if(*s == '[') {
				in_default_group = false;   // group keys are never in Get(key)
				continue;
			}
			String key;
			if(!in_default_group)   // keys under a [group] line live in that group
				continue;
			while(*s && *s != '=')
				key.Cat(*s++);
			if(*s == '=')
				s++;
			String value;
			while(*s)
				value.Cat(*s++);
			if(*key != '\0')   // for empty keys, the old loop yields eq==0 or no eq
				kv.GetAdd(TrimBoth(key)) = TrimBoth(value);
		}
	}

	auto read_key = [&](const char *folder) {
		String v = kv.Get(folder, Null);
		if(*v == '"')
			v = v.Mid(1);
		if(v.GetCount() && *v.Last() == '"')
			v.Trim(v.GetCount() - 1);
		String r;
		const char *s = v;
		while(*s) {
			if(*s == '$') {
				CParser p(s + 1);
				p.NoSkipSpaces();
				p.Spaces();
				if(p.IsId()) {
					r.Cat(GetEnv(p.ReadId()));
					s = p.GetPtr();
				}
				else {
					r.Cat('$');
					s++;
				}
			}
			else
				r.Cat(*s++);
		}
		return r;
	};

	String ret = read_key(local);
	if(ret == "" && *users != '\0')
		return read_key(users);
	return ret;
}

// Pin the environment to a synthetic case and restore what it replaced, so no
// later section sees the pinned values.
struct PinnedEnv {
	Vector<String> names;
	Vector<String> values;

	void Pin(const char* name, const String& value)
	{
		names.Add(name);
		values.Add(Upp::GetEnv(name));
		setenv(name, value, 1);
	}

	~PinnedEnv()
	{
		for(int i = 0; i < names.GetCount(); i++) {
			if(values[i].IsEmpty())
				unsetenv(names[i]);
			else
				setenv(names[i], values[i], 1);
		}
	}
};

// One fixture file relative to the case's own root.
struct ChainFile : Moveable<ChainFile> {
	String rel;
	const char* content;

	ChainFile() : content("") {}
	ChainFile(const String& r, const char* c) : rel(r), content(c) {}
};

// The Desktop wrapper the old reader fed: the direct lookup plus its own
// $HOME/Desktop fallback for an empty result.
static String LegacyGetDesktopFolder()
{
	String ret = LegacyGetShellFolder("XDG_DESKTOP_DIR", "DESKTOP");
	if(ret.IsEmpty())
		return AppendFileName(GetHomeDirectory(), "Desktop");
	return ret;
}

// Run one chain case: the fixture files are written under chain/<case>,
// the environment is pinned to the case's own config roots, and the delegating
// production wrapper is compared against the legacy wrapper port and against
// the value the old reader had to produce. `music` selects the Music wrapper,
// whose lookup is direct; `extra` optionally pins one more variable the file
// content references.
static void CheckShellFolderCase(const char* what, const String& case_name,
                                 const Vector<ChainFile>& files, bool music,
                                 const String& expected,
                                 const char* extra_name = nullptr,
                                 const char* extra_value = nullptr)
{
	String prefix = "chain/" + case_name;
	if(DirectoryExists(FixtureFull(prefix)))
		DeleteFolderDeep(FixtureFull(prefix));
	for(const ChainFile& f : files) {
		String path = FixtureFull(prefix + "/" + f.rel);
		RealizeDirectory(GetFileFolder(path));
		SaveFile(path, f.content);
	}

	PinnedEnv env;
	env.Pin("HOME", FixtureFull("chain/home"));
	env.Pin("XDG_CONFIG_HOME", FixtureFull(prefix + "/.config"));
	env.Pin("XDG_CONFIG_DIRS", FixtureFull(prefix + "/xdg"));
	if(extra_name)
		env.Pin(extra_name, extra_value);

	String via_backend = music ? Upp::GetMusicFolder() : Upp::GetDesktopFolder();
	String via_port = music ? LegacyGetShellFolder("XDG_MUSIC_DIR", "MUSIC")
	                        : LegacyGetDesktopFolder();
	Report(via_backend == expected && via_port == expected,
	       String("chain parity: ") + what,
	       "backend <" + via_backend + "> port <" + via_port + "> expected <" + expected + ">");
}

static void CheckShellFolderChain()
{
	printf("\n-- Shell-folder chain: GetShellFolder legacy parity\n");

	Vector<ChainFile> files;
	auto Add = [](Vector<ChainFile>& v, const char* rel, const char* content) {
		v.Add(ChainFile(String(rel), content));
	};

	// 1. The common case: user-dirs.dirs declares desktop and documents.
	files.Clear();
	Add(files, ".config/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
		"XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n");
	CheckShellFolderCase("a declared directory expands $HOME", "declared", files, false,
		FixtureFull("chain/home/Desktop"));
	CheckShellFolderCase("an absent key falls through the empty second key", "declared", files, true,
		String(""));

	// 2. A system that only ships user-dirs.defaults: the short key answers.
	files.Clear();
	Add(files, "xdg/user-dirs.defaults",
		"DESKTOP=\"$HOME/Dft\"\n");
	CheckShellFolderCase("user-dirs.defaults answers the short fallback key", "defaults", files, false,
		FixtureFull("chain/home/Dft"));

	// 3. The third chain link: config-dirs user-dirs.dirs.
	files.Clear();
	Add(files, "xdg/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"$HOME/Xdgd\"\n");
	CheckShellFolderCase("config-dirs user-dirs.dirs is the last chain file", "sysdirs", files, false,
		FixtureFull("chain/home/Xdgd"));

	// 4. No chain file anywhere: both readers agree on no directory.
	files.Clear();
	CheckShellFolderCase("a missing chain hits the $HOME/Desktop wrapper fallback", "empty", files, false,
		AppendFileName(GetHomeDirectory(), "Desktop"));

	// 5. Duplicate keys: the legacy reader is last-wins.
	files.Clear();
	Add(files, ".config/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"$HOME/First\"\n"
		"XDG_DESKTOP_DIR=\"$HOME/Second\"\n");
	CheckShellFolderCase("a repeated key keeps its last occurrence", "dupes", files, false,
		FixtureFull("chain/home/Second"));

	// 6. ${HOME} is NOT a sourced-file expansion and stays literal.
	files.Clear();
	Add(files, ".config/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"${HOME}/Lit\"\n");
	CheckShellFolderCase("the brace form of $HOME stays literal", "brace", files, false,
		String("${HOME}/Lit"));

	// 7. A [group] header hides every key below it from the default group.
	files.Clear();
	Add(files, ".config/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"$HOME/Top\"\n"
		"[Group]\n"
		"XDG_MUSIC_DIR=\"$HOME/Mus\"\n");
	CheckShellFolderCase("a group header hides later keys", "grouped", files, false,
		FixtureFull("chain/home/Top"));
	CheckShellFolderCase("keys below a group header are invisible", "grouped", files, true,
		String(""));

	// 8. An unbalanced leading quote is stripped like the old reader did.
	files.Clear();
	Add(files, ".config/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"$HOME/Lead\n");
	CheckShellFolderCase("an unbalanced quote still yields the path", "leadquote", files, false,
		FixtureFull("chain/home/Lead"));

	// 9. $IDENTIFIER expands through the environment; an unset identifier
	//    disappears instead of staying a literal dollar.
	files.Clear();
	Add(files, ".config/user-dirs.dirs",
		"XDG_DESKTOP_DIR=\"$HOME/$CHAINVAR/sub\"\n");
	CheckShellFolderCase("a set environment variable expands into the path", "envvar", files, false,
		FixtureFull("chain/home/Hello/sub"),
		"CHAINVAR", "Hello");
	files[0].content = "XDG_DESKTOP_DIR=\"$HOME/$UNSETCHAINVAR/lost\"\n";
	CheckShellFolderCase("an unset environment variable disappears", "envvar", files, false,
		FixtureFull("chain/home") + "//lost");
}

#endif   // PLATFORM_POSIX

static void CheckHostBackend()
{
	printf("\n-- Host backend: actuator + Status\n");

	// A backend registered only through static-init would be dropped by
	// archive linking: prove the actuator gives callers a live backend even
	// in a plain binary.
	RegisterFreeDesktopBackend();
	const IDesktopBackend *b = GetDesktopBackend();
	if(!Report(b != nullptr, "RegisterFreeDesktopBackend yields a backend"))
		return;
	Report(String("FreeDesktop") == b->Name(), "the backend is the FreeDesktop one", b->Name());

	// One hostenticated lookup: the XDG chains on this host pass through the
	// backend without reading anything beyond the standard files.
	DesktopStatus status;
	b->GetUserDirectory("XDG_DESKTOP_DIR", "DESKTOP", status);
	Report(status == DESKTOP_OK || status == DESKTOP_NOT_FOUND,
	       "GetUserDirectory answers OK or NOT_FOUND", Format("status %d", (int)status));
}

static void CheckMime()
{
	printf("\n-- XdgMime: globs, globs2, aliases, subclasses, magic\n");

	MimeLookupConfig cfg;
	cfg.mime_roots.Add(FixtureFull("data/mime"));
	cfg.use_magic = true;

	Vector<MimeGlob> globs;
	Vector<MimeGlob2> globs2;
	Vector<MimeAlias> aliases;
	Vector<MimeSubclass> subclasses;
	Vector<MimeMagic> magic;
	Vector<MimeInfo> info;
	LoadMimeDatabase(cfg, globs, globs2, aliases, subclasses, magic, info);

	Report(globs.GetCount() == 2, "two legacy globs loaded", Format("count %d", globs.GetCount()));
	Report(globs2.GetCount() == 3, "three globs2 rows loaded", Format("count %d", globs2.GetCount()));
	Report(aliases.GetCount() == 1 && subclasses.GetCount() == 2,
	       "aliases and subclasses loaded",
	       Format("aliases %d, subclasses %d", aliases.GetCount(), subclasses.GetCount()));
	Report(magic.GetCount() == 1, "one magic rule loaded", Format("count %d", magic.GetCount()));

	MimeDetectResult by_glob = DetectMimeFromName(cfg, globs, globs2, aliases, "note.txt");
	Report(by_glob.found && by_glob.mime == "text/plain" && by_glob.source == "glob",
	       "a legacy glob resolves the MIME type", by_glob.mime);

	MimeDetectResult by_glob2 = DetectMimeFromName(cfg, globs, globs2, aliases, "readme.md");
	Report(by_glob2.found && by_glob2.mime == "text/x-markdown" && by_glob2.source == "glob2",
	       "a globs2 row resolves by weight", by_glob2.mime);

	MimeDetectResult ci = DetectMimeFromName(cfg, globs, globs2, aliases, "case5.upp");
	Report(ci.found && ci.mime == "application/x-upp",
	       "a case-insensitive globs2 row matches regardless of case", ci.mime);

	MimeDetectResult miss = DetectMimeFromName(cfg, globs, globs2, aliases, "mystery.zzz");
	Report(!miss.found, "an unknown extension has no glob answer");

	Report(ResolveMimeAlias(aliases, "text/x-script") == "application/x-shellscript",
	       "an alias resolves to its canonical MIME type");
	Report(IsMimeSubclass(subclasses, "application/x-shellscript", "application/octet-stream"),
	       "subclassing is transitive");
	Report(!IsMimeSubclass(subclasses, "application/octet-stream", "application/x-shellscript"),
	       "subclassing is not symmetric");

	// Full detection: the name has no glob, so magic decides.
	String sample = FixtureFull("data/magic-sample.dat");
	MimeDetectResult sniffed = DetectMime(cfg, globs, globs2, aliases, magic, sample, "");
	Report(sniffed.found && sniffed.mime == "image/x-magic-fixture" && sniffed.source == "magic",
	       "content sniffing resolves a file the globs cannot", sniffed.mime);

	// A file that matches neither a glob nor magic falls back to the default.
	String unknown = FixtureFull("data/unknown.zzz");
	SaveFile(unknown, "no known signature here");
	MimeDetectResult fallback = DetectMime(cfg, globs, globs2, aliases, magic, unknown, "");
	Report(fallback.mime == "application/octet-stream" && fallback.source == "default",
	       "a file with no match falls back to application/octet-stream", fallback.mime);
}

static void CheckDesktopEntry()
{
	printf("\n-- XdgDesktopEntry: parsing, visibility, mimeapps.list\n");

	DesktopEntryConfig cfg;
	cfg.data_dirs.Add(FixtureFull("data"));
	cfg.current_desktop = "TestDE";

	DesktopEntry entry;
	bool found = FindDesktopEntry(cfg, "test-app.desktop", entry);
	Report(found, "the fixture desktop entry is found");
	Report(entry.type == "Application" && entry.name == "Test App",
	       "type and name parse", entry.name);
	Report(entry.icon == "desktop-fixture-icon" && entry.exec == "testapp %f",
	       "icon and exec parse", entry.exec);
	Report(entry.mime_types.GetCount() == 2 && entry.mime_types[0] == "text/x-test",
	       "the MimeType list splits on semicolons", Join(entry.mime_types, ","));
	Report(entry.categories.GetCount() == 1 && entry.categories[0] == "Utility",
	       "the Categories list splits on semicolons", Join(entry.categories, ","));

	Report(ShouldShowEntry(entry, "TestDE"), "OnlyShowIn accepts the named desktop");
	Report(!ShouldShowEntry(entry, "OtherDE"), "OnlyShowIn rejects another desktop");

	Vector<String> files;
	files.Add("/tmp/file1");
	String cmd = ExpandExec(entry, files, Vector<String>());
	Report(cmd == "testapp /tmp/file1", "field code %f expands to the file argument", cmd);

	Report(GetDefaultApp(cfg, "text/x-test") == "test-app.desktop",
	       "mimeapps.list names the default application");
	Report(GetDefaultApp(cfg, "text/x-absent").IsEmpty(),
	       "an unlisted MIME type has no default application");
}

static void CheckIconTheme()
{
	printf("\n-- XdgIconTheme: index.theme lookup\n");

	IconLookupConfig cfg;
	cfg.theme = "testtheme";
	cfg.fallback_theme = "hicolor";
	cfg.theme_roots.Add(FixtureFull("data/icons"));

	IconTheme theme = LoadIconTheme(cfg, "testtheme");
	Report(theme.name == "testtheme" && theme.dirs.GetCount() == 1,
	       "the index.theme declares one directory",
	       Format("%s dirs %d", theme.name, theme.dirs.GetCount()));

	IconLookup lookup = LookupIcon(cfg, "desktop-fixture-icon", 48, 1, "");
	Report(lookup.found, "the icon resolves at 48px");
	Report(GetFileName(lookup.file) == "desktop-fixture-icon.png",
	       "the resolved file is the fixture icon", lookup.file);
	Report(lookup.theme == "testtheme", "the answering theme is reported", lookup.theme);

	IconLookup absent = LookupIcon(cfg, "no-such-icon-here", 48, 1, "");
	Report(!absent.found, "a missing icon is reported as not found, not an error");
}

static void CheckTrash()
{
	printf("\n-- XdgTrash: move, list, restore\n");

	TrashConfig cfg;
	cfg.home_dir = FixtureFull("home");
	cfg.top_dirs.Add(FixtureFull("data"));

	Vector<TrashDir> trashes = FindTrashDirs(cfg);
	Report(trashes.GetCount() >= 1 && trashes[0].is_home,
	       "the home trash is the first candidate", Format("count %d", trashes.GetCount()));

	// A file to trash, and the record that moves with it.
	String victim = FixtureFull("home/victim.txt");
	SaveFile(victim, "delete me");

	Vector<String> files;
	files.Add(victim);
	TrashResult moved = MoveToTrash(cfg, files);
	Report(moved.status == DESKTOP_OK && moved.failed_files.IsEmpty(),
	       "the file is moved to trash", moved.detail);
	Report(!FileExists(victim), "the original path is gone after trashing");

	Vector<TrashEntry> entries = ListTrash(trashes[0]);
	Report(entries.GetCount() == 1, "the trash lists one entry",
	       Format("count %d", entries.GetCount()));
	if(entries.GetCount() == 1) {
		Report(entries[0].valid && entries[0].original_path == GetFullPath(victim),
		       "the .trashinfo records the original path", entries[0].original_path);
		Report(!entries[0].deletion_date.IsEmpty(),
		       "the .trashinfo records a deletion date", entries[0].deletion_date);

		Vector<String> infos;
		infos.Add(entries[0].trash_info);
		TrashResult restored = RestoreFromTrash(cfg, infos);
		Report(restored.status == DESKTOP_OK && restored.failed_files.IsEmpty(),
		       "the file is restored from trash", restored.detail);
		Report(FileExists(victim), "the original path exists again after restore");
		Report(ListTrash(trashes[0]).GetCount() == 0, "the restored entry left the trash");
	}

	// The .trashinfo round trip, independent of a move: what CreateTrashInfo
	// writes is what ParseTrashInfo reads back.
	String info_rel = "home/.local/share/Trash/info/roundtrip.trashinfo";
	WriteFixture(info_rel, CreateTrashInfo("/orig/path.txt", "2026-01-02T03:04:05"));
	TrashEntry parsed;
	Report(ParseTrashInfo(FixtureFull(info_rel), parsed) &&
	       parsed.original_path == "/orig/path.txt" &&
	       parsed.deletion_date == "2026-01-02T03:04:05",
	       "a .trashinfo round-trips through write and parse", parsed.original_path);

	// Two different files can carry the same basename: the trash renames the
	// second one instead of overwriting the first.
	WriteFixture("home/a/dup.txt", "first");
	WriteFixture("home/b/dup.txt", "second");
	Vector<String> pair;
	pair.Add(FixtureFull("home/a/dup.txt"));
	TrashResult r1 = MoveToTrash(cfg, pair);
	pair[0] = FixtureFull("home/b/dup.txt");
	TrashResult r2 = MoveToTrash(cfg, pair);
	Report(r1.status == DESKTOP_OK && r2.status == DESKTOP_OK,
	       "two same-named files trash separately", r2.detail);
	Report(FileExists(FixtureFull("home/.local/share/Trash/files/dup.txt")),
	       "the first keeps its plain name");
	Report(FileExists(FixtureFull("home/.local/share/Trash/files/dup.1.txt")),
	       "the collision gets a numbered name");
	Report(FileExists(FixtureFull("home/.local/share/Trash/info/dup.txt.trashinfo")) &&
	       FileExists(FixtureFull("home/.local/share/Trash/info/dup.1.txt.trashinfo")),
	       "both collision records exist");
}

CONSOLE_APP_MAIN
{
	printf("Core/Desktop FreeDesktop backend fixture check (no display, no GUI flag)\n");

	g_root = AppendFileName(GetTempPath(), "desktop-services-check");
	BuildFixture();

	CheckXdgDirs();
#ifdef PLATFORM_POSIX
	CheckHostBackend();
#if !defined(PLATFORM_MACOS)
	CheckShellFolderChain();
#endif
#endif
	CheckMime();
	CheckDesktopEntry();
	CheckIconTheme();
	CheckTrash();

	printf("\n%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "PASSED",
	       checks, failures);
	NoMemoryLeaksCheck = true;
	SetExitCode(failures ? 1 : 0);
}