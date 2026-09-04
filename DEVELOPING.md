# Developing qMedia

## Windows prerequisites

- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload
- CMake 3.16 or newer
- Qt 6.8.3 for MSVC 2022 64-bit
- Qt modules: Qt Image Formats, Qt Multimedia, and their dependencies
- PowerShell 7 for the scripts under `dist/scripts`

The application links Qt Core, Gui, Multimedia, MultimediaWidgets, Network,
Widgets, and Svg. Tests also need Qt Test, and translations need Qt LinguistTools.

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

The executable is copied to `bin/qMedia.exe`.

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

## Optional image-format plugins

Qt does not include an APNG decoder in its standard image-format package. To add
APNG animation support to a local build, deploy qMedia's matching QtApng plugin
beside the executable:

```powershell
./dist/scripts/download-plugins.ps1 -DestinationRoot build-nmake -ApngOnly
```

Omit `-ApngOnly` when preparing a distribution to include the additional
KImageFormats decoders as well. The plugin version is selected from the active
Qt installation, so run the script in the same configured Qt environment used
for the build.

## Export

Ctrl+S opens Export for the current image, animation, or video. Playback and the
slideshow pause when opening the dialog. The default source is the current frame;
animations and videos also offer the entire file. Rotation, horizontal mirroring,
vertical flipping, looping, and audio inclusion start with the current canvas and
playback state. Rotation is applied before screen-axis flips, then resizing; output
dimensions swap when toggling a quarter-turn. Sizing can use pixels or a percentage
of the original dimensions. The editable filename is carried into the save dialog.
Canvas zoom does not affect export. Whole-video export offers a speed multiplier,
initialized from playback speed, and adjusts audio tempo while preserving pitch.
Whole-media export can reverse playback (including included audio).
Animated frame durations are reversed with their frames. Video reversal uses
FFmpeg's reverse filters and can require substantial memory for long clips.

The preview automatically renders still images through the actual export pipeline,
including format, quality, transparency, size, and orientation. For whole media,
the initial preview shows the selected frame; **Play preview** encodes the entire
output into a temporary file for silent playback. Changing a setting cancels stale
preview work. Preview generation and final export use the same settings and encoder.
Below the preview, output details show dimensions, aspect ratio, duration for whole
media, and the encoded file size. Whole-media file size is available after rendering
the full preview; a frame thumbnail is never used to estimate a video's file size.

Still images use Qt's installed image writers (PNG, JPEG, WebP, TIFF, BMP where
available). Full video/animation conversion uses an optional installed FFmpeg
executable, found on PATH or selected with **Locate FFmpeg** in the Export dialog.
This does not download or bundle an executable and adds no startup dependency.
Qt's bundled FFmpeg playback libraries do not provide the FFmpeg command-line tool.
MP4 requires libx264 and AAC, WebM requires libvpx-vp9 and libopus, and animated WebP
requires libwebp_anim in that installation. Missing encoders produce an export error.

Animation decoding uses Qt's plugins and stages PNG frames in the temporary
directory to preserve frame timing and support the same formats as the viewer.
Long animations can need substantial temporary disk space. Conversion runs off
the GUI thread, can be cancelled, and commits output only after success. Original
source files cannot be overwritten. Qt 6 captures decoded video frames directly;
Qt 5 uses FFmpeg timestamp extraction for still video export.

`ExportTests` tests image output and file preservation without FFmpeg, and also
tests real animation/video conversions when FFmpeg is installed.

## Deployment note

Run `windeployqt` when creating a standalone Windows build. Once the application
links Qt Multimedia, `windeployqt` also deploys the Qt media plugin and the FFmpeg
runtime libraries supplied with Qt.
