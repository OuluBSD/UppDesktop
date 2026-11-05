#ifndef _Explorer_ExplorerPolicy_h
#define _Explorer_ExplorerPolicy_h

#include "ExplorerLog.h"

#ifdef DELETE
#undef DELETE
#endif

NAMESPACE_UPP

// Authorization for mutations Explorer performs.
//
// Explorer's browsing goes straight through MountManager, but anything that
// changes state -- including creating directories under system paths -- goes
// through this service first. The Vfs layer has no opinion about permissions, and
// must not grow one: policy belongs to the application, not to the filesystem
// abstraction.
//
// Design rules, in order of importance:
//
//   1. Fail closed. If the platform cannot be consulted, or the answer is
//      ambiguous, the operation is denied. An unknown answer is never an allow.
//   2. Never fall back to raw system calls to "get around" a denial. A denied
//      request stays denied, and the reason is logged.
//   3. Elevation is a platform concept, not a boolean. Linux has uid plus SELinux,
//      Windows has UAC integrity levels, macOS has its own authorisation, Android
//      has per-app sandbox grants. Adapters model that; the core does not flatten
//      it into "am I root".
struct ExplorerPolicy {
	enum Action {
		CREATE_DIR,
		CREATE_FILE,
		CREATE_SYMLINK,
		DELETE,
		RENAME,
		WRITE,
		CHMOD,
		MOUNT,
		UNMOUNT,
		EXECUTE
	};

	enum Decision {
		ALLOW,              //< permitted outright
		CONFIRM,            //< permitted if the user agrees (GUI only)
		DENY                //< refused
	};

	enum Trust {
		READ,               //< inspecting, no policy involved
		USER_SCOPE,         //< inside the user's own data
		SYSTEM_SCOPE,       //< touches a system location
		DESTRUCTIVE         //< removes or overwrites existing data
	};

	struct Request {
		Action action = CREATE_DIR;
		Trust  trust  = USER_SCOPE;
		String path;
		String purpose;

		Request(Action a, String p, Trust t = USER_SCOPE, String why = String())
		    : action(a), trust(t), path(pick(p)), purpose(pick(why)) {}

		String ToString() const;
	};

	// Platform-specific answer provider.
	struct Adapter {
		virtual ~Adapter() {}
		virtual String Name() const = 0;
		//< Whether this adapter can actually answer. A false here is a deny-by-
		//< default, not an "allow and hope".
		virtual bool   CanDetermine() const = 0;
		//< Whether the process currently holds elevated/administrative rights.
		virtual bool   IsElevated() const = 0;
		//< Extra restrictions beyond elevation, e.g. SELinux labels or immutable
		//< flags. Empty when there is nothing extra to say.
		virtual String PathRestriction(const String& path) const = 0;
		//< How the platform asks the user for authority, for prompts and logs.
		virtual String ElevationMechanism() const = 0;
		//< Whether the path itself is a system location by policy.
		virtual bool   IsSystemPath(const String& path) const = 0;
	};

	// Asks the user. Set by the GUI; when unset, CONFIRM becomes DENY, which is
	// what makes the headless twin fail closed rather than prompt invisibly.
	Function<bool(const Request&, const String& why)> confirm;

	ExplorerPolicy();

	Adapter& GetAdapter()                        { return *adapter; }
	void     SetAdapter(Adapter& a)              { adapter = &a; }
	String   AdapterName() const                { return adapter ? adapter->Name() : String("none"); }

	//< Classifies a path as user scope or system scope.
	Trust    Classify(const String& path) const;

	//< Full decision, with the reason always filled in.
	Decision Evaluate(const Request& req, String& reason) const;

	//< Convenience: true only when Evaluate returned ALLOW, or CONFIRM and the
	//< confirmation callback agreed. Never throws away a denial.
	bool     Authorize(const Request& req, String& error);

	//< Human-readable one-liner describing the effective policy, for the log view
	//< and the --explain output of the headless twin.
	String   Describe() const;

	static const char *ActionName(Action a);
	static const char *TrustName(Trust t);

private:
	Adapter* adapter;
};

// The adapter for the platform this build targets.
struct PlatformPolicyAdapter : ExplorerPolicy::Adapter {
	String Name() const override;
	bool   CanDetermine() const override;
	bool   IsElevated() const override;
	String PathRestriction(const String& path) const override;
	String ElevationMechanism() const override;
	bool   IsSystemPath(const String& path) const override;
};

// Returns the process-wide policy service. The GUI installs a confirmation
// callback on it; the headless twin leaves it unset so that CONFIRM resolves to
// DENY.
ExplorerPolicy& GetExplorerPolicy();

END_UPP_NAMESPACE

#endif
