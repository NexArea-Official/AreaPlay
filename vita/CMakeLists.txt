cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "Define VITASDK to point to the VitaSDK installation.")
endif()

# Nom affiche de l'appli (bulle, logo, textes) : le changer ici seulement.
set(VF_NAME "Area Play")
# Nom du dossier ux0:data/<nom> : lettres et chiffres, sans espace ni accent.
set(VF_DIR "AreaPlay")
set(VITA_APPNAME "AreaPlay") # nom des fichiers produits (build\AreaPlay.vpk), utilise par build.cmd
# Identifiant de la bulle (9 caracteres). VitaFlow utilisait VFLW00001 : Area Play s'installe a cote.
set(VITA_TITLEID "ARPL00001")
set(VITA_VERSION "01.00")

project(${VITA_APPNAME} C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# main.c inclut les fichiers de src/parties/ : ils ne doivent pas etre ajoutes ici.
add_executable(${VITA_APPNAME} src/main.c)
target_compile_definitions(${VITA_APPNAME} PRIVATE VF_APP_NAME="${VF_NAME}" VF_DATA_NAME="${VF_DIR}")
# -O2 : sans optimisation, vita-elf-create (VitaSDK du 25/08/2026) plante (0xC0000374) des que le programme grossit
target_compile_options(${VITA_APPNAME} PRIVATE -O2)
target_link_libraries(${VITA_APPNAME}
  vita2d
  SceAppMgr_stub
  SceAudio_stub
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  ScePgf_stub
  ScePower_stub
  SceRtc_stub
  SceSysmodule_stub
  SceTouch_stub
  SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  SceSsl_stub
  SceLibKernel_stub
  jpeg
  png
  z
  freetype
  bz2
  m
)

vita_create_self(eboot.bin ${VITA_APPNAME} UNSAFE)
file(GLOB_RECURSE VITA_ASSETS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.jpg"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.jpeg"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.otf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.wav"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang/*.txt"
)
set(VITA_ASSET_ARGS)
foreach(asset IN LISTS VITA_ASSETS)
  file(RELATIVE_PATH asset_dest "${CMAKE_CURRENT_SOURCE_DIR}" "${asset}")
  # Espaces, accents ou parentheses dans un nom : l'outil d'emballage echoue sans le dire (VPK non mis a jour).
  # Ces fichiers sont copies sous un nom sur (caracteres remplaces par _) avant l'emballage.
  if(NOT asset_dest MATCHES "^[A-Za-z0-9_./-]+$")
    string(REGEX REPLACE "[^A-Za-z0-9_./-]" "_" asset_dest "${asset_dest}")
    set(asset_safe "${CMAKE_BINARY_DIR}/assets_noms_surs/${asset_dest}")
    configure_file("${asset}" "${asset_safe}" COPYONLY)
    set(asset "${asset_safe}")
  endif()
  list(APPEND VITA_ASSET_ARGS "${asset}" "${asset_dest}")
endforeach()

vita_create_vpk(${VITA_APPNAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME "${VF_NAME} Vita Edition" # nom sous la bulle (dans l'appli : VF_NAME seul)
  FILE ${VITA_ASSET_ARGS}
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/pic0.png sce_sys/pic0.png
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
       ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
)
