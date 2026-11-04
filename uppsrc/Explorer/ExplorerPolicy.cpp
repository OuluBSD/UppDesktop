#include "ExplorerPolicy.h"

NAMESPACE_UPP

const char *ExplorerPolicy::ActionName(Action a)
{
	switch(a) {
	case CREATE_DIR:  return "create-dir";
	case CREATE_FILE: return "create-file";
	case DELETE:      return "delete";
	case RENAME:      return "rename";
	case WRITE:       return "write";
	case CHMOD:       return "chmod";
	case MOUNT:       return "mount";
	case UNMOUNT:     return "unmount";
	case EXECUTE:     return "execute";
	}
	return "?";
}

const char *ExplorerPolicy::TrustName(Trust t)
{
	switch(t) {
	case READ:         return "read";
	case USER_SCOPE:   return "user";
	case SYSTEM_SCOPE: return "system";
	case DESTRUCTIVE:  return "destructive";
	}
	return "?";
}

String ExplorerPolicy::Request::ToString() const
{
	String s = ActionName(action);
	s << ' ' << TrustName(trust) << ' ' << path;
	if(!purpose.IsEmpty()) s << " (" << purpose << ")";
	return s;
}

ExplorerPolicy::ExplorerPolicy()
{
	static PlatformPolicyAdapter platform;
	adapter = &platform;
}

ExplorerPolicy::Trust ExplorerPolicy::Classify(const String& path) const
{
	if(path.IsEmpty())
		return READ;
	if(adapter && adapter->IsSystemPath(path))
		return SYSTEM_SCOPE;
	return USER_SCOPE;
}

ExplorerPolicy::Decision ExplorerPolicy::Evaluate(const Request& req, String& reason) const
{
	reason.Clear();

	if(req.trust == READ)
		return ALLOW;

	// Fail closed: no adapter, or one that cannot answer, means no permission.
	if(!adapter || !adapter->CanDetermine()) {
		reason = String("no usable ") + (adapter ? adapter->Name() : String("null"))
		       + " policy adapter: cannot determine authorization";
		return DENY;
	}

	// Extra platform restrictions beyond elevation (SELinux labels, immutable
	// flags, and so on) are checked first: they can refuse even a privileged process.
	String restriction = adapter->PathRestriction(req.path);
	if(!restriction.IsEmpty()) {
		reason = restriction;
		return DENY;
	}

	if(req.trust == SYSTEM_SCOPE && !adapter->IsElevated()) {
		// This is a CONFIRM, not an allow: the platform may still be able to obtain
		// authority through its own mechanism, which the GUI can trigger.
		reason = String("system location requires elevation via ") + adapter->ElevationMechanism();
		return CONFIRM;
	}

	if(req.trust == DESTRUCTIVE)
		return CONFIRM;

	return ALLOW;
}

bool ExplorerPolicy::Authorize(const Request& req, String& error)
{
	Decision d = Evaluate(req, error);

	if(d == ALLOW) {
		EDEBUG(ExplorerChannels::POLICY, "allow " << req.ToString());
		return true;
	}

	if(d == CONFIRM) {
		if(confirm) {
			bool ok = confirm(req, error);
			if(ok) {
				EDEBUG(ExplorerChannels::POLICY, "confirmed " << req.ToString() << " -- " << error);
				return true;
			}
			error = "refused by the user: " + error;
			EWARN(ExplorerChannels::POLICY, "deny " << req.ToString() << " -- " << error);
			return false;
		}
		// No way to ask (headless, or no GUI wired up): fail closed.
		error += " (no confirmation channel available, failing closed)";
		EWARN(ExplorerChannels::POLICY, "deny " << req.ToString() << " -- " << error);
		return false;
	}

	EWARN(ExplorerChannels::POLICY, "deny " << req.ToString() << " -- " << error);
	return false;
}

String ExplorerPolicy::Describe() const
{
	String s = "adapter: ";
	s << AdapterName() << "\n";
	s << "  can determine: " << (adapter && adapter->CanDetermine() ? "yes" : "no") << "\n";
	s << "  elevated: " << (adapter && adapter->IsElevated() ? "yes" : "no") << "\n";
	s << "  elevation via: " << (adapter ? adapter->ElevationMechanism() : String("none")) << "\n";
	s << "  confirmation channel: " << (confirm ? "wired" : "none (confirm resolves to deny)");
	return s;
}

// ---------------------------------------------------------------------------
// Platform adapters
// ---------------------------------------------------------------------------

#ifdef PLATFORM_LINUX

// Linux: uid-based elevation plus SELinux when it is present on the system.
// SELinux is probed through /sys/fs/selinux/enforce, which needs no libselinux
// link dependency and reflects the running kernel's actual enforcement state.
bool PlatformPolicyAdapter::CanDetermine() const
{
	return true;
}

bool PlatformPolicyAdapter::IsElevated() const
{
	return geteuid() == 0;
}

String PlatformPolicyAdapter::ElevationMechanism() const
{
	return "polkit / sudo (pkexec)";
}

String PlatformPolicyAdapter::PathRestriction(const String& path) const
{
	String enforce = "/sys/fs/selinux/enforce";
	if(FileExists(enforce)) {
		// Enforcing mode means a label denial can also come from policy even for
		// root, so surface the state instead of pretending elevation suffices.
		String state = LoadFile(enforce);
		if(state[0] == '1')
			return String("SELinux is enforcing: '") + path
			     + "' may still be denied by policy after elevation";
	}
	return String();
}

bool PlatformPolicyAdapter::IsSystemPath(const String& path) const
{
	static const char *system_prefixes[] = {
		"/bin", "/boot", "/dev", "/etc", "/lib", "/lib32", "/lib64", "/libx32",
		"/proc", "/root", "/run", "/sbin", "/srv", "/sys", "/usr", "/var/lib",
		"/var/spool", nullptr
	};
	for(const char **p = system_prefixes; *p; p++) {
		String prefix = *p;
		if(path == prefix)
			return true;
		if(path.StartsWith(prefix) && path.GetCount() > prefix.GetCount()
		   && path[prefix.GetCount()] == '/')
			return true;
	}
	return false;
}

String PlatformPolicyAdapter::Name() const { return "linux"; }

#elif defined(PLATFORM_WIN32)

// Windows: elevation means administrator token membership. UAC consent itself is
// a shell verb, so a refusal is reported as needing UAC rather than as a hard deny.
bool PlatformPolicyAdapter::CanDetermine() const
{
	return true;
}

bool PlatformPolicyAdapter::IsElevated() const
{
	// CheckTokenMembership against the built-in Administrators group. Reported as a
	// best-effort probe: a false result is treated as "not elevated", which the
	// caller turns into a confirmation request, never into an allow.
	return IsUserAnAdmin() != 0;
}

String PlatformPolicyAdapter::ElevationMechanism() const
{
	return "UAC elevation (runas)";
}

String PlatformPolicyAdapter::PathRestriction(const String& path) const
{
	// Read-only system locations cannot be written at all, elevated or not.
	String p = path;
	p = p.MakeLower();
	if(p.StartsWith("c:\\windows\\system32\\config") || p.StartsWith("c:\\$recycle.bin"))
		return String("'") + path + "' is protected by Windows and cannot be modified";
	return String();
}

bool PlatformPolicyAdapter::IsSystemPath(const String& path) const
{
	String p = path;
	p = p.MakeLower();
	static const char *roots[] = {
		"c:\\windows", "c:\\program files", "c:\\program files (x86)",
		"c:\\programdata", nullptr
	};
	for(const char **r = roots; *r; r++) {
		String root = *r;
		if(p == root || (p.StartsWith(root) && p.GetCount() > root.GetCount() && p[root.GetCount()] == '\\'))
			return true;
	}
	return false;
}

String PlatformPolicyAdapter::Name() const { return "windows"; }

#elif defined(PLATFORM_OSX)

// macOS: elevation is requested through osascript's administrator privileges,
// which is the mechanism a user actually sees. System paths are judged by the
// /System, /Library and /Applications roots.
bool PlatformPolicyAdapter::CanDetermine() const
{
	return true;
}

bool PlatformPolicyAdapter::IsElevated() const
{
	return geteuid() == 0;
}

String PlatformPolicyAdapter::ElevationMechanism() const
{
	return "osascript administrator privileges";
}

String PlatformPolicyAdapter::PathRestriction(const String& path) const
{
	// SIP protects these subtrees even for root.
	if(path.StartsWith("/System") || path.StartsWith("/usr/bin") || path.StartsWith("/bin"))
		return String("'") + path + "' is protected by System Integrity Protection";
	return String();
}

bool PlatformPolicyAdapter::IsSystemPath(const String& path) const
{
	static const char *roots[] = { "/System", "/Library", "/Applications", "/usr", "/bin", "/sbin", nullptr };
	for(const char **r = roots; *r; r++) {
		String root = *r;
		if(path == root)
			return true;
		if(path.StartsWith(root) && path.GetCount() > root.GetCount() && path[root.GetCount()] == '/')
			return true;
	}
	return false;
}

String PlatformPolicyAdapter::Name() const { return "macos"; }

#else

// No adapter for this platform: CanDetermine() is false, so every mutation is
// denied. That is the fail-closed default, and it is the correct behaviour for a
// platform nobody has thought about yet -- better than allowing everything.
bool PlatformPolicyAdapter::CanDetermine() const { return false; }
bool PlatformPolicyAdapter::IsElevated() const    { return false; }
String PlatformPolicyAdapter::ElevationMechanism() const { return "unsupported on this platform"; }
String PlatformPolicyAdapter::PathRestriction(const String&) const { return String(); }
String PlatformPolicyAdapter::Name() const { return "unsupported"; }

bool PlatformPolicyAdapter::IsSystemPath(const String&) const { return true; }

#endif

ExplorerPolicy& GetExplorerPolicy()
{
	static ExplorerPolicy policy;
	return policy;
}

END_UPP_NAMESPACE
