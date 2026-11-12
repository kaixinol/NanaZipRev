@setlocal
@echo off

rem Change to the current folder.
cd "%~dp0"

rem Clean final artifacts only. Keep Output\Objects so that the
rem incremental compilation cache (.ipdb / .iobj / .obj / .ilk)
rem survives across runs and avoids falling back to full LTCG.
for %%D in (Binaries AppPackages Root) do if exist "Output\%%D" rd /s /q "Output\%%D"
del /q Output\*.binlog 2>nul

rem Initialize Visual Studio environment.
set VisualStudioInstallerFolder="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if %PROCESSOR_ARCHITECTURE%==x86 set VisualStudioInstallerFolder="%ProgramFiles%\Microsoft Visual Studio\Installer"
pushd %VisualStudioInstallerFolder%
for /f "usebackq tokens=*" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set VisualStudioInstallDir=%%i
)
popd
call "%VisualStudioInstallDir%\VC\Auxiliary\Build\vcvarsall.bat" x86

rem Build preview release and generate msixbundle for all configured
rem architectures (see NanaZipPackage\NanaZipPackage.wapproj: AppxBundlePlatforms).
MSBuild -binaryLogger:Output\BuildPreviewMsixBundle.binlog -m BuildAllTargets.proj -p:NanaZipBuildPreviewRelease=true

echo.
echo === Done ===
echo MSIX bundle is in: Output\Binaries\AppPackages
echo Portable binaries are in: Output\Binaries\Root\Binaries
@endlocal
