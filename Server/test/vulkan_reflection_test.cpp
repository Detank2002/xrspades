// Reflect the actual compiled water shaders. No GPU, SDL or mock SPIR-V.
// A Vulkan build without reflection used to create empty descriptor layouts,
// then write bindings 2/3/5 on the first title-screen frame (a driver crash).

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spirv_cross/spirv_cross.hpp>

namespace {
	void Check(bool ok, const std::string& what) {
		if (!ok)
			throw std::runtime_error(what);
	}

	spirv_cross::Compiler Load(const std::string& name) {
		const auto path = std::string(ZS_TEST_SHADER_DIR) + "/" + name;
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		Check(f.good(), "compiled shader is present: " + path);
		const auto size = static_cast<std::size_t>(f.tellg());
		Check(size >= 20 && size % 4 == 0, "valid SPIR-V size: " + name);
		std::vector<std::uint32_t> words(size / 4);
		f.seekg(0);
		f.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
		Check(f.good() && words[0] == 0x07230203, "valid SPIR-V header: " + name);
		return spirv_cross::Compiler(std::move(words));
	}

	void Vertex(const std::string& file) {
		auto compiler = Load(file);
		const auto resources = compiler.get_shader_resources();
		bool matrices = false;
		for (const auto& ubo : resources.uniform_buffers) {
			if (compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet) != 0)
				continue;
			Check(compiler.get_decoration(ubo.id, spv::DecorationBinding) == 5,
			      "water matrices binding 5: " + file);
			const auto bytes = compiler.get_declared_struct_size(compiler.get_type(ubo.type_id));
			Check((bytes + 15) / 16 * 16 == 368,
			      "water matrices match the CPU's std140-rounded 368-byte allocation: " + file);
			matrices = true;
		}
		Check(matrices, "water vertex shader must reflect its matrices: " + file);
	}

	void Fragment(const std::string& file, const std::set<std::uint32_t>& expected) {
		auto compiler = Load(file);
		const auto resources = compiler.get_shader_resources();
		std::set<std::uint32_t> bindings;
		for (const auto& image : resources.sampled_images)
			if (compiler.get_decoration(image.id, spv::DecorationDescriptorSet) == 0)
				bindings.insert(compiler.get_decoration(image.id, spv::DecorationBinding));
		Check(bindings == expected, "water's descriptor layout must include all textures: " + file);
		Check(resources.push_constant_buffers.size() == 1,
		      "water must reflect its push constants: " + file);
		const auto& pc = resources.push_constant_buffers.front();
		Check(compiler.get_declared_struct_size(compiler.get_type(pc.type_id)) == 128,
		      "water push constants match the portable CPU range: " + file);
	}
} // namespace

int main() {
	try {
		for (const std::string suffix : {".spv", ".mv.spv"}) {
			for (const std::string vertex : {"Water.vk.vs", "Water2.vk.vs", "Water3.vk.vs"})
				Vertex(vertex + suffix);
			Fragment("Water.vk.fs" + suffix, {0, 1, 2, 3});
			Fragment("WaterInPass.vk.fs" + suffix, {2, 3});
			Fragment("Water2.vk.fs" + suffix, {0, 1, 2, 6, 8});
			Fragment("Water3.vk.fs" + suffix, {0, 1, 2, 6, 7, 8});
		}
		std::puts("vulkan_reflection_test: all passed");
		return 0;
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "FAIL: %s\n", ex.what());
		return 1;
	}
}
