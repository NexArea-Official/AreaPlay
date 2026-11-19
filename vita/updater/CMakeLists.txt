cmake_minimum_required(VERSION 3.16)
# Area Play Updater : installe la mise a jour d'Area Play (voir main.c). Compile avec le projet principal.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
endif()
project(AreaPlayUpdater C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(UPDATER_TITLEID "ARPL00002")
set(UPDATER_VERSION "01.00")

add_executable(AreaPlayUpdater main.c)
target_compile_options(AreaPlayUpdater PRIVATE -O2 -Wall -Wno-format-truncation)
target_link_libraries(AreaPlayUpdater
  vita2d
  SceAppMgr_stub
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  ScePgf_stub
  SceSysmodule_stub
  SceLibKernel_stub
  freetype
  png
  jpeg
  z
  bz2
  m
)
vita_create_self(eboot.bin AreaPlayUpdater UNSAFE)
set(AP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
vita_create_vpk(AreaPlayUpdater.vpk ${UPDATER_TITLEID} eboot.bin
  VERSION ${UPDATER_VERSION}
  NAME "Area Play Updater"
  FILE ${AP_ROOT}/assets/fonts/Inter-Regular.ttf assets/fonts/Inter-Regular.ttf
       ${AP_ROOT}/assets/fonts/Inter-SemiBold.ttf assets/fonts/Inter-SemiBold.ttf
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/pic0.png sce_sys/pic0.png
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
)
