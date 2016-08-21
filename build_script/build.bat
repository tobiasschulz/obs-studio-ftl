SET build_config=Release
SET obs_version=0.15.4-ftl.1
SET coredeps=C:\beam\tachyon_deps
SET QTDIR64=C:\Qt\5.6\msvc2015_64
SET QTDIR32=C:\Qt\5.6\msvc2015
SET obsInstallerTempDir=%cd:\=/%/rundir/%build_config%
SET PATH=%PATH%;C:\Program Files (x86)\MSBuild\14.0\Bin;C:\Program Files (x86)\CMake\bin
SET DepsPath32=%coredeps%\win32
SET DepsPath64=%coredeps%\win64
SET build32=
SET build64=
SET package=
if "%1" == "all" (
SET build32=true
SET build64=true
SET package=true
)
if "%1" == "win64" (
SET build64=true
)
pushd .
cd ..
call git submodule update --init
popd
if defined build32 (
	cmake -G "Visual Studio 14 2015" -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true ..
	call msbuild /p:Configuration=%build_config% ALL_BUILD.vcxproj || exit /b
	copy %coredeps%\win32\bin\postproc-54.dll rundir\%build_config%\bin\32bit
)
if defined build64 (
	rmdir CMakeFiles /s /q
	del CMakeCache.txt
	cmake -G "Visual Studio 14 2015 Win64" -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true ..
	call msbuild /p:Configuration=%build_config%,Platform=x64 ALL_BUILD.vcxproj || exit /b
	copy %coredeps%\win64\bin\postproc-54.dll rundir\%build_config%\bin\64bit
)
if defined package (
	del CMakeCache.txt
	rmdir CMakeFiles /s /q
	cmake -G "Visual Studio 14 2015" -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true -DINSTALLER_RUN=true ..
	call msbuild /p:Configuration=%build_config% PACKAGE.vcxproj || exit /b
)
