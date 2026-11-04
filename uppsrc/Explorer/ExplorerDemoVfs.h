#ifndef _Explorer_ExplorerDemoVfs_h_
#define _Explorer_ExplorerDemoVfs_h_

#include <Core/Core.h>
#include <Vfs/Core/VFS.h>
#include <Vfs/Core/Mount.h>

NAMESPACE_UPP

// A tiny synthetic filesystem used only by the --demo-overlay demonstration:
// any VFS mounted under any path replaces what the panels show there, with
// Explorer knowing nothing about it. Phase-02 task 04. It is deliberately
// trivial -- a fixed tree, no storage, no mutation -- because the point it
// proves is about MountManager resolution, not about this class.
//
// Read-only by construction: VFS's default capability set is browsable and
// non-mutable, and nothing here opts in, so every mutation that reaches the
// mount is refused by the layer that owns the capability -- and the refusal
// names the mount ("Mount '<path>' is read-only").
struct DemoOverlayVfs : VFS {
	//< The tree the mount serves. Only used by the demo, so the names are
	//< chosen to be unmistakable: nothing on the host filesystem is called
	//< these.
	static const char *EntryName(int i);
	static int          EntryCount();
	static VfsItemType  EntryType(int i);

	bool GetFiles(const VfsPath& rel_path, Vector<Shared<VfsItem>>& items) override;
	VfsItemType CheckItem(const VfsPath& rel_path) override;

	DemoOverlayVfs() { caps.description = "demo overlay (read-only)"; }
};

// Mounts the demo VFS at path. The path has to exist under the mounts already
// present -- an overlay replaces a directory the user could browse -- and the
// rejection names the path. Returns false with error filled.
bool MountDemoOverlay(const String& path, String& error);

// Detaches the demo mount at path. Fails with error when there is no such
// mount (the cycle demonstration unmounts what it mounted; nothing persists
// across processes).
bool UnmountDemoOverlay(const String& path, String& error);

// The --demo-cycle target, set by the GUI entry point and consumed by the
// window's dump: the dump itself is the baseline stage, then it mounts,
// re-reads and unmounts inside the same run, so one artifact holds all three
// stages. Empty (the default) means no cycle.
namespace ExplorerDemo {
	extern String cycle_path;
}

END_UPP_NAMESPACE

#endif
