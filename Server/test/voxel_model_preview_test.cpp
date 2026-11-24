// Checks the actual voxel mesh/projection without a GPU or a game world.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include <Core/MemoryStream.h>
#include <Core/VoxelModel.h>
#include <Gui/UI/Widgets/VoxelModelPreview.h>

using namespace spades;
using spades::gui::ui::VoxelModelPreview;

namespace {
	int failures = 0;
	void Check(bool ok, const char* what) {
		if (!ok) {
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}
	void CheckRotation(const VoxelModelPreview& preview) {
		const AABB2 bounds(23, 47, 200, 108);
		for (int i = 0; i < 96; ++i) {
			const auto quads = preview.Project(bounds, float(i) * kTwoPi / 96);
			Check(!quads.empty(), "model stays visible throughout rotation");
			float lastDepth = -1e30F;
			for (const auto& q : quads) {
				Check(q.depth >= lastDepth, "faces are sorted from back to front");
				lastDepth = q.depth;
				for (const auto& p : {q.origin, q.origin + q.axisU, q.origin + q.axisV,
				                      q.origin + q.axisU + q.axisV})
					Check(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= bounds.min.x &&
					        p.x <= bounds.max.x && p.y >= bounds.min.y && p.y <= bounds.max.y,
					      "all faces stay inside their card");
			}
		}
	}
} // namespace

int main() {
	auto empty = Handle<VoxelModel>::New(1, 1, 1);
	VoxelModelPreview blank(*empty);
	Check(blank.IsEmpty() && blank.Project(AABB2(0, 0, 100, 100), 0).empty(),
	      "empty models are safe");
	auto cube = Handle<VoxelModel>::New(1, 1, 1);
	cube->SetSolid(0, 0, 0, 0x0000FF);
	VoxelModelPreview single(*cube);
	Check(single.GetFaceCount() == 6, "one voxel has six exposed faces");
	auto quads = single.Project(AABB2(0, 0, 100, 100), 0.8F);
	Check(quads.size() == 3, "backfaces are culled");
	for (const auto& q : quads)
		Check(q.color.x > 0 && q.color.y == 0 && q.color.z == 0, "voxel RGB order is preserved");
	Check(single.Project(AABB2(0, 0, 0, 100), 0).empty(), "zero-sized projection is safe");
	CheckRotation(single);

	auto pair = Handle<VoxelModel>::New(2, 1, 1);
	pair->SetSolid(0, 0, 0, 0xEEEEEE);
	pair->SetSolid(1, 0, 0, 0x444444);
	VoxelModelPreview adjacent(*pair);
	Check(adjacent.GetFaceCount() == 10, "interior faces are not meshed");

	auto tall = Handle<VoxelModel>::New(10, 10, 64);
	for (int z = 2; z < 62; ++z)
		tall->SetSolid(8, 7, z, 0xAAAAAA);
	tall->SetOrigin(MakeVector3(10000, -10000, 500));
	CheckRotation(VoxelModelPreview(*tall));
	auto longGun = Handle<VoxelModel>::New(4, 180, 12);
	for (int y = 10; y < 170; ++y)
		longGun->SetSolid(2, y, 5, 0xAAAAAA);
	VoxelModelPreview horizontal(*longGun);
	CheckRotation(horizontal);
	const auto first = horizontal.Project(AABB2(0, 0, 200, 120), 0);
	const auto turned = horizontal.Project(AABB2(0, 0, 200, 120), 1);
	Check(std::fabs(first.front().origin.x - turned.front().origin.x) > 1,
	      "yaw actually rotates the geometry");

	auto detailed = Handle<VoxelModel>::New(96, 96, 32);
	for (int x = 0; x < 96; ++x)
		for (int y = 0; y < 96; ++y)
			for (int z = 0; z < 32; ++z)
				if ((x + y + z) % 2 == 0)
					detailed->SetSolid(x, y, z, 0xFFFFFF);
	VoxelModelPreview bounded(*detailed);
	Check(!bounded.IsEmpty() && bounded.GetFaceCount() <= 20000,
	      "custom model complexity is bounded");

#ifdef ZS_TEST_RESOURCE_DIR
	for (const char* model :
	     {"Spade/Spade", "Grenade/Grenade", "Rifle/Weapon", "SMG/Weapon", "Shotgun/Weapon"}) {
		const std::string path =
		  std::string(ZS_TEST_RESOURCE_DIR) + "/Models/Weapons/" + model + ".kv6";
		std::ifstream file(path, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(file)),
		                        std::istreambuf_iterator<char>());
		MemoryStream stream(bytes.data(), bytes.size());
		auto voxels = VoxelModel::LoadKV6(stream);
		VoxelModelPreview preview(*voxels);
		Check(!preview.IsEmpty(), "shipped weapon has a preview");
		CheckRotation(preview);
	}
#endif
	std::printf("voxel_model_preview_test: %s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
