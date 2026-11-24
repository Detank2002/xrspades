# The client's headless tests and the pose fixture, built on request
# (`cmake --build . --target zs_tests`, which also builds the training server's)
# and run by CTest. Each compiles the one client source it checks, so none of
# them needs the game, a GPU or an OpenXR runtime.

set(ZS_TEST_SOURCES "${CMAKE_SOURCE_DIR}/Sources")

function(zs_test_executable name)
	add_executable(${name} EXCLUDE_FROM_ALL ${ARGN})
	target_include_directories(${name} PRIVATE "${ZS_TEST_SOURCES}")
	set_target_properties(${name} PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		FOLDER tests)
	if(WIN32)
		target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
			_CRT_SECURE_NO_WARNINGS)
	endif()
endfunction()

zs_test_executable(vr_state_test
	"${CMAKE_CURRENT_SOURCE_DIR}/vr_state_test.cpp"
	"${ZS_TEST_SOURCES}/Client/VRState.cpp")
add_test(NAME vr_state COMMAND vr_state_test)

zs_test_executable(xr_caps_test
	"${CMAKE_CURRENT_SOURCE_DIR}/xr_caps_test.cpp"
	"${ZS_TEST_SOURCES}/VR/XrCaps.cpp")
add_test(NAME xr_caps COMMAND xr_caps_test)

zs_test_executable(vr_pose_fixture
	"${CMAKE_CURRENT_SOURCE_DIR}/vr_pose_fixture.cpp"
	"${ZS_TEST_SOURCES}/Client/VRState.cpp")
add_test(NAME vr_pose_roundtrip COMMAND vr_pose_fixture)

# The self-updater's checks: SHA-256, the signed SHA256SUMS, package paths
zs_test_executable(update_package_test
	"${CMAKE_CURRENT_SOURCE_DIR}/update_package_test.cpp"
	"${ZS_TEST_SOURCES}/Core/Sha256.cpp"
	"${ZS_TEST_SOURCES}/Gui/UpdatePackage.cpp"
	"${ZS_TEST_SOURCES}/monocypher/monocypher.c"
	"${ZS_TEST_SOURCES}/monocypher/monocypher-ed25519.c")
add_test(NAME update_package COMMAND update_package_test)

# The key a server's extension list is remembered under
zs_test_executable(server_extension_key_test
	"${CMAKE_CURRENT_SOURCE_DIR}/server_extension_key_test.cpp"
	"${ZS_TEST_SOURCES}/Client/ServerExtensionKey.cpp")
add_test(NAME server_extension_key COMMAND server_extension_key_test)

# The enabled collection vs. equipped skin, and the backend-independent 3D
# thumbnail mesh. No SDL window, GPU, game world or script engine is needed.
zs_test_executable(skin_selection_test
	"${CMAKE_CURRENT_SOURCE_DIR}/skin_selection_test.cpp"
	"${ZS_TEST_SOURCES}/Gui/SkinSelection.cpp")
add_test(NAME skin_selection COMMAND skin_selection_test)

zs_test_executable(voxel_model_preview_test
	"${CMAKE_CURRENT_SOURCE_DIR}/voxel_model_preview_test.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/skin_core_test_support.cpp"
	"${ZS_TEST_SOURCES}/Gui/UI/Widgets/VoxelModelPreview.cpp"
	"${ZS_TEST_SOURCES}/Client/IRenderer.cpp"
	"${ZS_TEST_SOURCES}/Core/VoxelModel.cpp"
	"${ZS_TEST_SOURCES}/Core/MemoryStream.cpp"
	"${ZS_TEST_SOURCES}/Core/IStream.cpp"
	"${ZS_TEST_SOURCES}/Core/RefCountedObject.cpp")
target_compile_definitions(voxel_model_preview_test PRIVATE
	ZS_TEST_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/Resources")
add_test(NAME voxel_model_preview COMMAND voxel_model_preview_test)

# Exercise real pak mounting, independent previews and failed-selection rollback
# with headless devices and a script-build failure marker.
find_package(ZLIB REQUIRED)
zs_test_executable(skin_overlay_test
	"${CMAKE_CURRENT_SOURCE_DIR}/skin_overlay_test.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/skin_overlay_test_runtime.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/skin_core_test_support.cpp"
	"${ZS_TEST_SOURCES}/Gui/ModOverlay.cpp"
	"${ZS_TEST_SOURCES}/Gui/SkinCatalog.cpp"
	"${ZS_TEST_SOURCES}/Gui/SkinSelection.cpp"
	"${ZS_TEST_SOURCES}/Core/FileManager.cpp"
	"${ZS_TEST_SOURCES}/Core/ZipFileSystem.cpp"
	"${ZS_TEST_SOURCES}/Core/MemoryStream.cpp"
	"${ZS_TEST_SOURCES}/Core/DynamicMemoryStream.cpp"
	"${ZS_TEST_SOURCES}/Core/IStream.cpp"
	"${ZS_TEST_SOURCES}/Core/StdStream.cpp"
	"${ZS_TEST_SOURCES}/Core/VoxelModel.cpp"
	"${ZS_TEST_SOURCES}/Core/RefCountedObject.cpp"
	"${ZS_TEST_SOURCES}/Core/ResourceChanges.cpp"
	"${ZS_TEST_SOURCES}/Client/IRenderer.cpp"
	"${ZS_TEST_SOURCES}/unzip/unzip.c"
	"${ZS_TEST_SOURCES}/unzip/ioapi.c")
target_link_libraries(skin_overlay_test PRIVATE ZLIB::ZLIB)
add_test(NAME skin_overlay COMMAND skin_overlay_test)

# A VR player's pose, as a client without a headset decodes it (what a macOS
# build draws other players' VR from)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
	add_test(NAME vr_puppet_pose
		COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/test_vr_puppet.py"
		        $<TARGET_FILE:vr_pose_fixture>)
endif()

add_custom_target(zs_tests DEPENDS
	vr_state_test xr_caps_test vr_pose_fixture update_package_test training_server_test
	server_extension_key_test skin_selection_test voxel_model_preview_test skin_overlay_test)
