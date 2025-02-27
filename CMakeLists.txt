# In CLion reloading cmake profiles in parallel can cause issues with the vcpkg lock.
# Make sure to enable the Reload CMake profiles sequentially option in Settings | Advanced Settings | CMake.

cmake_minimum_required(VERSION 3.28)
include(FetchContent)

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Verbose Makefile" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies are managed with vcpkg when possible
include(cmake/vcpkg_toolchain.cmake)
include(cmake/compiler_config.cmake)

# Project Config

project(cpp_vulkan_playground CXX)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "src/*.h")
add_executable(cpp_vulkan_playground ${sources} ${headers})

set_compiler_flags(cpp_vulkan_playground)

# Dependencies

include(cmake/dependencies.cmake)

target_compile_definitions(cpp_vulkan_playground PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_SPACESHIP_OPERATOR VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_link_libraries(cpp_vulkan_playground PRIVATE Vulkan::Headers)
target_link_libraries(cpp_vulkan_playground PRIVATE GPUOpen::VulkanMemoryAllocator unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp)
target_link_libraries(cpp_vulkan_playground PRIVATE glfw)
target_compile_definitions(cpp_vulkan_playground PRIVATE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_LEFT_HANDED GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(cpp_vulkan_playground PRIVATE glm::glm)
target_link_libraries(cpp_vulkan_playground PRIVATE unofficial::shaderc::shaderc)
target_include_directories(cpp_vulkan_playground PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(cpp_vulkan_playground PRIVATE tinyobjloader::tinyobjloader)
target_link_libraries(cpp_vulkan_playground PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(cpp_vulkan_playground PRIVATE Vulkan::UtilityHeaders) # Unused-> Vulkan::SafeStruct Vulkan::LayerSettings  Vulkan::CompilerConfiguration
target_link_libraries(cpp_vulkan_playground PRIVATE cpptrace::cpptrace)
target_compile_definitions(cpp_vulkan_playground PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(cpp_vulkan_playground PRIVATE imgui::imgui)
target_link_libraries(cpp_vulkan_playground PRIVATE TracyClient)
