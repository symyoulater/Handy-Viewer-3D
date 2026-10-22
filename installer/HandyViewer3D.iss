; Handy Viewer 3D installer (Inno Setup 6)
; Installs the OpenXR layer and registers it so every OpenXR game loads it.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={{6B1C3E52-8F0A-4C7E-9B5D-2A7F4E1D9C30}
AppName=Handy Viewer 3D
AppVersion={#AppVersion}
AppPublisher=symyoulater
DefaultDirName={autopf}\Handy Viewer 3D
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=HandyViewer3D-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Handy Viewer 3D

[Files]
Source: "{#StageDir}\HandyViewer3D.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\XR_APILAYER_SYMYOULATER_handy_viewer.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\shapes\*"; DestDir: "{app}\shapes"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Dirs]
; Created now so you can see it exists; the log appears here once a game has loaded the layer.
Name: "{userdocs}\Handy Viewer 3D"
; Log folder: games must be able to write here without admin rights.
Name: "{commonappdata}\Handy Viewer 3D"; Permissions: users-modify

[Registry]
; Implicit layer: value name is the manifest path, value 0 means enabled.
Root: HKLM64; Subkey: "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"; ValueType: dword; \
  ValueName: "{app}\XR_APILAYER_SYMYOULATER_handy_viewer.json"; ValueData: 0; Flags: uninsdeletevalue
