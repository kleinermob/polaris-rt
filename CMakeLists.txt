cmake_minimum_required(VERSION 3.20)
project(polaris_rt_layer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Vulkan
if(NOT Vulkan_ROOT)
    set(Vulkan_ROOT "C:/VulkanSDK/1.4.341.1" CACHE PATH "Vulkan SDK path")
endif()

find_package(Vulkan REQUIRED)
if(NOT Vulkan_INCLUDE_DIRS)
  message(STATUS "Falling back to Vulkan_ROOT include path: ${Vulkan_ROOT}/Include")
  set(Vulkan_INCLUDE_DIRS "${Vulkan_ROOT}/Include")
endif()

message(STATUS "Vulkan include: ${Vulkan_INCLUDE_DIRS}")

# Layer source
set(LAYER_SOURCES layer/layer.cpp)
set(LAYER_DEF layer/polaris_rt_layer.def)

# Build layer as DLL
add_library(polaris_rt_layer SHARED ${LAYER_SOURCES} ${LAYER_DEF})

# ── FIX: link CRT statically so the DLL has no Clang runtime dependency ──────
# Without this, the 1.3+ Vulkan loader silently fails LoadLibrary because
# clang_rt / MSVC CRT DLLs are not next to the layer or in the system PATH.
set_property(TARGET polaris_rt_layer PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# ─────────────────────────────────────────────────────────────────────────────

target_include_directories(polaris_rt_layer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layer
    ${Vulkan_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}/vulkan/utility
)

target_compile_definitions(polaris_rt_layer PRIVATE
    _CRT_SECURE_NO_WARNINGS
    VK_ENABLE_BETA_EXTENSIONS
)

target_link_libraries(polaris_rt_layer PRIVATE
    ${Vulkan_LIBRARY}
)

set_target_properties(polaris_rt_layer PROPERTIES
    PREFIX ""
    OUTPUT_NAME "polaris_rt_layer"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ── FIX: also link test app CRT statically for the same reason ───────────────
# Test application
add_executable(rt_test_app test_app/main.cpp)

set_property(TARGET rt_test_app PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(rt_test_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test_app
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(rt_test_app PRIVATE
    ${Vulkan_LIBRARY}
)

set_target_properties(rt_test_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ── Copy JSON and verify library_path is correct after configure ──────────────
# The JSON is generated from the .in template by configure_file.
# library_path must start with "./" so the 1.3+ loader resolves it relative
# to the JSON file, not as a bare system DLL name.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/layer/polaris_rt_layer.json.in"
    "${CMAKE_BINARY_DIR}/bin/polaris_rt_layer.json"
    @ONLY
)

# Also copy JSON next to Release/Debug output for in-tree runs
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/layer/polaris_rt_layer.json.in"
    "${CMAKE_BINARY_DIR}/bin/Release/polaris_rt_layer.json"
    @ONLY
)

message(STATUS "")
message(STATUS "=== Polaris RT Layer Configuration ===")
message(STATUS "Vulkan SDK:  ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Layer output: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "Test app:    ${CMAKE_BINARY_DIR}/bin/rt_test_app.exe")
message(STATUS "CRT linkage: Static (MultiThreaded)")
message(STATUS "JSON library_path: ./polaris_rt_layer.dll")
message(STATUS "========================================")
