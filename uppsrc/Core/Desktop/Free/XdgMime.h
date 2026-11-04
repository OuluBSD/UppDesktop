// XdgMime.h
//
// MIME type resolution per the FreeDesktop MIME Applications Specification.
// Supports globs, globs2, aliases, subclasses, and magic sniffing.

#ifndef _Core_Desktop_Free_XdgMime_h_
#define _Core_Desktop_Free_XdgMime_h_

#include <Core/Core.h>

namespace Upp {

// A glob pattern with weight for priority (legacy globs format:
// "mimetype:glob[:flags]").
struct MimeGlob : Moveable<MimeGlob> {
	String pattern;     // e.g. "*.txt"
	String mime;        // e.g. "text/plain"
	String flags;       // "" (case sensitive) or "ci" (case insensitive)
	int    weight       = 50; // higher = more specific
};

// A glob2 pattern with weight (globs2 format:
// "weight:mimetype:glob[:flags]").
struct MimeGlob2 : Moveable<MimeGlob2> {
	String pattern;     // e.g. "*.txt"
	String mime;
	String flags;       // "" or "ci"
	int    weight       = 50;
};

// An alias mapping.
struct MimeAlias : Moveable<MimeAlias> {
	String alias;       // e.g. "text/x-script"
	String mime;        // e.g. "application/x-shellscript"
};

// A subclass relation: child -> parent.
struct MimeSubclass : Moveable<MimeSubclass> {
	String child;
	String parent;
};

// Magic rule for content-based sniffing.
struct MimeMagic : Moveable<MimeMagic> {
	int    priority    = 50;
	String mime;
	Vector<String> matches; // each is "offset:value:mask" or "offset:value"
};

// A MIME type description entry.
struct MimeInfo : Moveable<MimeInfo> {
	String mime;
	String description;
	String generic_icon; // icon name for this type
};

// Configuration for MIME lookup.
struct MimeLookupConfig : Moveable<MimeLookupConfig> {
	Vector<String> mime_roots;  // XDG_DATA_DIRS + custom
	bool           use_magic    = true; // sniff content if globs fail
};

// Result of MIME detection.
struct MimeDetectResult : Moveable<MimeDetectResult> {
	String mime;
	String source;    // "glob", "glob2", "alias", "magic", "default"
	int    weight     = 0;
	bool   found      = false;
};

// Load all MIME database files from the configured roots.
void LoadMimeDatabase(const MimeLookupConfig& cfg,
                      Vector<MimeGlob>& globs,
                      Vector<MimeGlob2>& globs2,
                      Vector<MimeAlias>& aliases,
                      Vector<MimeSubclass>& subclasses,
                      Vector<MimeMagic>& magic,
                      Vector<MimeInfo>& info);

// Detect MIME type from filename (globs/globs2/aliases).
MimeDetectResult DetectMimeFromName(const MimeLookupConfig& cfg,
                                    const Vector<MimeGlob>& globs,
                                    const Vector<MimeGlob2>& globs2,
                                    const Vector<MimeAlias>& aliases,
                                    const String& filename);

// Detect MIME type from file content (magic).
MimeDetectResult DetectMimeFromContent(const MimeLookupConfig& cfg,
                                       const Vector<MimeMagic>& magic,
                                       const String& file_path);

// Full detection: name first, then magic if enabled and name inconclusive.
MimeDetectResult DetectMime(const MimeLookupConfig& cfg,
                            const Vector<MimeGlob>& globs,
                            const Vector<MimeGlob2>& globs2,
                            const Vector<MimeAlias>& aliases,
                            const Vector<MimeMagic>& magic,
                            const String& file_path,
                            const String& filename_hint = String());

// Get description and generic icon for a MIME type.
MimeInfo GetMimeInfo(const Vector<MimeInfo>& info, const String& mime);

// Resolve aliases and return canonical MIME.
String ResolveMimeAlias(const Vector<MimeAlias>& aliases, const String& mime);

// Check if `mime` is a subclass of `parent` (transitive).
bool IsMimeSubclass(const Vector<MimeSubclass>& subclasses, const String& mime, const String& parent);

// Get all parent classes of a MIME type.
Vector<String> GetMimeParents(const Vector<MimeSubclass>& subclasses, const String& mime);

} // Upp

#endif // _Core_Desktop_Free_XdgMime_h_