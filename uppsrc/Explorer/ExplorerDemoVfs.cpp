#include "ExplorerDemoVfs.h"

NAMESPACE_UPP

namespace ExplorerDemo {
	String cycle_path;
}

// The demo tree. A directory with a file and two subdirectories, one of them
// holding a file of its own, so both the listing and the sidebar tree have
// something of their own to show: an overlay whose whole content is one flat
// row cannot show that a *tree* is replaced.
static const struct {
	const char *path;
	VfsItemType type;
} demo_entries[] = {
	{ "OverlayNotes",                     VFS_DIRECTORY },
	{ "OverlayNotes/Readme.txt",          VFS_FILE      },
	{ "OverlayData",                      VFS_DIRECTORY },
	{ "OverlayData/figures.csv",          VFS_FILE      },
	{ "OverlayNotes/Deep",                VFS_DIRECTORY },
	{ "OverlayNotes/Deep/nested.txt",     VFS_FILE      },
	{ "overlay.txt",                      VFS_FILE      },
};
static const int demo_entry_count = sizeof(demo_entries) / sizeof(demo_entries[0]);

const char *DemoOverlayVfs::EntryName(int i)  { return demo_entries[i].path; }
int          DemoOverlayVfs::EntryCount()     { return demo_entry_count; }
VfsItemType  DemoOverlayVfs::EntryType(int i) { return demo_entries[i].type; }

bool DemoOverlayVfs::GetFiles(const VfsPath& rel_path, Vector<Shared<VfsItem>>& items)
{
	// AsSysPath hands back "/" for the mount root and "/a/b" deeper, but the
	// table below is relative: strip the leading separator so the root lists
	// everything and a subdirectory lists its own.
	String prefix = rel_path.AsSysPath("/");
	if(prefix.StartsWith("/")) prefix = prefix.Mid(1);
	if(!prefix.IsEmpty()) prefix << '/';
	// An entry at this level and its children both project onto the same top
	// component, so without this a directory shows up once per table row under
	// it -- OverlayNotes three times, once as itself and once per file.
	Vector<String> added;
	auto add_once = [&](const String& name, VfsItemType t) {
		for(const String& n : added)
			if(n == name) return;
		added.Add(name);
		VfsItem& item = items.Add().Create<VfsItem>();
		item.name = name;
		item.type = t;
	};
	for(int i = 0; i < demo_entry_count; i++) {
		String p = demo_entries[i].path;
		if(!p.StartsWith(prefix)) continue;
		String rest = p.Mid(prefix.GetCount());
		if(rest.IsEmpty()) continue;
		int slash = rest.Find('/');
		if(slash >= 0) {
			// An intermediate directory: only its top component is this level's
			// row, and it is a directory even though the table spells the whole
			// path.
			add_once(rest.Left(slash), VFS_DIRECTORY);
			continue;
		}
		add_once(rest, demo_entries[i].type);
	}
	return true;
}

VfsItemType DemoOverlayVfs::CheckItem(const VfsPath& rel_path)
{
	String p = rel_path.AsSysPath("/");
	if(p.StartsWith("/")) p = p.Mid(1);
	for(int i = 0; i < demo_entry_count; i++)
		if(String(demo_entries[i].path) == p)
			return demo_entries[i].type;
	return VFS_NULL;
}

bool MountDemoOverlay(const String& path, String& error)
{
	MountManager& mm = MountManager::System();
	if(mm.Find(path)) {
		error = "demo overlay: '" + path + "' is already a mount point";
		return false;
	}
	// An overlay replaces a directory the user could browse. A path that does
	// not exist under the current mounts is not an overlay, it is a typo -- and
	// the rejection says which path, because a bare "failed" is the error
	// message nobody can act on.
	VfsPath vp;
	vp.Set(path);
	if(!mm.DirectoryExists(vp)) {
		error = "demo overlay: '" + path + "' does not exist under the current mounts";
		return false;
	}
	One<DemoOverlayVfs> owned;
	owned.Create();
	// One<Derived> is not a One<Base>, and MountOwned takes the base One by
	// value: Detach the pointer and hand the ownership over in the same
	// expression.
	if(!mm.MountOwned(path, One<VFS>(owned.Detach()), "demo-overlay")) {
		error = "demo overlay: " + mm.last_error;
		return false;
	}
	return true;
}

bool UnmountDemoOverlay(const String& path, String& error)
{
	MountManager& mm = MountManager::System();
	if(!mm.Unmount(path)) {
		error = "demo overlay: unmount failed: " + mm.last_error;
		return false;
	}
	return true;
}

END_UPP_NAMESPACE
