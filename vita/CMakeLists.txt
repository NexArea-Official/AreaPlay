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
set(VITA_VERSION "01.10")

project(${VITA_APPNAME} C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# main.c inclut les fichiers de src/parties/ : ils ne doivent pas etre ajoutes ici.
add_executable(${VITA_APPNAME} src/main.c)
target_compile_definitions(${VITA_APPNAME} PRIVATE VF_APP_NAME="${VF_NAME}" VF_DATA_NAME="${VF_DIR}" VF_TITLE_ID="${VITA_TITLEID}")
# Version de test des mises a jour (jamais publiee) : cmake -S . -B build_test -G Ninja -DAP_TEST_MAJ=ON
# [-DAP_TEST_VERSION=1.1.1] ; lit ux0:data/AreaPlay/maj_test.txt (adresse d'un serveur sur le PC).
option(AP_TEST_MAJ "Version de test des mises a jour" OFF)
set(AP_TEST_VERSION "" CACHE STRING "Numero de version force (test)")
if(AP_TEST_MAJ)
  target_compile_definitions(${VITA_APPNAME} PRIVATE AP_TEST_MAJ)
endif()
if(AP_TEST_VERSION)
  target_compile_definitions(${VITA_APPNAME} PRIVATE VF_VERSION_NUM="${AP_TEST_VERSION}")
endif()
# vita-elf-create (VitaSDK du 25/08/2026) plante parfois (0xC0000374) selon le contenu exact du programme, sans
# erreur dans le code. Si ca arrive : essayer l'autre reglage (ajouter -ffunction-sections -fdata-sections et
# target_link_options(... -Wl,--gc-sections), ou -Os) ; l'un des deux passe.
target_compile_options(${VITA_APPNAME} PRIVATE -O2)
target_link_libraries(${VITA_APPNAME}
  curl # HTTPS (mises a jour) : curl + OpenSSL dans l'appli, ne depend pas du SSL de la console
  ssl
  crypto
  zstd
  pthread
  vita2d
  SceAppMgr_stub
  ScePromoterUtil_stub
  SceAVConfig_stub
  SceRegistryMgr_stub
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
  SceProcessmgr_stub
  SceIofilemgr_stub
  SceKernelModulemgr_stub
  SceKernelThreadMgr_stub
  SceSysmem_stub
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
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/nouveautes.txt"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/cacert.pem"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/transfert/*.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/themes/*/theme.txt"
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

# Area Play Updater, livre dans le VPK (dossier updater/) et installe par Area Play au demarrage.
# Compiler updater/ d'abord : cmake --build updater/build
set(UPD_B "${CMAKE_CURRENT_SOURCE_DIR}/updater/build")
set(UPD_S "${CMAKE_CURRENT_SOURCE_DIR}/updater/sce_sys")
if(EXISTS "${UPD_B}/eboot.bin")
  list(APPEND VITA_ASSET_ARGS
       ${UPD_B}/eboot.bin updater/eboot.bin
       ${UPD_B}/AreaPlayUpdater.vpk_param.sfo updater/sce_sys/param.sfo
       ${UPD_S}/icon0.png updater/sce_sys/icon0.png
       ${UPD_S}/pic0.png updater/sce_sys/pic0.png
       ${UPD_S}/livearea/contents/template.xml updater/sce_sys/livearea/contents/template.xml
       ${UPD_S}/livearea/contents/bg0.png updater/sce_sys/livearea/contents/bg0.png
       ${UPD_S}/livearea/contents/startup.png updater/sce_sys/livearea/contents/startup.png)
  if(EXISTS "${UPD_S}/package/head.bin")
    list(APPEND VITA_ASSET_ARGS ${UPD_S}/package/head.bin updater/sce_sys/package/head.bin)
  endif()
else()
  message(WARNING "Area Play Updater non compile : le VPK ne le contiendra pas")
endif()
# Plugin de retour apres un jeu : installe par Area Play quand l'option est activee (cmake --build plugin/areaplay_return/build)
set(RET_SUPRX "${CMAKE_CURRENT_SOURCE_DIR}/plugin/areaplay_return/build/areaplay_return.suprx")
if(EXISTS "${RET_SUPRX}")
  list(APPEND VITA_ASSET_ARGS ${RET_SUPRX} plugin/areaplay_return.suprx)
else()
  message(WARNING "Plugin areaplay_return non compile : le VPK ne le contiendra pas")
endif()

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
