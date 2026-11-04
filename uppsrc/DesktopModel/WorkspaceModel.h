#ifndef _DesktopModel_WorkspaceModel_h_
#define _DesktopModel_WorkspaceModel_h_

//! What a workspace switch did, so that the view never recomputes the indices
/*!
  `changed` is false when the request was refused or was a no-op, and then both
  indices are -1. A transition returns what the view must do; the view only maps
  those two indices onto whatever it opened for them.
*/
struct WorkspaceSwitch : Moveable<WorkspaceSwitch>
{
	bool changed;
	int  from;
	int  to;
};

//! The workspace list, the current index, and the transitions over them
/*!
  A workspace is an index here and nothing else. The view decides what to open
  for an index, so the model holds no window, no display and no timer, and every
  transition runs in a caller that has none of those.

  The list is stored as a count because that is the whole of the desktop state
  today: workspaces are unnamed, unplaced and always open in list order. Names,
  placements and per-workspace window sets belong here when they exist, not in
  the view.

  An empty model is a legal state. `Current()` is then -1, `CreateWorkspace()`
  makes the first workspace current, and every other transition is a no-op.
  `RemoveWorkspace()` refuses to take the last one, so a desktop that opened with
  a single workspace keeps one.
*/
class WorkspaceModel
{
public:
	WorkspaceModel() : count(0), current(-1) {}
	explicit WorkspaceModel(int count);

	//! Append a workspace and return its index. The first one becomes current.
	int CreateWorkspace();

	//! Remove the workspace at id. False when id is out of range, or when it is
	//! the last workspace, which a desktop must never lose.
	bool RemoveWorkspace(int id);

	//! Make id current and report the leaving/arriving pair.
	WorkspaceSwitch SwitchTo(int id);

	//! Switch by a relative step, without wrapping: a step past either end of
	//! the list is a no-op, exactly as NextWorkspace()/PrevWorkspace() behave today.
	WorkspaceSwitch SwitchToRelative(int step);

	int  GetCount() const   { return count; }
	int  Current() const { return current; }
	bool IsCurrent(int id) const { return id == current; }
	bool IsValid(int id) const   { return id >= 0 && id < count; }

	String ToString() const;

private:
	void CheckInvariants() const;

	int count;
	int current;
};

#endif // _DesktopModel_WorkspaceModel_h_
