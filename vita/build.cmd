@echo off
rem ============================================================
rem  Compile Area Play (Vita Edition) en VPK. Double-clic possible : la fenetre
rem  reste ouverte a la fin pour lire les messages.
rem  Option : "build.cmd clean" supprime l'ancien build d'abord.
rem ============================================================
set "PROJECT=E:\Projet\Projet_Vita\VitaFlow"
set "VITASDK=%USERPROFILE%\Documents\Codex\tools\vitasdk"
set "PATH=%VITASDK%\bin;%PATH%"

cd /d "%PROJECT%"
if errorlevel 1 (
  echo ERREUR : le dossier du projet n'existe pas : %PROJECT%
  pause
  exit /b 1
)
echo Dossier du projet : %CD%
echo.

if not exist "CMakeLists.txt" (
  echo ERREUR : CMakeLists.txt introuvable dans ce dossier.
  pause
  exit /b 1
)
if not exist "src\main.c" (
  echo ERREUR : src\main.c introuvable.
  pause
  exit /b 1
)
if not exist "%VITASDK%\share\vita.toolchain.cmake" (
  echo ERREUR : VitaSDK introuvable : %VITASDK%
  pause
  exit /b 1
)

if not exist "src\parties\vf_donnees.c" (
  echo ERREUR : dossier src\parties incomplet ^(main.c inclut ces fichiers^).
  pause
  exit /b 1
)

findstr /c:"VF_VERSION_NUM \"1.1\"" src\main.c >nul
if errorlevel 1 echo ATTENTION : ce src\main.c n'est PAS la version 1.1 ^(ancien fichier ?^)

findstr /c:"SceAppMgr_stub" CMakeLists.txt >nul
if errorlevel 1 (
  echo ERREUR : il manque SceAppMgr_stub dans CMakeLists.txt.
  pause
  exit /b 1
)
findstr /i /c:"UNSAFE" CMakeLists.txt >nul
if errorlevel 1 echo ATTENTION : CMakeLists.txt ne contient pas UNSAFE ^(lecture de ux0:app impossible^).

echo src\main.c date du :
dir /tc src\main.c | findstr /i "main.c"
echo.

if /i "%1"=="clean" (
  echo Suppression de build...
  rmdir /s /q build
)

cmake -G Ninja -S . -B build -DCMAKE_TOOLCHAIN_FILE="%VITASDK%\share\vita.toolchain.cmake"
if errorlevel 1 (
  echo.
  echo ECHEC a la configuration CMake. Essaie : build.cmd clean
  pause
  exit /b 1
)

rem Ancien VPK supprime : si l'emballage echoue, la compilation s'arrete au lieu de recopier l'ancien.
if exist "build\AreaPlay.vpk.out" del /q "build\AreaPlay.vpk.out"
if exist "build\AreaPlay.vpk" del /q "build\AreaPlay.vpk"

cmake --build build
if errorlevel 1 (
  echo.
  echo ECHEC de la compilation ^(voir les messages ci-dessus^).
  pause
  exit /b 1
)

echo.
echo ===== TERMINE : le VPK est ici =====
echo %PROJECT%\build\AreaPlay.vpk
echo.
pause
