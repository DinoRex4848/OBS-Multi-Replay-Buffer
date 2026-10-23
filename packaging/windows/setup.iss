#ifndef PluginVersion
  #error PluginVersion is required
#endif
#ifndef KitDir
  #error KitDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

[Setup]
; Separate from the obsolete 0.1.0 registration retired by the transaction engine.
AppId=OBS-Multi-Replay-Buffer
AppName=OBS Multi-Length Replay
AppVersion={#PluginVersion}
AppPublisher=OBS Multi-Length Replay contributors
DefaultDirName={autopf64}\OBS Multi Replay Buffer
UsePreviousAppDir=no
UsePreviousPrivileges=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayName=OBS Multi-Length Replay
OutputDir={#OutputDir}
OutputBaseFilename=obs-multi-replay-{#PluginVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
LicenseFile={#KitDir}\payload\obs-multi-replay\data\LICENSE
SetupLogging=yes
SetupMutex=Global\OBS-Multi-Length-Replay-Setup

[Files]
Source: "{#KitDir}\*"; DestDir: "{tmp}"; Flags: dontcopy recursesubdirs
Source: "{#KitDir}\Installer.Core.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#KitDir}\Setup.Backend.ps1"; DestDir: "{app}"; Flags: ignoreversion

[INI]
Filename: "{app}\installation.ini"; Section: "Setup"; Key: "ObsRoot"; String: "{code:GetObsRoot}"

[UninstallDelete]
Type: files; Name: "{app}\installation.ini"

[Code]
var
  FolderPage: TInputDirWizardPage;
  Extracted: Boolean;
  Summary: String;

function Quote(Value: String): String;
begin
  Result := '"' + Value + '"';
end;

procedure ExtractKit;
begin
  if not Extracted then begin
    ExtractTemporaryFiles('*');
    Extracted := True;
  end;
end;

function GetObsRoot(Param: String): String;
begin
  Result := RemoveBackslash(Trim(FolderPage.Values[0]));
end;

function RunBackend(Action, Root, EngineDir: String; var Message: String): Boolean;
var
  Code: Integer;
  Data: AnsiString;
  ResultFile, Params: String;
begin
  ResultFile := ExpandConstant('{tmp}\replay-result.txt');
  DeleteFile(ResultFile);
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    Quote(EngineDir + '\Setup.Backend.ps1') + ' -Action ' + Action;
  if Root <> '' then Params := Params + ' -ObsRoot ' + Quote(Root);
  Params := Params + ' -PackageRoot ' + Quote(EngineDir) +
    ' -ResultPath ' + Quote(ResultFile);
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Params, EngineDir, SW_HIDE, ewWaitUntilTerminated, Code);
  Message := 'Could not run the installation engine. See the setup log in %TEMP%.';
  if LoadStringFromFile(ResultFile, Data) then Message := Utf8Decode(Data);
  Log(Action + ': ' + Message);
  Result := Result and (Code = 0);
end;

procedure InitializeWizard;
var Root: String;
begin
  ExtractKit;
  if not RunBackend('FindObs', '', ExpandConstant('{tmp}'), Root) then Root := '';
  FolderPage := CreateInputDirPage(wpLicense, 'Locate normally installed OBS Studio',
    'Install or repair Multi-Length Replay',
    'Close OBS. Select its root folder containing bin\64bit\obs64.exe.' + #13#10 +
    'Portable OBS uses the separate ZIP package. Your profiles, scenes, hotkeys and recordings are preserved.', False, '');
  FolderPage.Add('OBS Studio folder:');
  FolderPage.Values[0] := ExpandConstant('{param:OBSROOT|}');
  if FolderPage.Values[0] = '' then FolderPage.Values[0] := Root;
  WizardForm.FinishedLabel.Caption := 'Restart OBS. Open Docks > Multi-Length Replay and Tools > Multi-Buffer Settings.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = FolderPage.ID then
    Result := RunBackend('Inspect', GetObsRoot(''), ExpandConstant('{tmp}'), Summary);
  if not Result then MsgBox(Summary, mbError, MB_OK);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := Summary + NewLine + NewLine + 'Settings, scenes, hotkeys and recordings will be preserved.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Message: String;
begin
  Result := '';
  if not RunBackend('Inspect', GetObsRoot(''), ExpandConstant('{tmp}'), Message) then Result := Message;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Message: String;
begin
  // Fail before Inno writes support files or a new uninstall registration.
  if CurStep = ssInstall then
    if not RunBackend('Install', GetObsRoot(''), ExpandConstant('{tmp}'), Message) then
      RaiseException(Message);
end;

function InitializeUninstall: Boolean;
var Message: String;
begin
  Result := RunBackend('CheckRemove', '', ExpandConstant('{app}'), Message);
  if not Result then MsgBox(Message, mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Root, Message: String;
begin
  if CurUninstallStep <> usUninstall then exit;
  Root := GetIniString('Setup', 'ObsRoot', '', ExpandConstant('{app}\installation.ini'));
  if not RunBackend('Uninstall', Root, ExpandConstant('{app}'), Message) then
    RaiseException(Message);
end;
