cmake_minimum_required(VERSION 3.12)
project(splat_renderer)

include(cmake/StringifyShaders.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(targetname splat_renderer)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LIB_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies)

add_subdirectory(${LIB_ROOT_DIR}/glfw)
add_subdirectory(${LIB_ROOT_DIR}/tinyply)
add_subdirectory(${LIB_ROOT_DIR}/pybind11)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c" "src/*.h" "src/shaders/*")

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
stringify_shaders(splat.vert splat.frag)
set(GENERATED_HEADERS ${GENERATED_HEADER_DIR}/splat.vert.h ${GENERATED_HEADER_DIR}/splat.frag.h)

pybind11_add_module(${targetname} ${SRC} ${GENERATED_HEADERS})
set_property(TARGET ${targetname} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories(${targetname}
  ${LIB_ROOT_DIR}/glfw/include
  ${LIB_ROOT_DIR}/tinyply/source
  ${LIB_ROOT_DIR}/glad
  ${LIB_ROOT_DIR}/KHR
  ${LIB_ROOT_DIR}/glm
  ${LIB_ROOT_DIR}/pybind11/include
  ${SRC}
  ${GENERATED_HEADER_DIR}
)

find_package(OpenGL REQUIRED)

target_compile_definitions(${targetname} PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(${targetname} PRIVATE ${OPENGL_gl_LIBRARY})
target_link_libraries(${targetname} PRIVATE glfw tinyply)