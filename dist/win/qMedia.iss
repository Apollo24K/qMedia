#define MyAppName "qMedia"
#define MyAppPublisher "qMedia contributors"
#define MyAppURL "https://github.com/Apollo24K/qMedia"
#define MyAppExeName "qMedia.exe"

; Update these when building
#define MyAppVersion "7.1"
#define MyAppYear "2026"

[Setup]
AppId={{1C41CFE4-107D-4F8A-87C8-6C6923529252}
AppName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
LicenseFile=../../LICENSE
#if "MyArch" == "x86"
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-win32
#elif "MyArch" == "arm64"
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-winarm64
#else
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-win64
#endif
SetupIconFile=qMedia.ico
WizardSmallImageFile=wiz-small.bmp
WizardImageFile=wiz.bmp
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}
AppCopyright=Copyright © 2018-{#MyAppYear}, {#MyAppPublisher}
MinVersion=0,6.1
DisableProgramGroupPage=yes
ChangesAssociations=yes
Compression=lzma
SolidCompression=yes
PrivilegesRequiredOverridesAllowed=dialog
#if "MyArch" == "x86"
ArchitecturesAllowed=x86
#elif "MyArch" == "arm64"
ArchitecturesInstallIn64BitMode=arm64
ArchitecturesAllowed=arm64
#else
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startentry"; Description: "Create a start menu shortcut"; GroupDescription: "{cm:AdditionalIcons}";
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "fileassociation"; Description: "Create file associations"; GroupDescription: "Other:";

[Files]
#if "MyArch" == "x86"
Source: "qMedia-Win32/*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
#elif "MyArch" == "arm64"
Source: "qMedia-WinArm64/*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
#else
Source: "qMedia-Win64/*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
#endif
Source: "qMedia.VisualElementsManifest.xml"; DestDir: "{app}"; Flags: ignoreversion
Source: "win-tile-m.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "win-tile-s.png"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startentry
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Registry]
; Key that specifies exe path to file associations
Root: HKA; Subkey: "SOFTWARE\Classes\{#MyAppName}.1"; Flags: uninsdeletekey; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\{#MyAppName}.1\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1""""; Tasks: fileassociation

; File associations that point to the above key
Root: HKA; Subkey: "SOFTWARE\Classes\.bmp\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.cur\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.gif\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.icns\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ico\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.jpeg\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.jpg\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.jpe\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.jfi\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.jfif\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.pbm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.pgm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.png\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ppm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.svg\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.svgz\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.tif\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.tiff\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.wbmp\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.webp\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.xbm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.xpm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
; plugins
Root: HKA; Subkey: "SOFTWARE\Classes\.ani\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.apng\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.avif\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.avifs\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.bw\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.exr\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.hdr\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.heic\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.heif\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.jxl\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.kra\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ora\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.pcx\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.pic\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.psd\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ras\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.rgb\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.rgba\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.sgi\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.tga\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.xcf\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
; videos
Root: HKA; Subkey: "SOFTWARE\Classes\.3g2\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.3gp\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.asf\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.avi\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.divx\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.dv\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.f4v\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.flv\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.m1v\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.m2ts\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.m2v\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.m4v\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mkv\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mov\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mp4\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mpe\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mpeg\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mpg\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mpv\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mts\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.mxf\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ogm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ogv\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.qt\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.ts\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.vob\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.webm\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKA; Subkey: "SOFTWARE\Classes\.wmv\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.1"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation

; Capabilities keys for default programs/apps to work
Root: HKA; Subkey: "Software\{#MyAppName}"; Flags: uninsdeletekey; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#MyAppName}"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Minimal image and video viewer"; Tasks: fileassociation

Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".bmp"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".cur"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".gif"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".icns"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ico"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpeg"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpg"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpe"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jfi"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jfif"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pbm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pgm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".png"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ppm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".svg"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".svgz"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tif"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tiff"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wbmp"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webp"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xbm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xpm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
; plugins
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ani"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".apng"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avif"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avifs"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".bw"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".exr"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".hdr"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".heic"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".heif"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jxl"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".kra"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ora"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pcx"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pic"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".psd"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ras"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".rgb"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".rgba"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".sgi"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tga"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xcf"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
; videos
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".3g2"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".3gp"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".asf"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avi"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".divx"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".dv"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".f4v"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".flv"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m1v"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m2ts"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m2v"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m4v"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mkv"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mov"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp4"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpe"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpeg"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpg"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpv"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mts"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mxf"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogv"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".qt"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ts"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".vob"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webm"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation
Root: HKA; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wmv"; ValueData: "{#MyAppName}.1"; Tasks: fileassociation

Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "qMedia"; ValueData: "Software\qMedia\Capabilities"; Flags: uninsdeletevalue; Tasks: fileassociation

[Code]
// Prompt if you want to remove user data when uninstalling
// This is not for the file associations, but for the QSettings-created config files
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
        if SuppressibleMsgBox('Do you want to also delete saved settings?',
      mbConfirmation, MB_YESNO, IDYES) = IDYES
    then
    begin
      RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\qMedia');
      RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, 'Software\qMedia');
      RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, 'Software\WOW6432node\qMedia');
    end;
  end;
end;
