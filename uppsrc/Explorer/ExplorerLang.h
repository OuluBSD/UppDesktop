#include "ExplorerModel.h"

#ifndef _Explorer_ExplorerLang_h_
#define _Explorer_ExplorerLang_h_

NAMESPACE_UPP

// Explorer's own translations, from Explorer.t.
//
// The strings themselves are not declared here: t_() takes the English text
// itself as its key, so a call site reads t_("Bookmarks") whether or not a
// translation exists. What is declared here is choosing the language, because
// that has to happen once, before the first t_() call, and every binary in this
// package has to make the same choice the same way.
//
// English is the default and not a fallback in the middle: a process with no
// language set shows the keys, which is what an untranslated string *is*.
void ExplorerSetLanguage();
void ExplorerSetLanguage(const String& language_name);
//< The language in force, as the four-letter code Core uses ("fiFI"), or empty
//< when nothing has been set and English is in force.
String ExplorerGetLanguage();

END_UPP_NAMESPACE

#endif