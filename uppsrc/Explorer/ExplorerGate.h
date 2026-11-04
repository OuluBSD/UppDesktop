#ifndef _Explorer_ExplorerGate_h_
#define _Explorer_ExplorerGate_h_

// GUI-only: the gate drives a real window and CtrlLib's Access machinery.
#ifdef flagGUI

#include <CtrlLib/CtrlLib.h>
#include "Explorer.h"

NAMESPACE_UPP

namespace ExplorerGate {

//< The three gui-user-gate entry points (ai/docs/gates/gui-user-gate.md), plus
//< the plain TOON dump. Each returns a process exit code; each builds its own
//< window, so main.cpp can hand the command line over before it opens one.

//< --gui-toon: the live Access surface as TOON text plus its state hash.
int ToonCli();

//< --gui-access-gate: the TOON of every window state the gate names, asserting
//< the required action/option/label rows in each.
int AccessGateCli();

//< --gui-render-gate: a real window, checked for visibility, clipping, frame
//< overlaps and text/paper contrast.
int RenderGateCli();

//< --gui-llm-gate --goal=<text> [--planner-mock=<file>] [--llm-max-steps=N]:
//< an action planner walks the live TOON state to the goal through the real
//< Access callbacks. The mock is a scripted decision list; without it the
//< openai-compatible endpoint from the shared ai.json config is asked.
int LlmGateCli(const Vector<String>& args);

//< Dispatches on the gate flags in args; returns 2 when there is none.
int Main(const Vector<String>& args);

//< The gate's scripted answer to the modal dialogs (NamePrompt). While set,
//< the next dialog takes it and accepts instead of running its event loop --
//< the action the planner named opens it, and this is the input the planner
//< would have typed. Empty string means "no scripted answer".
bool TakeDialogAnswer(String& answer);

//< The scripted answer to a yes/no prompt (delete's permanent-delete
//< confirmation and its folders-inside question). Consumes one queued answer:
//< "yes" answers 1, anything else answers 0, and an empty queue answers -1,
//< which means "ask the user normally".
int ScriptedYesNo();

}

END_UPP_NAMESPACE

#endif // flagGUI

#endif
