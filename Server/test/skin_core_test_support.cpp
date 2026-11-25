// Minimal diagnostic runtime for the headless skin/voxel tests. The real voxel,
// stream, filesystem and reference-counting implementations are linked; only
// logging/backtraces (which otherwise require SDL) are replaced here.

#include <cstdarg>
#include <cstdio>

#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace reflection {
		BacktraceEntryAdder::BacktraceEntryAdder(const BacktraceEntry&) : bt(nullptr) {}
		BacktraceEntryAdder::~BacktraceEntryAdder() = default;
	} // namespace reflection

	Exception::Exception(const char* format, ...) {
		char buffer[4096];
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		shortMessage = message = buffer;
	}
	Exception::Exception(const char*, int, const char* format, ...) {
		char buffer[4096];
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		shortMessage = message = buffer;
	}
	Exception::~Exception() noexcept = default;
	const char* Exception::what() const noexcept {
		return message.c_str();
	}
	void LogMessage(const char*, int, const char*, ...) {}
} // namespace spades
