@ECHO OFF
CALL :genspv "offscreen.vert"
CALL :genspv "offscreen.frag"
CALL :genspv "deferred.vert"
CALL :genspv "deferred.frag"

:genspv
start /b "" "glsLangValidator" -V100 --target-env vulkan1.2 -o %~1.spv %~1"
EXIT /B 0

glsLangValidator" -V100 --target-env vulkan1.2 -o %~1.spv %~1