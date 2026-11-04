#ifndef _Explorer_ExplorerLog_h
#define _Explorer_ExplorerLog_h

#include <Core/Core.h>

NAMESPACE_UPP

// Channels used throughout Explorer. Keeping them in one place means the log view
// can offer a channel filter without every call site inventing its own strings.
namespace ExplorerChannels {
	static const char *MOUNT  = "mount";
	static const char *POLICY = "policy";
	static const char *MODEL  = "model";
	static const char *OPS    = "ops";
	static const char *GUI    = "gui";
}

// Logging for Explorer.
//
// Two sinks, deliberately:
//
//   - AppLog keeps structured records in memory. The debug-build logging window
//     renders them, and the headless twin prints the same records, so both paths
//     report identical information.
//   - LOG() mirrors every record into the rotating standard log file
//     (~/.local/state/<config group>/log/<AppName>.log on POSIX). LOG() is a no-op
//     in release builds, which is the intended behaviour for the file mirror.
//
// There is no bespoke logger: rotation and the path convention already come from
// StdLogSetup/StdLog.
struct ExplorerLog {
	AppLog&       Records()             { return records; }
	const AppLog& Records() const       { return records; }

	//< Points the standard log at a rotating file. Call once, early in startup.
	//< rotate_count of 0 disables rotation. Returns the resolved log path.
	static String SetupFile(int rotate_count = 3, int64 size_limit = 8 * 1024 * 1024);

	static String FilePath()            { return GetStdLogPath(); }

	void Trace(const char *channel, const String& msg) { Write(APPLOG_LEVEL_TRACE,   channel, msg); }
	void Debug(const char *channel, const String& msg) { Write(APPLOG_LEVEL_DEBUG,   channel, msg); }
	void Info (const char *channel, const String& msg) { Write(APPLOG_LEVEL_INFO,    channel, msg); }
	void Warn (const char *channel, const String& msg) { Write(APPLOG_LEVEL_WARNING, channel, msg); }
	void Error(const char *channel, const String& msg) { Write(APPLOG_LEVEL_ERROR,   channel, msg); }

	void Write(int level, const char *channel, const String& msg);

	static ExplorerLog& Get();

private:
	AppLog records;
};

#define ETRACE(ch, msg) Upp::ExplorerLog::Get().Trace(ch, msg)
#define EDEBUG(ch, msg) Upp::ExplorerLog::Get().Debug(ch, msg)
#define EINFO(ch, msg)  Upp::ExplorerLog::Get().Info(ch, msg)
#define EWARN(ch, msg)  Upp::ExplorerLog::Get().Warn(ch, msg)
#define EERROR(ch, msg) Upp::ExplorerLog::Get().Error(ch, msg)

END_UPP_NAMESPACE

#endif
