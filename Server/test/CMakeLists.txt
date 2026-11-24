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

# Existing font holders must update in place, not retain freed FreeType faces
# or the first selected fallback atlas. Uses the game's bundled font fixtures.
if(NOT FREETYPE_LIBRARIES)
	find_package(Freetype REQUIRED)
endif()
find_package(Threads REQUIRED)
zs_test_executable(font_reload_test
	"${CMAKE_CURRENT_SOURCE_DIR}/font_reload_test.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/skin_core_test_support.cpp"
	"${ZS_TEST_SOURCES}/Client/Fonts.cpp"
	"${ZS_TEST_SOURCES}/Client/FTFont.cpp"
	"${ZS_TEST_SOURCES}/Client/IFont.cpp"
	"${ZS_TEST_SOURCES}/Client/Quake3Font.cpp"
	"${ZS_TEST_SOURCES}/Client/IRenderer.cpp"
	"${ZS_TEST_SOURCES}/Core/Bitmap.cpp"
	"${ZS_TEST_SOURCES}/Core/IBitmapCodec.cpp"
	"${ZS_TEST_SOURCES}/Core/FileManager.cpp"
	"${ZS_TEST_SOURCES}/Core/MemoryStream.cpp"
	"${ZS_TEST_SOURCES}/Core/IStream.cpp"
	"${ZS_TEST_SOURCES}/Core/RefCountedObject.cpp"
	"${ZS_TEST_SOURCES}/Core/ThreadLocalStorage.cpp"
	"${ZS_TEST_SOURCES}/Core/Math.cpp"
	"${ZS_TEST_SOURCES}/Core/CP437.cpp")
target_compile_definitions(font_reload_test PRIVATE
	ZS_TEST_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/Resources")
target_include_directories(font_reload_test PRIVATE ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(font_reload_test PRIVATE ${FREETYPE_LIBRARIES} Threads::Threads)
add_test(NAME font_reload COMMAND font_reload_test)

# Exercise actual shader reflection even without a window/GPU. A Vulkan build
# must never substitute empty layouts for the title screen's water shaders.
set(ZS_VULKAN_TEST_TARGETS)
if(USE_VULKAN)
	set(ZS_TEST_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/water-shaders")
	file(GLOB_RECURSE ZS_WATER_INCLUDES "${CMAKE_SOURCE_DIR}/Resources/Shaders/Vulkan/*.glsl")
	set(ZS_WATER_SPIRV)
	foreach(SHADER Water.vk.vs Water2.vk.vs Water3.vk.vs
	               Water.vk.fs WaterInPass.vk.fs Water2.vk.fs Water3.vk.fs)
		set(SHADER_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Shaders/Vulkan/${SHADER}")
		if(SHADER MATCHES "\\.vs$")
			set(SHADER_STAGE vert)
		else()
			set(SHADER_STAGE frag)
		endif()
		foreach(VARIANT flat multiview)
			set(SHADER_FLAGS)
			set(SHADER_SUFFIX .spv)
			if(VARIANT STREQUAL multiview)
				set(SHADER_FLAGS -DZS_MULTIVIEW=1)
				set(SHADER_SUFFIX .mv.spv)
			endif()
			set(SHADER_OUTPUT "${ZS_TEST_SHADER_DIR}/${SHADER}${SHADER_SUFFIX}")
			add_custom_command(OUTPUT "${SHADER_OUTPUT}"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${ZS_TEST_SHADER_DIR}"
				COMMAND "${GLSLANG_VALIDATOR}" -V -S ${SHADER_STAGE} ${SHADER_FLAGS}
				        -o "${SHADER_OUTPUT}" "${SHADER_SOURCE}"
				DEPENDS "${SHADER_SOURCE}" ${ZS_WATER_INCLUDES}
				VERBATIM)
			list(APPEND ZS_WATER_SPIRV "${SHADER_OUTPUT}")
		endforeach()
	endforeach()
	add_custom_target(zs_vulkan_test_shaders DEPENDS ${ZS_WATER_SPIRV})
	zs_test_executable(vulkan_reflection_test "${CMAKE_CURRENT_SOURCE_DIR}/vulkan_reflection_test.cpp")
	add_dependencies(vulkan_reflection_test zs_vulkan_test_shaders)
	target_compile_definitions(vulkan_reflection_test PRIVATE ZS_TEST_SHADER_DIR="${ZS_TEST_SHADER_DIR}")
	target_include_directories(vulkan_reflection_test PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
	target_link_libraries(vulkan_reflection_test PRIVATE ${SPIRV_CROSS_LIBRARIES})
	add_test(NAME vulkan_reflection COMMAND vulkan_reflection_test)
	list(APPEND ZS_VULKAN_TEST_TARGETS vulkan_reflection_test)
endif()

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
	server_extension_key_test skin_selection_test voxel_model_preview_test skin_overlay_test
	font_reload_test ${ZS_VULKAN_TEST_TARGETS})
