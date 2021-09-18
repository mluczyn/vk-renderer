project(Vulkan-Renderer)
cmake_minimum_required(VERSION 3.8)

add_compile_definitions("$<$<PLATFORM_ID:Windows>:NOMINMAX>")

set(BUILD_SHARED_LIBS NO)
add_subdirectory(glfw)
add_subdirectory(glslang)
add_subdirectory(glm)
add_subdirectory(assimp)

find_package(Vulkan REQUIRED)

file(GLOB HEADERS inc/*.hpp)
file(GLOB SOURCES src/*.cpp)

add_executable(vkexp ${SOURCES} ${HEADERS})
set_target_properties(vkexp PROPERTIES CXX_STANDARD 17 CMAKE_CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_link_libraries(vkexp Vulkan::Vulkan glfw SPIRV glslang glslang-default-resource-limits assimp)
target_include_directories(vkexp PRIVATE inc Vulkan::Vulkan glm assimp "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include" "${CMAKE_CURRENT_SOURCE_DIR}/external_inc")

target_compile_definitions(vkexp PRIVATE VW_DEBUG=$<CONFIG:DEBUG>)
target_compile_definitions(vkexp PRIVATE "$<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>")
target_compile_options(vkexp PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>")

source_group("headers" FILES ${HEADERS})
source_group("sources" FILES ${SOURCES})