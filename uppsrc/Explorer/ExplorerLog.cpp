#include "ExplorerLog.h"

NAMESPACE_UPP

String ExplorerLog::SetupFile(int rotate_count, int64 size_limit)
{
	dword options = LOG_FILE | LOG_TIMESTAMP | LOG_ELAPSED;
	if(rotate_count > 0)
		options |= LOG_ROTATE(rotate_count);
	StdLogSetup(options, nullptr, int(size_limit));
	String path = GetStdLogPath();
	return path;
}

void ExplorerLog::Write(int level, const char *channel, const String& msg)
{
	records.Add(level, channel, msg, __FILE__, __LINE__, __func__);

	// Mirror into the rotating file. LOG() compiles away in release builds.
	// The file mirror is deliberately terse: the in-memory records carry the
	// structured detail that the log window renders.
	switch(level) {
	case APPLOG_LEVEL_ERROR:
		LOG("ERROR [" << channel << "] " << msg);
		break;
	case APPLOG_LEVEL_WARNING:
		LOG("WARN  [" << channel << "] " << msg);
		break;
	case APPLOG_LEVEL_INFO:
		LOG("INFO  [" << channel << "] " << msg);
		break;
	case APPLOG_LEVEL_DEBUG:
	case APPLOG_LEVEL_TRACE:
		LOG("DEBUG [" << channel << "] " << msg);
		break;
	}
}

ExplorerLog& ExplorerLog::Get()
{
	static ExplorerLog log;
	return log;
}

END_UPP_NAMESPACE
