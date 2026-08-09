add_library(sxwnl_gles_renderer STATIC
  ${SXWNL_ROOT}/gui/renderer.cpp
  ${SXWNL_ROOT}/gui/objloader.cpp
  ${SXWNL_ROOT}/gui/stb_impl.cpp)

target_compile_definitions(sxwnl_gles_renderer PUBLIC SXWNL_USE_GLES=1)
target_include_directories(sxwnl_gles_renderer PUBLIC
  ${SXWNL_ROOT}/gui
  ${stb_SOURCE_DIR})
target_link_libraries(sxwnl_gles_renderer PUBLIC GLESv3)
