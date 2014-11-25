@echo off
rem $Id: loadall.cmd 43137 2012-08-31 12:25:53Z vboxsync $
rem rem @file
rem Windows NT batch script for launching loadall.sh
rem

rem
rem Copyright (C) 2009-2012 Oracle Corporation
rem
rem This file is part of VirtualBox Open Source Edition (OSE), as
rem available from http://www.virtualbox.org. This file is free software;
rem you can redistribute it and/or modify it under the terms of the GNU
rem General Public License (GPL) as published by the Free Software
rem Foundation, in version 2 as it comes in the "COPYING" file of the
rem VirtualBox OSE distribution. VirtualBox OSE is distributed in the
rem hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
rem


setlocal ENABLEEXTENSIONS
setlocal

rem
rem loadall.sh should be in the same directory as this script.
rem
set MY_SCRIPT=%~dp0loadall.sh
if exist "%MY_SCRIPT%" goto found
echo loadall.cmd: failed to find loadall.sh in "%~dp0".
goto end

rem
rem Found it, convert slashes and tell kmk_ash to interpret it.
rem
:found
set MY_SCRIPT=%MY_SCRIPT:\=/%
set MY_ARGS=%*
if ".%MY_ARGS%." NEQ ".." set MY_ARGS=%MY_ARGS:\=/%
kmk_ash %MY_SCRIPT% %MY_ARGS%

:end
endlocal
endlocal
