cmake_minimum_required(VERSION 3.22)
project(sxwnl-android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SXWNL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

set(SXWNL_BUILD_GUI OFF CACHE BOOL "" FORCE)
set(SXWNL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${SXWNL_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/sxwnl)

include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
  GIT_SHALLOW ON)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(imgui)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

add_library(imgui_android STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_android.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_compile_definitions(imgui_android PUBLIC IMGUI_IMPL_OPENGL_ES3=1)
target_include_directories(imgui_android PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_android PUBLIC android GLESv3)

add_subdirectory(gles)

add_library(native_app_glue STATIC
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
  ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(sxwnl_android SHARED
  android_main.cpp
  ${SXWNL_ROOT}/gui/scene.cpp
  ${SXWNL_ROOT}/gui/panels.cpp)
target_include_directories(sxwnl_android PRIVATE
  ${SXWNL_ROOT}/gui)
target_link_libraries(sxwnl_android PRIVATE
  sxwnl_core
  sxwnl_gles_renderer
  imgui_android
  native_app_glue
  android
  EGL
  GLESv3
  log)
target_link_options(sxwnl_android PRIVATE "-uANativeActivity_onCreate")
