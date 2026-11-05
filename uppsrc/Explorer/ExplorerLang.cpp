#include "ExplorerLang.h"

// The translations of Explorer's own strings.
//
// Core's own strings are registered from Core.t the same way; this is the same
// mechanism with a different file, which is why it is one file of four lines
// rather than a translation framework.
//
// TFILE is a macro because Core/t.h is written to be included by every package
// that has strings: the table it builds is `static` inside an INITBLOCK, and
// including it twice from two translation units would register the same module
// twice.
#define TFILE <Explorer/Explorer.t>
#include <Core/t.h>
#undef TFILE
