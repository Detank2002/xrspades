# A small, offline fallback when no system/vcpkg SPIRV-Cross core is available.
# Only reflection is used: no CLI, shader translators or upstream test suite.
add_library(zs_spirv_cross_core STATIC
	spirv_cross/spirv_cross.cpp
	spirv_cross/spirv_parser.cpp
	spirv_cross/spirv_cross_parsed_ir.cpp
	spirv_cross/spirv_cfg.cpp)
target_include_directories(zs_spirv_cross_core SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(zs_spirv_cross_core PROPERTIES
	CXX_STANDARD 11
	CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON
	FOLDER libs)
