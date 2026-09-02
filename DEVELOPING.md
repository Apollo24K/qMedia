# Developing qMedia

## Windows prerequisites

- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload
- CMake 3.16 or newer
- Qt 6.8.3 for MSVC 2022 64-bit
- Qt modules: Qt Image Formats, Qt Multimedia, and their dependencies
- PowerShell 7 for the scripts under `dist/scripts`

The current application links Qt Core, Gui, Network, Widgets, and Svg. Tests also
need Qt Test, and translations need Qt LinguistTools. Qt Multimedia and its FFmpeg
backend are installed in preparation for video and audio support but are not linked
by the application yet.

The local SDK used for this repository is:

```text
C:\Qt\6.8.3\msvc2022_64
```

## Build a release

Set the Qt location for the current PowerShell session, then run the existing build
script from the repository root:

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.8.3\msvc2022_64'
$env:Path = "$env:QT_ROOT_DIR\bin;$env:Path"
$env:buildArch = 'X64'
./dist/scripts/build.ps1
```

The executable is copied to `bin/qView.exe`. The qMedia rename has not yet been
applied to build targets or executable names.

## Build and run tests

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.8.3\msvc2022_64'
$env:Path = "$env:QT_ROOT_DIR\bin;$env:Path"
$env:buildArch = 'X64'
./dist/scripts/vcvars.ps1

cmake -S . -B build-tests -DBUILD_TESTS=ON -DCMAKE_PREFIX_PATH="$env:QT_ROOT_DIR"
cmake --build build-tests --config Debug --parallel
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir build-tests -C Debug --output-on-failure
```

`QT_QPA_PLATFORM=offscreen` keeps the widget tests from opening visible windows.

## Deployment note

Run `windeployqt` when creating a standalone Windows build. Once the application
links Qt Multimedia, `windeployqt` also deploys the Qt media plugin and the FFmpeg
runtime libraries supplied with Qt.
