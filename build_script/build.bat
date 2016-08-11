SET build_config=Release
SET obs_version=1.2.11
SET coredeps=C:\beam\tachyon_deps
SET QTDIR64=C:\Qt\5.6\msvc2015_64
SET PATH=%PATH%;C:\Program Files (x86)\MSBuild\14.0\Bin;C:\Program Files (x86)\CMake\bin
SET DepsPath64=%coredeps%\win64
SET FFmpegPath64=%coredeps%\win64
SET x264Path64=%coredeps%\win64
SET curlPath64=%coredeps%\win64
pushd .
cd ..
call git submodule update --init
popd
cmake -G "Visual Studio 14 2015 Win64" -DOBS_VERSION_OVERRIDE=%obs_version% -DFTLSDK_LIB=%ftl_lib_dir% -DFTLSDK_INCLUDE_DIR=%ftl_inc_dir% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true ..
call msbuild /p:Configuration=%build_config%,Platform=x64 ALL_BUILD.vcxproj || exit /b
copy %coredeps%\win64\bin\postproc-54.dll rundir\%build_config%\bin\64bit

