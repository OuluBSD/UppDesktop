#ifndef _Explorer_ExplorerBookmarks_h
#define _Explorer_ExplorerBookmarks_h

#include "ExplorerLog.h"

NAMESPACE_UPP

// The user's bookmarked places: the folders they want to come back to.
//
// This is application state, not directory state, so it is a service of its own
// rather than part of ExplorerModel -- the same reason ExplorerPolicy is separate.
// It is deliberately free of GUI types, so the window and the headless twin read
// and write exactly the same list, and the file is the single source of truth: the
// window never keeps a copy in memory that a script could not see.
//
// The order is the user's order. There is no sorting and no reordering yet, because
// a list that cannot be reordered by hand cannot be usefully reordered at all.
struct ExplorerBookmarks {
	ExplorerBookmarks();

	//< Reads the file, creating nothing. A missing file is an empty list, not an
	//< error: a first run has no bookmarks and that is a normal state.
	bool Load(String& error);
	//< Writes the list back. Called after every change so the file cannot drift
	//< from what the user sees.
	bool Save(String& error);

	const Vector<String>& Entries() const { return entries; }
	int  GetCount() const                  { return entries.GetCount(); }
	bool Has(const String& path) const;

	//< Adds a path. Refuses a duplicate and refuses an empty path, and both refusals
	//< say so -- a bookmark list that silently grows duplicates looks like a bug in
	//< the file, which is exactly what it would be.
	bool Add(const String& path, String& error);
	//< Removes by path. Removing something that is not there fails, for the same
	//< reason: a command that quietly did nothing is worse than one that complained.
	bool Remove(const String& path, String& error);
	void Clear()                           { entries.Clear(); }

	//< Canonical form used for comparison and storage: absolute, no trailing slash,
	//< no "." or ".." segments left behind by a relative path. Two spellings of the
	//< same folder must not become two bookmarks, so everything funnels through
	//< here before it is stored.
	static String Normalize(const String& path);

	//< Where the list is kept, under the user config folder rather than next to the
	//< executable, so it survives moving the binary and is not writable by accident
	//< from wherever the app happens to be run.
	static String FilePath();

	//< One line per bookmark, for the headless twin and the log.
	String Describe() const;

private:
	Vector<String> entries;
};

ExplorerBookmarks& GetExplorerBookmarks();

END_UPP_NAMESPACE

#endif
