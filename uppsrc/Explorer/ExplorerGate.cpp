// GUI-only: the gate drives a real window. The twin never builds it.
#ifdef flagGUI

#include "ExplorerGate.h"
#include "ExplorerPolicy.h"
#include <CtrlLib/GuiAutomation.h>

NAMESPACE_UPP

extern bool NoMemoryLeaksCheck;

// ---------------------------------------------------------------------------
// The gate's member surface. Defined outside namespace ExplorerGate because
// they are members of Upp::Explorer and its composites, which that namespace
// does not enclose.
// ---------------------------------------------------------------------------

void Explorer::CollectGateRenderFailures(ValueArray& failures)
{
	Explorer& w = *this;
	failures.Clear();
	auto fail = [&](const String& what) { failures.Add(what); };
	auto check_visible = [&](Ctrl& ctrl, const char *name) {
		Rect visible = ctrl.GetVisibleScreenRect();
		if(!ctrl.IsOpen() || !ctrl.IsVisible() || visible.IsEmpty())
			fail(Format("not-visible:%s", name));
		else if(!(visible == (visible & w.GetVisibleScreenRect())))
			fail(Format("outside-window:%s", name));
	};
	check_visible(w.menu, "menu");
	if(w.bars_shown[0] || w.bars_shown[1] || w.bars_shown[2])
		check_visible(w.all_bars, "bars");
	check_visible(w.nav, "sidebar");
	check_visible(w.main_pane.tabs, "listing");
	check_visible(w.status, "status");

	auto overlaps = [](Ctrl& a, Ctrl& b) {
		Rect intersection = a.GetVisibleScreenRect() & b.GetVisibleScreenRect();
		return !intersection.IsEmpty();
	};
	if(overlaps(w.menu, w.main_pane.tabs))
		fail("overlap:menu-listing");
	if(overlaps(w.status, w.main_pane.tabs))
		fail("overlap:status-listing");
	if(overlaps(w.nav, w.main_pane.tabs))
		fail("overlap:sidebar-listing");
	if((w.bars_shown[0] || w.bars_shown[1] || w.bars_shown[2]) && overlaps(w.all_bars, w.main_pane.tabs))
		fail("overlap:bars-listing");
	// The location row lives inside the panel now (one per pane), so the
	// meaningful no-overlap claim is against the listing ctrls it shares the
	// panel with, not against the panels ctrl that contains it. The hidden
	// view's visible rect is empty, so checking both costs nothing.
	if(w.bars_shown[3] && (overlaps(w.ActivePanel().location, w.ActivePanel().columns)
	                       || overlaps(w.ActivePanel().location, w.ActivePanel().icons)))
		fail("overlap:location-listing");

	auto luminance = [](Color c) {
		auto channel = [](byte v) {
			double x = v / 255.0;
			return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
		};
		return 0.2126 * channel(c.GetR()) + 0.7152 * channel(c.GetG()) + 0.0722 * channel(c.GetB());
	};
	Color ink = SColorText().Resolved();
	Color paper = SColorPaper().Resolved();
	double l1 = luminance(ink), l2 = luminance(paper);
	double contrast = (max(l1, l2) + 0.05) / (min(l1, l2) + 0.05);
	if(contrast < 4.5)
		fail(Format("low-text-contrast:%.2f", contrast));
}

// One named window state per gate check. The mutators they need are private,
// so this is a member: the structural gate walks the window through these
// states and asserts its surface in each.
void Explorer::GateApplyState(const String& name)
{
	if(name == "sidebar-no-tree")
		SetPanelMode(ExplorerNav::ONE);
	else if(name == "split-on")
		SetSplit(true);
	else if(name == "big-icons")
		SetViewMode(VIEW_BIG_ICONS);
	else if(name == "detailed")
		SetViewMode(VIEW_LIST);
	else if(name == "location-hidden")
		SetBarShown(3, false);
	else if(name == "toolbars-hidden") {
		SetBarShown(0, false);
		SetBarShown(1, false);
		SetBarShown(2, false);
	}
}

namespace ExplorerGate {

// The scripted dialog answers, consumed in order. The planner names an action
// whose callback opens a modal dialog (new folder, rename); this is the input
// the planner would have typed, handed to the dialog instead of its event loop,
// because a modal has no event loop the outer planner can drive. Normal runs
// never ask, so the dialogs behave exactly as before.
static Vector<String> s_dialog_answers;

bool TakeDialogAnswer(String& answer)
{
	if(s_dialog_answers.IsEmpty())
		return false;
	answer = s_dialog_answers[0];
	s_dialog_answers.Remove(0);
	return true;
}

int ScriptedYesNo()
{
	if(s_dialog_answers.IsEmpty())
		return -1;
	String a = s_dialog_answers[0];
	s_dialog_answers.Remove(0);
	return a == "yes" ? 1 : 0;
}

static void QueueDialogAnswers(const ValueArray& answers)
{
	for(const Value& a : answers)
		s_dialog_answers.Add(String(a));
}

// ---------------------------------------------------------------------------
// The TOON surface. GuiAutomationVisitor is the walker the --dump-ui menu walk
// already uses: it is both the Visitor that receives the Access calls and the
// Bar that submenu builders fill, so the live MenuBar is walked with real
// paths ("Näkymä/Järjestä/Koko"), check states and enable states in one pass.
// The callback of a named action is fired by a second walk in write mode with
// that path as target -- AutomationVisitor fires it at the matching element --
// which is what keeps the planner's decision honest: it can only name a line
// the walk itself produced.
// ---------------------------------------------------------------------------

static bool IsOption(const AutomationElement& el)   { return el.semantic_type == "Option"; }
static bool IsLabel(const AutomationElement& el)    { return el.semantic_type == "Label"; }
static bool IsMenu(const AutomationElement& el)     { return el.is_menu; }
static bool IsAction(const AutomationElement& el)   { return !el.text.IsEmpty() && !IsOption(el) && !IsLabel(el); }

static String ElementValueLine(const AutomationElement& el)
{
	return "value: " + AsString(el.value);
}

static String BuildToon(Explorer& w, Vector<String>& action_lines)
{
	GuiAutomationVisitor v;
	w.Access(v);
	String toon;
	action_lines.Clear();
	for(const AutomationElement& el : v.elements) {
		if(IsLabel(el)) {
			if(el.text.GetCount())
				toon << "label: " << el.text << "\n";
		}
		else if(IsOption(el))
			toon << "option: " << el.path << " = " << (el.checked ? "true" : "false") << "\n";
		else if(IsMenu(el))
			toon << "menu: " << el.path << "\n";
		else if(IsAction(el)) {
			toon << "action: " << el.path << (el.enabled ? "" : " (disabled)") << "\n";
			action_lines.Add(el.path);
		}
		if(!el.value.IsVoid() && !IsLabel(el))
			toon << ElementValueLine(el) << "\n";
		else if(IsLabel(el) && !el.value.IsVoid())
			toon << ElementValueLine(el) << "\n";
	}
	return toon;
}

// Fires the action the planner named, by re-walking in write mode. Returns
// false when no element matches, which the gate reports as the planner having
// named an action that does not exist -- a hard FAIL per the gate spec.
static bool ApplyNamed(Explorer& w, const String& target)
{
	GuiAutomationVisitor v;
	v.write_mode = true;
	v.target_path = target;
	w.Access(v);
	return v.found;
}

// Puts a value in a field by its surface path (the location field), again via
// a write-mode walk: EditField::Access writes and fires its action when the
// walk found its label. target_action stays false so a coincidental action
// line with the same text is not fired.
static bool SetFieldValue(Explorer& w, const String& field_path, const Value& val)
{
	GuiAutomationVisitor v;
	v.write_mode = true;
	v.target_path = field_path;
	v.target_value = val;
	v.target_action = false;
	w.Access(v);
	return v.found;
}

// ---------------------------------------------------------------------------
// The rendered checks, in the shape of PokerPanel's CollectRenderedGateFailures:
// visibility of the frames, no overlap between them, and text/paper contrast
// (WCAG 4.5:1). An overlap warning is a finding, not noise.
// ---------------------------------------------------------------------------

static void CloseGateWindow(Explorer& w)
{
	w.TopWindow::Close();   // gate windows never prompt to save or persist state
	Ctrl::ProcessEvents();
}

// The toolbar config file, the same path Explorer writes; the gate snapshots
// and restores it around its state walks.
static String GateToolbarsConfPath()
{
	return AppendFileName(GetConfigFolder(), "toolbars.cfg");
}

// The toolbar config as it was when this gate started, restored on exit: the
// gate's own state walks toggle the groups, and that scaffolding must not
// become the user's configuration.
static String gate_toolbars_conf;

static int ExitGate(int rc)
{
	fflush(stdout);
	fflush(stderr);
	if(!gate_toolbars_conf.IsEmpty())
		FileOut(GateToolbarsConfPath()).Put(gate_toolbars_conf);
	NoMemoryLeaksCheck = true;
	_exit(rc);
}

static Explorer *OpenGateWindow(const String& path)
{
	// Snapshot once per process, before the window's own load-and-save cycle:
	// ExitGate puts the file back when the run leaves.
	if(gate_toolbars_conf.IsEmpty())
		gate_toolbars_conf = LoadFile(GateToolbarsConfPath());
	Explorer *w = new Explorer(path, false, VIEW_LIST, true, false);
	w->SetRect(40, 40, 1240, 840);
	w->Open();
	Ctrl::ProcessEvents();
	Ctrl::ProcessEvents();
	return w;
}

// ---------------------------------------------------------------------------
// The four CLI entry points.
// ---------------------------------------------------------------------------

int ToonCli()
{
	const Vector<String>& args = CommandLine();
	String path;
	for(const String& a : args)
		if(!a.StartsWith("-")) { path = a; break; }
	Explorer *w = OpenGateWindow(path);
	Vector<String> action_lines;
	String toon = BuildToon(*w, action_lines);
	ValueMap out;
	out("status", "ok")("gate", "gui-toon")("toon", toon)
	   ("state_hash", AsString(GetHashValue(toon)))
	   ("actions", action_lines.GetCount())("action_executed", false);
	Cout() << AsJSON(out) << "\n";
	CloseGateWindow(*w);
	return toon.IsEmpty() ? 1 : 0;
}

int AccessGateCli()
{
	const Vector<String>& args = CommandLine();
	String path;
	for(const String& a : args)
		if(!a.StartsWith("-")) { path = a; break; }
	Explorer *w = OpenGateWindow(path);

	// One entry per window state the gate names. setup rearranges the window;
	// required rows must be present in that state's TOON, absent rows must not
	// be there -- a hidden toolbar that still advertises its buttons is a
	// surface lying about what is reachable.
	struct StateCheck {
		const char *name;
		Vector<String> required;
		Vector<String> absent;
	};
	// Array, not Vector: StateCheck holds Vectors of its own and the fork's
	// Vector demands Moveable elements.
	Array<StateCheck> states;

	{
		StateCheck& s = states.Add();
		s.name = "default";
		// The factory sidebar is the Win98 folders panel: the tree, and the
		// places panel only when a mode or a key brings it back. The default
		// state therefore asserts the tree and asserts the places panel's
		// absence -- "default" is the state a fresh window is actually in.
		s.required.Add("action: Tiedosto/Uusi kansio");
		s.required.Add("option: Näkymä/Yksityiskohtainen luettelo = true");
		s.required.Add("option: Näkymä/Työkalupalkit/Navigointi = true");
		s.required.Add("label: Sijainti");
		s.required.Add("label: listing");
		s.required.Add("label: Tree");
		s.absent.Add("label: Sijainnot");
	}
	{
		// Mode ONE hides the tree: a surface that still advertised it would be
		// claiming something the user cannot see.
		StateCheck& s = states.Add();
		s.name = "sidebar-no-tree";
		s.absent.Add("label: Tree");
		s.required.Add("label: Sijainnot");
	}
	{
		StateCheck& s = states.Add();
		s.name = "split-on";
		s.required.Add("option: Näkymä/Synkronooi paneelit = false");
		s.required.Add("action: Näkymä/Vaihda paneelia");
	}
	{
		StateCheck& s = states.Add();
		s.name = "big-icons";
		s.required.Add("option: Näkymä/Isot kuvakkeet = true");
	}
	{
		StateCheck& s = states.Add();
		s.name = "detailed";
		s.required.Add("option: Näkymä/Yksityiskohtainen luettelo = true");
	}
	{
		StateCheck& s = states.Add();
		s.name = "location-hidden";
		s.absent.Add("label: Sijainti");
		s.required.Add("option: Näkymä/Työkalupalkit/Sijainti = false");
	}
	{
		StateCheck& s = states.Add();
		s.name = "toolbars-hidden";
		s.absent.Add("action: Takaisin");
		s.required.Add("option: Näkymä/Työkalupalkit/Navigointi = false");
	}

	ValueArray state_json;
	bool all_passed = true;
	for(StateCheck& s : states) {
		w->GateApplyState(s.name);
		Ctrl::ProcessEvents();
		Vector<String> action_lines;
		String toon = BuildToon(*w, action_lines);
		ValueArray missing, unexpected;
		for(const String& r : s.required)
			if(toon.Find(r) < 0) missing.Add(r);
		for(const String& r : s.absent)
			if(toon.Find(r) >= 0) unexpected.Add(r);
		bool ok = missing.IsEmpty() && unexpected.IsEmpty();
		all_passed = all_passed && ok;
		// A named ValueMap cannot chain its constructor (no two-argument ctor),
		// so the chain starts from assignment.
		ValueMap st;
		st("state", s.name)("ok", ok)
		 ("state_hash", AsString(GetHashValue(toon)))
		 ("missing", missing)("unexpected", unexpected);
		// A failed state has to be able to say what its surface actually was,
		// or the next reader is back to guessing.
		if(!ok) st("toon", toon);
		state_json.Add(st);
	}
	CloseGateWindow(*w);
	ValueMap out;
	out("status", all_passed ? "passed" : "failed")("gate", "gui-access-structural")
	   ("states", state_json)("action_executed", false);
	Cout() << AsJSON(out) << "\n";
	return all_passed ? 0 : 1;
}

int RenderGateCli()
{
	const Vector<String>& args = CommandLine();
	String path;
	for(const String& a : args)
		if(!a.StartsWith("-")) { path = a; break; }
	Explorer *w = OpenGateWindow(path);
	ValueArray failures;
	w->CollectGateRenderFailures(failures);
	bool passed = failures.IsEmpty();
	ValueMap out;
	out("status", passed ? "passed" : "failed")("gate", "gui-render")
	   ("failures", failures)("action_executed", false);
	Cout() << AsJSON(out) << "\n";
	CloseGateWindow(*w);
	return passed ? 0 : 1;
}

// The openai-compatible chat-completions endpoint, the same shape GameScreenParser
// and PokerPanel drive: base_url/model/api_key from one config file, never
// printed. The config lives where theirs does so one settings file serves all
// the gate runners.
struct GateAiConfig {
	String base_url, model, api_key;
	int    timeout_ms = 30000;
};

static bool LoadAiConfig(const String& path, GateAiConfig& cfg, String& error)
{
	String json = LoadFile(path);
	if(json.IsEmpty()) {
		error = "config '" + path + "' is missing or empty";
		return false;
	}
	Value v;
	try { v = ParseJSON(json); }
	catch(Exc& e) { error = String("ai.json: ") + e; return false; }
	cfg.base_url = String(v["base_url"]);
	cfg.model = String(v["model"]);
	cfg.api_key = String(v["api_key"]);
	cfg.timeout_ms = IsNull(v["timeout_ms"]) ? 30000 : (int)v["timeout_ms"];
	if(cfg.base_url.IsEmpty() || cfg.model.IsEmpty()) {
		error = "ai.json needs base_url and model";
		return false;
	}
	return true;
}

static String PlannerChatCompletions(const GateAiConfig& cfg, const String& system_prompt,
                                     const String& user_prompt, String& error)
{
	String url = cfg.base_url;
	if(!url.EndsWith("/"))
		url << "/";
	url << "chat/completions";
	String body = "{\"model\":" + AsJSON(cfg.model)
		+ ",\"messages\":[{\"role\":\"system\",\"content\":" + AsJSON(system_prompt)
		+ "},{\"role\":\"user\",\"content\":" + AsJSON(user_prompt) + "}]"
		+ ",\"temperature\":0}";
	HttpRequest http;
	http.Url(url);
	http.Header("Authorization", "Bearer " + cfg.api_key);
	http.Header("Content-Type", "application/json");
	http.Post(body);
	http.Timeout(cfg.timeout_ms);
	String reply = http.Execute();
	if(reply.IsEmpty()) {
		error = Format("chat-completions failed: %d %s", http.GetStatusCode(), http.GetErrorDesc());
		return String();
	}
	Value value;
	try { value = ParseJSON(reply); }
	catch(Exc& e) { error = String("chat-completions JSON: ") + e; return String(); }
	const ValueArray& choices = value["choices"];
	if(choices.GetCount() == 0) { error = "chat-completions returned no choices"; return String(); }
	return String(choices[0]["message"]["content"]);
}

struct GateStep : Moveable<GateStep> {
	int    step = 0;
	String state_hash;
	String action;
};

int LlmGateCli(const Vector<String>& args)
{
	String goal, mock_path, ai_config_path, path;
	int max_steps = 12;
	for(int i = 0; i < args.GetCount(); i++) {
		if(args[i].StartsWith("--goal="))
			goal = args[i].Mid(7);
		else if(args[i].StartsWith("--planner-mock="))
			mock_path = args[i].Mid(15);
		else if(args[i].StartsWith("--llm-config="))
			ai_config_path = args[i].Mid(13);
		else if(args[i].StartsWith("--llm-max-steps="))
			max_steps = clamp(StrInt(args[i].Mid(16)), 1, 64);
		else if(!args[i].StartsWith("-") && !args[i].StartsWith("--"))
			path = args[i];
	}
	if(goal.IsEmpty()) { Cerr() << "ERROR: --gui-llm-gate needs --goal=<text>\n"; return 2; }
	if(path.IsEmpty()) {
		// The goals act on a fixture, not on the user's directories; the mock
		// runner names it as the bare path.
		path = "/tmp/kilo/gatework";
	}

	bool have_config = false;
	GateAiConfig cfg;
	String error;
	ValueArray mock_steps;
	String goal_marker, goal_absent;
	if(!mock_path.IsEmpty()) {
		Value mock;
		try { mock = ParseJSON(LoadFile(mock_path)); }
		catch(Exc& e) { Cerr() << "ERROR: planner mock JSON: " << e << "\n"; return 2; }
		mock_steps = mock["steps"];
		goal_marker = String(mock["goal_marker"]);
		goal_absent = String(mock["goal_absent"]);
		QueueDialogAnswers(mock["answers"]);
	}
	else {
		if(ai_config_path.IsEmpty())
			ai_config_path = GetHomeDirFile(".config/game_screen_parser/ai.json");
		if(!LoadAiConfig(ai_config_path, cfg, error)) {
			Cout() << AsJSON(ValueMap()("status", "error")("gate", "gui-user-gate-llm")
				("error_code", "llm-config-missing")("detail", error)
				("action_executed", false)) << "\n";
			return 1;
		}
		have_config = true;
	}

	Explorer *w = OpenGateWindow(path);
	// The gate is authorized to act on its fixture: the confirmation channel is
	// the auto-accept one the headless twin gets from --confirm-yes, and the
	// modal dialogs take the scripted answers. This runs on a throwaway tree.
	// It has to be installed AFTER the window is built, because the constructor
	// wires the GUI's prompt channel and would overwrite it back.
	GetExplorerPolicy().confirm
		= [](const ExplorerPolicy::Request&, const String&) { return true; };

	Vector<GateStep> steps;
	Vector<String> planner_errors;
	ValueArray render_failures;
	bool goal_reached = false;
	String stop_reason;
	int mock_index = 0;
	for(int step = 1; step <= max_steps; step++) {
		Vector<String> action_lines;
		String toon = BuildToon(*w, action_lines);
		String hash = AsString(GetHashValue(toon));
		w->CollectGateRenderFailures(render_failures);

		bool marker_reached = !goal_marker.IsEmpty() && toon.Find(goal_marker) >= 0 && step > 1;
		bool absent_reached = !goal_absent.IsEmpty() && toon.Find(goal_absent) < 0 && step > 1;
		if(marker_reached || absent_reached) {
			goal_reached = true;
			stop_reason = marker_reached ? "goal-marker" : "goal-absent";
			break;
		}

		String decision;
		if(!mock_path.IsEmpty()) {
			if(mock_index >= mock_steps.GetCount()) {
				stop_reason = "mock-exhausted";
				break;
			}
			const Value& ms = mock_steps[mock_index++];
			if(!IsNull(ms["set"])) {
				// A field-write step: { "set": { "<surface path>": "<value>" } }
				const ValueMap& kv = ms["set"];
				bool ok = false;
				for(int k = 0; k < kv.GetCount(); k++)
					ok = SetFieldValue(*w, kv.GetKey(k), kv.GetValue(k)) || ok;
				if(!ok) planner_errors.Add("set-target-not-found");
				GateStep& recorded = steps.Add();
				recorded.step = step;
				recorded.state_hash = hash;
				recorded.action = "set " + AsJSON(ms["set"]);
				Ctrl::ProcessEvents();
				continue;
			}
			decision = String(ms["action"]);
		}
		else {
			String actions_text;
			for(const String& line : action_lines)
				actions_text << "action: " << line << "\n";
			String system_prompt =
				"You are the action planner of a native GUI file manager.\n"
				"The current UI state is given as TOON text: 'label:' lines describe\n"
				"visible text, 'action:' and 'option:' lines are the user-reachable\n"
				"commands, 'menu:' lines are containers, 'value:' lines show editable\n"
				"content.\n"
				"Reply with exactly ONE line copied from the available actions, or the\n"
				"single word done when the goal is reached. No other text.";
			String user_prompt = "Goal: " + goal + "\n\nCurrent UI (TOON):\n"
				+ toon + "\nAvailable actions:\n" + actions_text;
			decision = PlannerChatCompletions(cfg, system_prompt, user_prompt, error);
			if(decision.IsEmpty()) {
				planner_errors.Add(error);
				break;
			}
		}
		decision = TrimBoth(decision);
		if(decision == "done") {
			goal_reached = true;
			stop_reason = "planner-done";
			break;
		}
		if(!ApplyNamed(*w, decision)) {
			planner_errors.Add("planner-action-not-found: " + decision);
			break;
		}
		GateStep& recorded = steps.Add();
		recorded.step = step;
		recorded.state_hash = hash;
		recorded.action = decision;
		Ctrl::ProcessEvents();
	}
	// Final rendered verdict on the state the loop ended in.
	w->CollectGateRenderFailures(render_failures);
	CloseGateWindow(*w);

	double explicit_score = render_failures.IsEmpty() ? 1.0
		: max(0.0, 1.0 - 0.25 * render_failures.GetCount());
	double reach_score = goal_reached ? (steps.GetCount() <= 6 ? 1.0 : 0.7) : 0.0;
	double clarity_score = render_failures.IsEmpty() ? 1.0 : 0.5;
	double score = (explicit_score + reach_score + clarity_score) / 3.0;
	ValueArray step_json;
	for(const GateStep& st : steps)
		step_json.Add(ValueMap()("step", st.step)("state_hash", st.state_hash)("action", st.action));
	ValueArray planner_error_json;
	for(const String& e : planner_errors)
		planner_error_json.Add(e);
	ValueMap out;
	out("status", goal_reached && render_failures.IsEmpty() ? "passed" : "failed")
	   ("gate", "gui-user-gate-llm")
	   ("goal", goal)
	   ("goal_reached", goal_reached)
	   ("stop_reason", stop_reason)
	   ("planner", mock_path.IsEmpty() ? "llm-endpoint" : "mock")
	   ("steps", AsString(steps.GetCount()))
	   ("score", score)
	   ("scores", ValueMap()("explicit", explicit_score)("reachability", reach_score)("clarity", clarity_score))
	   ("render_failures", render_failures)
	   ("planner_errors", planner_error_json)
	   ("history", step_json)
	   ("action_executed", !steps.IsEmpty());
	Cout() << AsJSON(out) << "\n";
	return goal_reached && render_failures.IsEmpty() ? 0 : 1;
}


} // namespace ExplorerGate

bool PlacesPanel::Access(Visitor& v)
{
	v.AccessLabel(caption.GetText().ToString());
	// Only the list that is showing: the two hidden lists' rows would make the
	// TOON claim places the user cannot click right now.
	Active().Access(v);
	return true;
}

bool ExplorerNav::Access(Visitor& v)
{
	// The mode decides, not IsVisible(): SetMode keeps widgets and mode in step,
	// and the walk must describe the state the window is in. A widget-level
	// visibility check let the tree's whole node surface through even with the
	// tree collapsed, because the splitter's relayout on a mode change can leave
	// the child's visible flag stale until the next paint.
	if(ExplorerNav::ModeTree(mode))
		tree.Access(v);
	if(mode != ExplorerNav::TREE_ONLY)
		left.Access(v);
	if(ExplorerNav::ModeRight(mode))
		right.Access(v);
	return true;
}

bool ExplorerColumns::Access(Visitor& v)
{
	rows.Access(v);
	return true;
}

bool ExplorerPanel::Access(Visitor& v)
{
	v.AccessLabel(caption.GetText().ToString());
	// The pane's own location row, visible or not: the planner drives the path
	// field and the Navigate action here, and the field is this pane's, so a
	// drive through it acts on this pane.
	if(location.IsVisible())
		location.Access(v);
	// The mode decides which list is the listing. A stale hidden list is not
	// part of the surface: two rows for one entry is a lie about the window.
	if(IsIconMode())
		icons.Access(v);
	else
		columns.Access(v);
	return true;
}

// The window's surface: the live menu bar (walked with paths, checks and enable
// states), the shown toolbar groups, the location group, the sidebar and the
// active listing. The split pane is deliberately not declared: it is a
// transient second view of the same commands, and the planner acts on the pane
// the window itself acts on.
bool Explorer::Access(Visitor& v)
{
	menu.Access(v);
	// One bar carries all shown groups; a hidden group's items are simply not
	// in it, so the TOON surface hides them by construction.
	if(bars_shown[0] || bars_shown[1] || bars_shown[2]) all_bars.Access(v);
	nav.Access(v);
	ActivePanel().Access(v);
	return true;
}


namespace ExplorerGate {

int Main(const Vector<String>& args)
{
	for(const String& a : args) {
		if(a == "--gui-toon")
			return ExitGate(ToonCli());
		if(a == "--gui-access-gate")
			return ExitGate(AccessGateCli());
		if(a == "--gui-render-gate")
			return ExitGate(RenderGateCli());
		if(a.StartsWith("--gui-llm-gate"))
			return ExitGate(LlmGateCli(args));
	}
	return 2;
}

}

END_UPP_NAMESPACE

#endif // flagGUI
