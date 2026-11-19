cmake_minimum_required(VERSION 3.16)
# Plugin taiHEN d'Area Play : retour dans Area Play apres un jeu (voir main.c et README.txt).
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
endif()
project(areaplay_return C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

add_executable(areaplay_return main.c)
target_compile_options(areaplay_return PRIVATE -Wall -Wextra -O2 -fno-builtin -fno-tree-loop-distribute-patterns) # sans libc : pas d appel cache a memset / strlen
target_link_options(areaplay_return PRIVATE -nostdlib)
target_link_libraries(areaplay_return
  SceAppMgr_stub
  SceCtrl_stub
  SceIofilemgr_stub
  SceLibKernel_stub
  SceKernelThreadMgr_stub
)
vita_create_self(areaplay_return.suprx areaplay_return CONFIG exports.yml UNSAFE)
