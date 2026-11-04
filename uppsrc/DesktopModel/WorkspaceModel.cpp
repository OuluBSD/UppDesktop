#ifndef _DesktopModel_DesktopModel_cpp_
#define _DesktopModel_DesktopModel_cpp_

#include "DesktopModel.h"

NAMESPACE_UPP

WorkspaceModel::WorkspaceModel(int count)
	: count(0), current(-1)
{
	for(int i = 0; i < count; i++)
		CreateWorkspace();
}

void WorkspaceModel::CheckInvariants() const
{
	ASSERT(count >= 0);
	// The current index is -1 exactly when there is no workspace at all. Every
	// other state must point at a workspace that exists, because a view that
	// follows current and finds nothing has an off-by-one to chase.
	ASSERT(count == 0 ? current == -1 : current >= 0 && current < count);
}

int WorkspaceModel::CreateWorkspace()
{
	int id = count++;
	// The first workspace created becomes the current one, so a desktop that
	// starts from an empty model lands on workspace 0 instead of on -1.
	if(current < 0)
		current = 0;
	CheckInvariants();
	return id;
}

bool WorkspaceModel::RemoveWorkspace(int id)
{
	if(id < 0 || id >= count)
		return false;
	if(count <= 1)
		return false;

	count--;

	// Clamp after the removal, not before. Clamping first is the bug this
	// replaces: removing the last workspace while it was current left current
	// pointing one past the end of the shorter list, and the next switch
	// request was then out of range for a reason no caller could see.
	if(current >= count)
		current = count - 1;
	// A workspace before the removed one shifted down by one.
	else if(current > id)
		current--;

	CheckInvariants();
	return true;
}

WorkspaceSwitch WorkspaceModel::SwitchTo(int id)
{
	WorkspaceSwitch result;
	result.changed = false;
	result.from = result.to = -1;
	if(id < 0 || id >= count || id == current)
		return result;

	result.changed = true;
	result.from = current;
	result.to = id;
	current = id;
	CheckInvariants();
	return result;
}

WorkspaceSwitch WorkspaceModel::SwitchToRelative(int step)
{
	// An empty model has no workspace to step away from, and -1 + step is not an
	// index into anything.
	if(count == 0)
		return SwitchTo(-1);
	return SwitchTo(current + step);
}

String WorkspaceModel::ToString() const
{
	return Format("workspaces %d, current %d", count, current);
}

END_UPP_NAMESPACE

#endif // _DesktopModel_DesktopModel_cpp_
