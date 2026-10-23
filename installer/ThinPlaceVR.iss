; Thin Place VR installer (Inno Setup 6)
; Thin Place VR - see your real wheel, hands and kit inside VR.
; Installs the OpenXR layer and registers it so every OpenXR game loads it.
; Same AppId as the old "Handy Viewer 3D", so it upgrades that install in place.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={{6B1C3E52-8F0A-4C7E-9B5D-2A7F4E1D9C30}
AppName=Thin Place VR
AppVersion={#AppVersion}
AppPublisher=symyoulater
DefaultDirName={autopf}\Thin Place VR
DisableProgramGroupPage=yes
DisableDirPage=yes
UsePreviousAppDir=no
AppComments=See your real wheel, hands and kit inside VR
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=ThinPlaceVR-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Thin Place VR
UninstallDisplayIcon={app}\ThinPlaceVR.exe
SetupIconFile=..\branding\ThinPlaceVR.ico
; Updates run while the app is open: close it first, then reopen it at the end.
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#StageDir}\ThinPlaceVR.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ThinPlaceVRCheck.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ThinPlaceVR.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ThinPlaceVR.exe.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\Microsoft.Web.WebView2.*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\XR_APILAYER_SYMYOULATER_thin_place_vr.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\shapes\*"; DestDir: "{app}\shapes"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
; Bring across settings from Handy Viewer 3D (window positions etc.) the first time.
Source: "{userdocs}\Handy Viewer 3D\config.json"; DestDir: "{userdocs}\Thin Place VR"; \
  Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall

[Dirs]
; Created now so you can see it exists; the log appears here once a game has loaded the layer.
Name: "{userdocs}\Thin Place VR"
; Log folder: games must be able to write here without admin rights.
Name: "{commonappdata}\Thin Place VR"; Permissions: users-modify

[Registry]
; Remove the old Handy Viewer 3D layer registration so games don't load two copies.
Root: HKLM64; Subkey: "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"; ValueType: none; \
  ValueName: "{autopf}\Handy Viewer 3D\XR_APILAYER_SYMYOULATER_handy_viewer.json"; Flags: deletevalue
; Implicit layer: value name is the manifest path, value 0 means enabled.
Root: HKLM64; Subkey: "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"; ValueType: dword; \
  ValueName: "{app}\XR_APILAYER_SYMYOULATER_thin_place_vr.json"; ValueData: 0; Flags: uninsdeletevalue

[Icons]
Name: "{autoprograms}\Thin Place VR"; Filename: "{app}\ThinPlaceVR.exe"
Name: "{autodesktop}\Thin Place VR"; Filename: "{app}\ThinPlaceVR.exe"
Name: "{autoprograms}\Thin Place VR Check"; Filename: "{app}\ThinPlaceVRCheck.exe"

[InstallDelete]
; Leftovers from when this was called Handy Viewer 3D.
Type: filesandordirs; Name: "{autopf}\Handy Viewer 3D"
Type: files; Name: "{autodesktop}\Handy Viewer 3D.lnk"
Type: files; Name: "{autodesktop}\Handy Viewer 3D Check.lnk"
Type: files; Name: "{autoprograms}\Handy Viewer 3D.lnk"
Type: files; Name: "{autoprograms}\Handy Viewer 3D Check.lnk"

[Run]
Filename: "{app}\ThinPlaceVR.exe"; Description: "Open Thin Place VR"; Flags: nowait postinstall runasoriginaluser
