cmake_minimum_required(VERSION 3.12)
project(splat_renderer)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-std=c++17)
endif()

set(LIB_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

add_subdirectory(${LIB_ROOT_DIR}/glfw)
add_subdirectory(${LIB_ROOT_DIR}/tinyply)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c" "src/*.h" "src/shaders/*")

add_executable(splat_renderer ${SRC})
set_property(TARGET splat_renderer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories(splat_renderer
  ${LIB_ROOT_DIR}/glfw/include
  ${LIB_ROOT_DIR}/tinyply/source
  ${LIB_ROOT_DIR}/glad
  ${LIB_ROOT_DIR}/KHR
  ${LIB_ROOT_DIR}/glm
  ${SRC}
)

find_package(OpenGL REQUIRED)

target_compile_definitions(splat_renderer PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(splat_renderer ${OPENGL_gl_LIBRARY})
target_link_libraries(splat_renderer glfw tinyply)