; Inno Setup script for Mouse Sliding OBS plugin
; Compile with: ISCC.exe installer\mouse-sliding.iss
; Requires dist\mouse-sliding\ with bin\64bit\ and data\

#define MyAppName "Mouse Sliding (OBS)"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Discrutans"
#define MyAppURL "https://github.com/Discrutans/mouse-sliding"
#define MyPluginName "mouse-sliding"

[Setup]
AppId={{B8D4F2A0-6C1E-4F9B-8D22-1A6F3E9C7B42}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={commonappdata}\obs-studio\plugins\{#MyPluginName}
DisableDirPage=yes
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename={#MyPluginName}-windows-x64-v{#MyAppVersion}-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\bin\64bit\{#MyPluginName}.dll
InfoAfterFile=after-install.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "..\dist\{#MyPluginName}\bin\64bit\*"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion recursesubdirs
Source: "..\dist\{#MyPluginName}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs

[Code]
procedure RemoveLegacyProgramFilesCopies;
var
  Plugins64, DataPlugins: String;
begin
  { Old manual installs into OBS Program Files shadow ProgramData and hide author links. }
  Plugins64 := ExpandConstant('{commonpf64}\obs-studio\obs-plugins\64bit');
  DataPlugins := ExpandConstant('{commonpf64}\obs-studio\data\obs-plugins');
  DeleteFile(Plugins64 + '\{#MyPluginName}.dll');
  DeleteFile(Plugins64 + '\{#MyPluginName}.pdb');
  DelTree(Plugins64 + '\{#MyPluginName}', True, True, True);
  DelTree(DataPlugins + '\{#MyPluginName}', True, True, True);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not DirExists(ExpandConstant('{commonappdata}\obs-studio')) then
  begin
    MsgBox('OBS Studio data folder was not found under ProgramData.'#13#10 +
           'The plugin will still be installed; start OBS once after install.',
           mbInformation, MB_OK);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RemoveLegacyProgramFilesCopies;
end;
