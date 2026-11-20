@echo off
echo Essai de FreeCOM sur Korin
dir /w > LISTE.TXT
type LISTE.TXT
dostest depuis-bat
if errorlevel 3 echo errorlevel 3 vu
set ESSAI=ok
echo variable : %ESSAI%
del LISTE.TXT
if not exist LISTE.TXT echo LISTE.TXT effac‚
