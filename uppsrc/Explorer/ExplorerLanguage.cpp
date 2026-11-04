#include "ExplorerLang.h"

NAMESPACE_UPP

// Setting the language is three things, in this order, and the order is the
// point:
//
// 1. --lang / $EXPLORER_LANG, so a user and a test can ask for one.
// 2. The desktop's own language (LANG on POSIX), so a Finnish desktop gets a
//    Finnish window without being told.
// 3. English, so a machine that says nothing gets English rather than a
//    language nobody asked for.
//
// GetSystemLNG() already does (2) and (3) for POSIX, but it caches in a static
// and it reads the charset out of the same string, which is not what a command
// line asked for: a user who says --lang fiFI on a C-locale machine asked for
// Finnish, not for Finnish-as-the-codepage-happens-to-say.
void ExplorerSetLanguage(const String& language_name)
{
	if(language_name.IsEmpty()) return;
	// Normalised to four letters, because that is what Core's codes are and what
	// everything that produces a language name here produces: "fiFI", "FI-FI" and
	// "fi_FI.UTF-8" are three spellings of one thing and LNGFromText skips a
	// separator only between the halves.
	String letters;
	for(const char *p = ~language_name; *p && letters.GetLength() < 4; p++)
		if(IsAlpha(*p))
			letters += *p;
	if(letters.GetLength() < 2) {
		Cerr() << "explorer: '" << language_name << "' is not a language name, using English\n";
		return;
	}
	letters = ToLower(letters.Left(2)) + ToUpper(letters.Mid(2, 2));
	int lang = LNGFromText(~letters);
	// LNGFromText packs whatever letters it is given, so it says yes to "xxYY" as
	// readily as to "fiFI". Core's table of languages it actually knows is the only
	// thing that can tell the two apart, and asking it is the difference between
	// falling back to English and switching to a language with no strings in it.
	bool known = false;
	for(const int *all = GetAllLanguages(); all && *all; all++)
		if((*all & 0xFFFFF) == (lang & 0xFFFFF)) { known = true; break; }
	if(!lang || !known) {
		// Not one of Core's known languages. Said once, and then English: a window
		// that silently shows the wrong language is worse than one that says it
		// does not know the one asked for.
		Cerr() << "explorer: unknown language '" << language_name << "', using English\n";
		return;
	}
	// The charset is not optional. A .t file registers its entries as UTF-8, and a
	// language set without one does not match them: t_() then returns the key and
	// the window is English with a Finnish language number, which looks exactly
	// like a missing translation table.
	SetLanguage(SetLNGCharset(lang, CHARSET_UTF8));
}

void ExplorerSetLanguage()
{
	// Ask, ask, then infer -- in that order, and the order is the point. Whether
	// anything was asked for is decided by *looking for it*, not by comparing the
	// current language against English: SetLanguage() folds the charset into the
	// number, so "is it English?" is not the same question as "was English asked
	// for", and getting that wrong silently prefers LANG over --lang.
	if(!GetEnv("EXPLORER_LANG").IsEmpty()) {
		ExplorerSetLanguage(GetEnv("EXPLORER_LANG"));
		return;
	}
	const Vector<String>& args = CommandLine();
	for(int i = 0; i < args.GetCount() - 1; i++)
		if(args[i] == "--lang") {
			ExplorerSetLanguage(args[i + 1]);
			return;
		}
	// What the desktop says. LANG looks like "fi_FI.UTF-8" here, and the part
	// after the dot is the charset rather than part of the name.
	String env = TrimLeft(TrimRight(Environment().Get("LANG", Null)));
	int dot = env.Find('.');
	if(dot >= 0) env = env.Left(dot);
	ExplorerSetLanguage(env);
}

String ExplorerGetLanguage()
{
	int lang = GetCurrentLanguage();
	// LNGAsText of the default carries no charset, so the comparison has to
	// ignore it: English with a charset attached is still English.
	if((lang & 0xFFFFF) == LNG_ENGLISH) return String();
	return LNGAsText(lang);
}

END_UPP_NAMESPACE
