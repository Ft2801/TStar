; TStar Installer Script for Inno Setup
; Download Inno Setup from https://jrsoftware.org/isinfo.php

#define MyAppName "TStar"
; Version is passed from command line via /DMyAppVersion="x.x.x"
; Default to 1.0.0 if not specified
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#define MyAppPublisher "Fabio Tempera"
#define MyAppURL "https://github.com/Ft2801/TStar"
#define MyAppExeName "TStar.exe"

[Setup]
AppId={{B5F8E3A4-2D91-4C67-9A5E-7F2B3C8D1E9A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
PrivilegesRequired=admin
LicenseFile=LICENSE
OutputDir=installer_output
OutputBaseFilename=TStar_Setup_{#MyAppVersion}
Compression=lzma2/normal
SolidCompression=yes
InfoBeforeFile=changelog.txt
; Modern Design Assets
WizardStyle=modern
WizardImageFile=src\images\Pillars.jpg
WizardSmallImageFile=src\images\Logo.png
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
; Show version in installer title
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription=TStar Astrophotography Application Setup
VersionInfoCopyright=Copyright (C) 2026 Fabio Tempera
VersionInfoProductName=TStar
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main executable and all files from dist folder
Source: "dist\TStar\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
