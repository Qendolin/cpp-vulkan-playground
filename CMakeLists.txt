cmake_minimum_required(VERSION 3.28)
project(cpp_vulkan_playground)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(VulkanHeaders REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "src/*.h")

add_executable(cpp_vulkan_playground ${sources} ${headers})

target_link_libraries(cpp_vulkan_playground PRIVATE Vulkan::Headers)
add_compile_definitions(VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_link_libraries(cpp_vulkan_playground PRIVATE glfw)
target_link_libraries(cpp_vulkan_playground PRIVATE glm::glm)
add_compile_definitions(GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_link_libraries(cpp_vulkan_playground PRIVATE unofficial::shaderc::shaderc)
# target_link_libraries(cpp_vulkan_playground PRIVATE GPUOpen::VulkanMemoryAllocator unofficial-vulkan-memory-allocator-hpp)
