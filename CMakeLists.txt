cmake_minimum_required(VERSION 3.28)

# Vcpkg config needs to come before "project". Using a toolchain file would be better.

set(ENV{CC} ${CMAKE_C_COMPILER})
set(ENV{CXX} ${CMAKE_CXX_COMPILER})

message("Using compiler '${CMAKE_CXX_COMPILER_ID}'")
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -std=c++23")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -L/usr/lib/llvm-19/lib/ -lc++ -lc++abi")
    set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")
    set(VCPKG_LINKER_FLAGS "${VCPKG_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")
endif ()

project(cpp_vulkan_playground)

# In CLion reloading cmake profiles in parallel can cause issues with the vcpkg lock.
# Make sure to enable the Reload CMake profiles sequentially option in Settings | Advanced Settings | CMake.

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(VulkanHeaders REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(VulkanUtilityLibraries CONFIG REQUIRED)
find_package(cpptrace CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "src/*.h")

add_executable(cpp_vulkan_playground ${sources} ${headers})

target_link_libraries(cpp_vulkan_playground PRIVATE Vulkan::Headers)
add_compile_definitions(VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_link_libraries(cpp_vulkan_playground PRIVATE glfw)
target_link_libraries(cpp_vulkan_playground PRIVATE glm::glm)
add_compile_definitions(GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_LEFT_HANDED)
target_link_libraries(cpp_vulkan_playground PRIVATE unofficial::shaderc::shaderc)
target_link_libraries(cpp_vulkan_playground PRIVATE GPUOpen::VulkanMemoryAllocator unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp)
target_include_directories(cpp_vulkan_playground PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(cpp_vulkan_playground PRIVATE tinyobjloader::tinyobjloader)
target_link_libraries(cpp_vulkan_playground PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(cpp_vulkan_playground PRIVATE Vulkan::UtilityHeaders) # Unused-> Vulkan::SafeStruct Vulkan::LayerSettings  Vulkan::CompilerConfiguration
target_link_libraries(cpp_vulkan_playground PRIVATE cpptrace::cpptrace)