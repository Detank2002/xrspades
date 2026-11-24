// Only singleton registration plumbing is replaced. The recorder, real
// AngelScript VM/math, voxel loader and rasterizer are production code. Never
// initialize the live game's ScriptManager in a preview test.
#include <ScriptBindings/ScriptManager.h>
#include <stdexcept>

namespace spades {
	ScriptObjectRegistrar::ScriptObjectRegistrar(const std::string& name) : name(name) {}
	void ScriptManager::CheckError(int code) {
		if (code < 0)
			throw std::runtime_error("Script registration error " + std::to_string(code));
	}
} // namespace spades
