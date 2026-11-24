# SPIRV-Cross core (fallback)

Unmodified core reflection sources from
[KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross), tag
`vulkan-sdk-1.4.309.0`, commit
`2c32b6bf86f3c4a5539aa1f0bacbd59fe61759cf`.

Only the files in upstream's `spirv-cross-core-sources` list are included.
Licenses and copyright notices are retained in the sources, `LICENSE` and
`LICENSES/`. This directory's CMakeLists builds those four translation units
without the upstream CLI, translators, install rules or tests.

The Vulkan build prefers an installed/vcpkg SPIRV-Cross core. If one is not
available, it uses this copy, without a network download or extra package
installation. Reflection is **required**, not optional: descriptor set layouts
and push constant ranges must match the compiled shaders. Previously, a
successful build without reflection could crash in the driver when the title
screen's water renderer updated bindings missing from an empty layout.
