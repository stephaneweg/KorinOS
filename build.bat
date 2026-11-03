@echo off
rem ===========================================================================
rem  ARMPI : construction de la carte SD d'un double-clic, sans PowerShell ni
rem  Python (seulement tools\fasmarm\fasmarm.exe). Resultat : le dossier
rem  sdcard\, a copier tel quel sur la premiere partition (FAT32) de la carte.
rem
rem    src\apps\<nom>  ->  sdcard\apps\<nom>.app\main (+ app.txt)
rem    src\bin\<nom>   ->  sdcard\bin\<nom>
rem    src\sys\<nom>   ->  sdcard\sys\<nom>
rem    src\svc\<nom>   ->  sdcard\sys\svc\<nom>
rem    src\kits\<kit>  ->  sdcard\lib\<kit>.so
rem    src\svc\modules ->  sdcard\<nom>.mod (charges par le firmware)
rem    noyaux, firmware, config.txt, polices, etc\, res\, home\, sources
rem
rem  Ecran par defaut 800x600 (PocketTerm) : build.bat 640 480 pour un autre.
rem  (build.ps1 reste l'outil de developpement : images QEMU, carte E:...)
rem ===========================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "ROOT=%~dp0"
set "FASM=%ROOT%tools\fasmarm\fasmarm.exe"
set "OUT=%ROOT%sdcard"
set "B=%ROOT%build"
set "LOG=%B%\build.log"
set "EW=800"
set "EH=600"
if not "%~1"=="" set "EW=%~1"
if not "%~2"=="" set "EH=%~2"

if not exist "%FASM%" ( echo FASMARM introuvable : %FASM% & goto :echec )
for %%d in ("%B%" "%B%\apps" "%B%\lib" "%B%\svc" "%B%\modules") do if not exist %%d mkdir %%d
if exist "%OUT%" rmdir /s /q "%OUT%"
for %%d in ("" "\apps" "\bin" "\sys" "\sys\svc" "\lib" "\fonts" "\firmware" "\home") do mkdir "%OUT%%%~d"
type nul > "%LOG%"

echo === kits
for /d %%k in (src\kits\*) do if exist "%%k\%%~nxk.asm" (
  call :asm "%%k" "%%~nxk" "%B%\lib\%%~nxk.so" || goto :echec
)
for %%k in ("%B%\lib\*.so") do copy /y "%%k" "%OUT%\lib\" >nul
rem kits precompiles (code C porte : mediakit, tools\mediakit)
for %%k in (prebuilt\*.so) do copy /y "%%k" "%OUT%\lib\" >nul

echo === services
for /d %%s in (src\svc\*) do if exist "%%s\%%~nxs.asm" (
  call :asm "%%s" "%%~nxs" "%B%\svc\%%~nxs.elf" || goto :echec
  copy /y "%B%\svc\%%~nxs.elf" "%OUT%\sys\svc\%%~nxs" >nul
)

echo === applications
for /d %%a in (src\apps\*) do if exist "%%a\%%~nxa.asm" (
  call :asm "%%a" "%%~nxa" "%B%\apps\%%~nxa.elf" || goto :echec
  mkdir "%OUT%\apps\%%~nxa.app"
  copy /y "%B%\apps\%%~nxa.elf" "%OUT%\apps\%%~nxa.app\main" >nul
  if exist "%%a\app.txt" copy /y "%%a\app.txt" "%OUT%\apps\%%~nxa.app\app.txt" >nul
)

echo === commandes (bin)
for /d %%a in (src\bin\*) do if exist "%%a\%%~nxa.asm" (
  call :asm "%%a" "%%~nxa" "%B%\apps\%%~nxa.elf" || goto :echec
  copy /y "%B%\apps\%%~nxa.elf" "%OUT%\bin\%%~nxa" >nul
)

echo === systeme (sys)
for /d %%a in (src\sys\*) do if exist "%%a\%%~nxa.asm" (
  call :asm "%%a" "%%~nxa" "%B%\apps\%%~nxa.elf" || goto :echec
  copy /y "%B%\apps\%%~nxa.elf" "%OUT%\sys\%%~nxa" >nul
)

echo === modules de demarrage
pushd src\svc\modules
for %%m in (*.asm) do (
  "%FASM%" "%%m" "%B%\modules\%%~nm.mod" >> "%LOG%" 2>&1 || ( echo   echec : modules\%%m & popd & goto :echec )
  echo   %%~nm.mod
)
popd
copy /y "%B%\modules\*.mod" "%OUT%\" >nul

echo === noyaux (ecran %EW%x%EH%)
pushd src\kernel
"%FASM%" -d ECRAN_W=%EW% -d ECRAN_H=%EH% kernel.asm "%OUT%\kernel8.img" >> "%LOG%" 2>&1 || ( echo   echec : noyau Pi 4 & popd & goto :echec )
"%FASM%" -d ARCH=pi5 -d ECRAN_W=%EW% -d ECRAN_H=%EH% kernel.asm "%OUT%\kernel_2712.img" >> "%LOG%" 2>&1 || ( echo   echec : noyau Pi 5 & popd & goto :echec )
popd
echo   kernel8.img, kernel_2712.img

echo === fichiers de la carte
copy /y firmware\start4.elf "%OUT%\" >nul
copy /y firmware\fixup4.dat "%OUT%\" >nul
copy /y firmware\bcm2711-rpi-4-b.dtb "%OUT%\" >nul
copy /y boot\config.txt "%OUT%\" >nul
copy /y ref\fonts\*.ttf "%OUT%\fonts\" >nul
copy /y firmware\wifi\* "%OUT%\firmware\" >nul
for %%d in (etc res docs) do xcopy /e /i /q /y "skel\sd0\%%d" "%OUT%\%%d" >nul
copy /y src\svc\modules\init.txt "%OUT%\etc\init.txt" >nul
if exist skel\sd0\README.TXT copy /y skel\sd0\README.TXT "%OUT%\" >nul
for %%d in (documents music pictures video) do mkdir "%OUT%\home\%%d"
rem WebKit (Jet, jsc...) : construit dans WSL, depose ici par tools\webkit\stage.sh
if exist build\webkit\sdroot xcopy /e /i /q /y build\webkit\sdroot "%OUT%" >nul
rem FM Tracker : ses instruments, ses mots, les morceaux d'exemple
xcopy /e /i /q /y src\apps\fmtracker\ins "%OUT%\apps\fmtracker.app\ins" >nul
xcopy /e /i /q /y src\apps\fmtracker\lang "%OUT%\apps\fmtracker.app\lang" >nul
xcopy /i /q /y src\apps\fmtracker\songs "%OUT%\home\music\fms" >nul
rem SoundFont General MIDI d'Onyx (hors de git) : le synthe MIDI d'AudioKit, Koton
if exist build\onyx\sdcard\res\soundfonts xcopy /i /q /y build\onyx\sdcard\res\soundfonts "%OUT%\res\soundfonts" >nul
rem Chansons de Koton Studio de l'utilisateur (hors de git), ouvertes par Koton
if exist "%USERPROFILE%\Music\*.sq" (
  if not exist "%OUT%\home\koton\songs" mkdir "%OUT%\home\koton\songs"
  copy /y "%USERPROFILE%\Music\*.sq" "%OUT%\home\koton\songs" >nul
)
rem sources : reassemblables sous ARMPI (/bin/fasmarm, fasmarm compris)
xcopy /e /i /q /y src "%OUT%\src" >nul

echo.
echo Carte prete dans %OUT%
echo (a copier sur la premiere partition FAT32 de la carte ; sur une carte deja
echo  utilisee, garder son dossier etc\ : vos reglages, en y reportant les
echo  lignes du reseau de sdcard\etc : services.txt SD0:/sys/svc/netd, telnetd,
echo  ftpd, vncd, rdpd ; drivers.txt soc:genet, soc:wifi, usb:02/06, usb:02/02/ff ;
echo  build.ps1 sd X: le fait tout seul)
goto :fin

rem --- :asm dossier nom sortie : assemble <dossier>\<nom>.asm dans son dossier
:asm
pushd "%~1"
"%FASM%" "%~2.asm" "%~3" >> "%LOG%" 2>&1
if errorlevel 1 ( echo   echec : %~1 & popd & exit /b 1 )
echo   %~2
popd
exit /b 0

:echec
echo.
echo *** ECHEC de la construction (details : %LOG%) ***
endlocal
pause
exit /b 1

:fin
endlocal
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
exit /b 0
