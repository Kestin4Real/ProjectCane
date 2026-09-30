##I'll be honest! I have NO IDEA how CMake works soo someone who knows it better PLEASE take a look at it!

cmake_minimum_required(VERSION 3.16)
project(ProjectCane LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)
pkg_check_modules(GLFW glfw3)
if(NOT GLFW_FOUND)
    find_library(GLFW_LIBRARIES NAMES glfw glfw3 libglfw.so.3
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
    if(NOT GLFW_LIBRARIES)
        message(FATAL_ERROR "GLFW not found. Install libglfw3-dev.")
    endif()
    set(GLFW_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include")
    set(GLFW_CFLAGS_OTHER "")
    set(GLFW_LIBRARY_DIRS "")
    message(STATUS "Using GLFW library ${GLFW_LIBRARIES}")
endif()

file(GLOB SLY1_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/*.cpp")
list(FILTER SLY1_SOURCES EXCLUDE REGEX "xaudio2_compat\\.cpp$")
list(APPEND SLY1_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/glad.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/ImGuiFileDialog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui_demo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui_impl_glfw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui_impl_opengl3.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui/imgui_widgets.cpp"
)

if(NOT WIN32)
    list(APPEND SLY1_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/xaudio2_compat.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/miniaudio_impl.c")
endif()

add_executable(Sly1-Release ${SLY1_SOURCES})

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/alo.cpp"
    PROPERTIES COMPILE_DEFINITIONS ALO_IMPLEMENTATION)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/glad.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/miniaudio_impl.c"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_include_directories(Sly1-Release PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui"
    ${GLFW_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS})

target_compile_definitions(Sly1-Release PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
    GLM_ENABLE_EXPERIMENTAL
    _CRT_SECURE_NO_WARNINGS
    SLY1_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release")

if(WIN32)
    target_compile_definitions(Sly1-Release PRIVATE GLFW_STATIC)
endif()

target_compile_options(Sly1-Release PRIVATE
    ${GLFW_CFLAGS_OTHER}
    ${FFMPEG_CFLAGS_OTHER})

if(NOT MSVC)
    target_compile_options(Sly1-Release PRIVATE
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-unused-function
        -Wno-unused-private-field
        -Wno-missing-field-initializers
        -Wno-invalid-offsetof
        -Wno-class-memaccess
        -Wno-delete-non-virtual-dtor
        -fno-strict-aliasing
        -fpermissive
        -fms-extensions
        -Wno-narrowing)

    set(SLY1_FORCE_HEADERS
        host_compat.h sw.h save.h freeze.h act.h wr.h shadow.h fader.h splicemap.h gc.h)
    foreach(header IN LISTS SLY1_FORCE_HEADERS)
        target_compile_options(Sly1-Release PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release/${header}>")
    endforeach()
    target_compile_options(Sly1-Release PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-iquote ${CMAKE_CURRENT_SOURCE_DIR}/Sly1-Release>")
endif()

target_link_directories(Sly1-Release PRIVATE
    ${GLFW_LIBRARY_DIRS}
    ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(Sly1-Release PRIVATE
    OpenGL::GL
    ${GLFW_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${CMAKE_DL_LIBS})

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(Sly1-Release PRIVATE Threads::Threads m)
endif()

add_executable(FileExtractor
    "${CMAKE_CURRENT_SOURCE_DIR}/File Extractor/main.cpp")
target_include_directories(FileExtractor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/File Extractor")
if(NOT MSVC)
    target_compile_options(FileExtractor PRIVATE
        -Wno-unused-parameter
        -fno-strict-aliasing)
endif()
if(UNIX)
    target_link_libraries(FileExtractor PRIVATE Threads::Threads)
endif()
