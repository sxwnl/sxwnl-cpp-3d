cmake_minimum_required(VERSION 3.14)
project(sxwnl-cpp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE on)

option(SXWNL_BUILD_GUI "Build the ImGui + OpenGL 3D solar-system GUI" OFF)

if (MSVC)
  add_compile_options("/utf-8")
  add_compile_definitions(WIN32 _CRT_SECURE_NO_WARNINGS)
elseif (MINGW)
  add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
  add_compile_definitions(WIN32)
elseif (CYGWIN)
  add_compile_options(-w -fexceptions)
else()
  add_compile_options(-w -fexceptions)
endif()

# ----------------------------------------------------------------------------
# Astronomical engine as a reusable static library (shared by test1 and GUI)
# ----------------------------------------------------------------------------
file(GLOB CORE_SRCS
  "${PROJECT_SOURCE_DIR}/eph/*.cpp"
  "${PROJECT_SOURCE_DIR}/lunar/*.cpp"
  "${PROJECT_SOURCE_DIR}/mylib/*.cpp"
)
add_library(sxwnl_core STATIC ${CORE_SRCS})
target_include_directories(sxwnl_core PUBLIC
  ${PROJECT_SOURCE_DIR}/eph
  ${PROJECT_SOURCE_DIR}/lunar
  ${PROJECT_SOURCE_DIR}/mylib
)
# The engine is C++11; keep it lenient so the C++17 GUI can still link it.
set_target_properties(sxwnl_core PROPERTIES CXX_STANDARD 11)

# ----------------------------------------------------------------------------
# Original console test executable (unchanged behaviour)
# ----------------------------------------------------------------------------
add_executable(test1 test/test1.cpp)
target_link_libraries(test1 PRIVATE sxwnl_core)
if (MINGW)
  target_link_options(test1 PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if (CYGWIN)
  add_custom_command(TARGET test1 POST_BUILD
    COMMAND bash ${PROJECT_SOURCE_DIR}/copy_cygdlls.sh
            "$<TARGET_FILE:test1>"
            "$<TARGET_FILE_DIR:test1>"
    COMMENT "拷贝 Cygwin 运行时 DLL 到输出目录")
endif()

# ----------------------------------------------------------------------------
# GUI: Dear ImGui + GLFW + GLEW + OpenGL
# ----------------------------------------------------------------------------
if (SXWNL_BUILD_GUI)
  include(FetchContent)

  # --- GLFW (window / input / GL context) ---
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  # Disable Wayland: requires wayland-scanner at build time which is not
  # always installed; X11 works everywhere including SSH/MobaXterm forwarding.
  set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_X11      ON  CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW ON)

  # --- GLAD (vendored, pre-generated for GL 3.3 core; no Python / codegen) ---
  add_library(glad STATIC
    ${PROJECT_SOURCE_DIR}/gui/third_party/glad/src/glad.c)
  target_include_directories(glad PUBLIC
    ${PROJECT_SOURCE_DIR}/gui/third_party/glad/include)

  # --- Dear ImGui (docking branch) ---
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW ON)

  # --- STB image loader (header-only, no CMakeLists in stb repo) ---
  FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW ON)
  FetchContent_GetProperties(stb)
  if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
  endif()

  FetchContent_MakeAvailable(glfw imgui)

  # ImGui ships no CMake; build it (core + GLFW/OpenGL3 backends) ourselves.
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)

  find_package(OpenGL REQUIRED)

  file(GLOB GUI_SRCS "${PROJECT_SOURCE_DIR}/gui/*.cpp")
  add_executable(sxwnl_gui ${GUI_SRCS})
  target_include_directories(sxwnl_gui PRIVATE ${stb_SOURCE_DIR})
  target_link_libraries(sxwnl_gui PRIVATE
    sxwnl_core
    imgui
    glad
    glfw
    OpenGL::GL)

  if (MINGW)
    target_link_options(sxwnl_gui PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
