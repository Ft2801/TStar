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
; Fix display name in Apps & Features
AppVerName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
PrivilegesRequired=admin
LicenseFile=..\LICENSE
SetupIconFile=images\Logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=..\installer_output
OutputBaseFilename=TStar_Setup_{#MyAppVersion}
Compression=lzma2/normal
SolidCompression=yes
InfoBeforeFile=..\changelog.txt
; Modern Design Assets
WizardStyle=modern
WizardImageFile=images\WizardImage.bmp
WizardSmallImageFile=images\WizardSmallImage.bmp
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

[CustomMessages]
english.UninstallOldVersion=An older version of TStar was detected. It will be uninstalled before proceeding.
italian.UninstallOldVersion=È stata rilevata una versione precedente di TStar. Verrà disinstallata prima di procedere.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\TStar\*"; \
    DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "scripts\*"

Source: "..\dist\TStar\scripts\*"; \
    DestDir: "{app}\scripts"; \
    Flags: onlyifdoesntexist recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
function GetUninstallString(): String;
var
  sUnInstPath: String;
  sUnInstallString: String;
begin
  sUnInstPath := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{B5F8E3A4-2D91-4C67-9A5E-7F2B3C8D1E9A}_is1';
  sUnInstallString := '';
  
  // Check HKLM 64-bit
  if not RegQueryStringValue(HKLM64, sUnInstPath, 'UninstallString', sUnInstallString) then
    // Check HKLM 32-bit
    if not RegQueryStringValue(HKLM32, sUnInstPath, 'UninstallString', sUnInstallString) then
      // Check HKCU
      RegQueryStringValue(HKCU, sUnInstPath, 'UninstallString', sUnInstallString);
      
  Result := sUnInstallString;
end;

function InitializeSetup(): Boolean;
var
  sUninstallString: String;
  iResultCode: Integer;
begin
  sUninstallString := GetUninstallString();
  if sUninstallString <> '' then
  begin
    // Show feedback to user
    MsgBox(CustomMessage('UninstallOldVersion'), mbInformation, MB_OK);

    // Remove quotes if present
    sUninstallString := RemoveQuotes(sUninstallString);
    Exec(sUninstallString, '/SILENT /SUPPRESSMSGBOXES', '', SW_SHOWNORMAL, ewWaitUntilTerminated, iResultCode);
  end;
  Result := True;
end;
