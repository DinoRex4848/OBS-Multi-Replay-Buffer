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
AppId=OBS-Multi-Length-Replay
AppName=OBS Multi-Length Replay
AppVersion={#PluginVersion}
AppPublisher=OBS Multi-Length Replay contributors
DefaultDirName={code:GetSupportDir}
UsePreviousAppDir=no
UsePreviousPrivileges=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CreateUninstallRegKey=not IsPortable
UninstallDisplayName=OBS Multi-Length Replay
UninstallDisplayIcon={app}\unins000.exe
UninstallFilesDir={app}
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
Source: "{#KitDir}\Installer.Core.ps1"; DestDir: "{app}"; Flags: ignoreversion; Check: not IsPortable
Source: "{#KitDir}\Setup.Backend.ps1"; DestDir: "{app}"; Flags: ignoreversion; Check: not IsPortable
; Portable files are removed relative to the running uninstaller, never via
; absolute paths saved before the OBS folder was moved.
Source: "{#KitDir}\Installer.Core.ps1"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall; Check: IsPortable
Source: "{#KitDir}\Setup.Backend.ps1"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall; Check: IsPortable

[INI]
Filename: "{app}\installation.ini"; Section: "Setup"; Key: "Mode"; String: "{code:GetMode}"
Filename: "{app}\installation.ini"; Section: "Setup"; Key: "ObsRoot"; String: "{code:GetObsRoot}"; Check: not IsPortable

[Code]
var
  ModePage: TInputOptionWizardPage;
  FolderPage: TInputDirWizardPage;
  Summary: String;
  Extracted, Relaunched: Boolean;

function IsPortable: Boolean;
begin
  Result := False;
  if Assigned(ModePage) then Result := ModePage.SelectedValueIndex = 1;
end;

function GetMode(Param: String): String;
begin
  if IsPortable then Result := 'Portable' else Result := 'Standard';
end;

function GetObsRoot(Param: String): String;
begin
  Result := RemoveBackslash(FolderPage.Values[0]);
end;

function GetSupportDir(Param: String): String;
begin
  if IsPortable then Result := GetObsRoot('') + '\obs-multi-replay-setup'
  else Result := ExpandConstant('{autopf64}\OBS Multi-Length Replay');
end;

function PowerShell: String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function Quote(Value: String): String;
begin
  Result := '"' + Value + '"';
end;

function CanWriteDirectory(Directory: String): Boolean;
var Probe: String;
begin
  while not DirExists(Directory) do Directory := ExtractFileDir(Directory);
  Probe := AddBackslash(Directory) + '.obs-replay-write-check-' + GetSHA256OfString(ExpandConstant('{tmp}'));
  Result := SaveStringToFile(Probe, '', False);
  if Result then DeleteFile(Probe);
end;

procedure ExtractKit;
begin
  if not Extracted then begin
    ExtractTemporaryFiles('*');
    Extracted := True;
  end;
end;

function RunBackend(Action, Mode, Root, EngineDir, PackageDir: String; var Message: String): Boolean;
var
  Code: Integer;
  Data: AnsiString;
  ResultFile, Params: String;
begin
  ResultFile := ExpandConstant('{tmp}\replay-result.txt');
  DeleteFile(ResultFile);
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    Quote(EngineDir + '\Setup.Backend.ps1') + ' -Action ' + Action +
    ' -Mode ' + Mode + ' -ObsRoot ' + Quote(Root) +
    ' -PackageRoot ' + Quote(PackageDir) + ' -ResultPath ' + Quote(ResultFile);
  Result := Exec(PowerShell, Params, EngineDir, SW_HIDE, ewWaitUntilTerminated, Code);
  Message := 'Could not run the installation engine.';
  if LoadStringFromFile(ResultFile, Data) then Message := Utf8Decode(Data);
  Result := Result and (Code = 0);
end;

procedure InitializeWizard;
var
  Root: String;
begin
  ModePage := CreateInputOptionPage(wpLicense, 'Choose OBS installation type',
    'Install or update Multi-Length Replay', 'Which OBS installation do you use?', True, False);
  ModePage.Add('Normally installed OBS Studio');
  ModePage.Add('Portable OBS Studio');
  ModePage.SelectedValueIndex := 0;
  if CompareText(ExpandConstant('{param:MODE|Standard}'), 'Portable') = 0 then
    ModePage.SelectedValueIndex := 1;
  FolderPage := CreateInputDirPage(ModePage.ID, 'Locate OBS Studio',
    'Choose the OBS root folder', 'Select the folder containing bin\64bit\obs64.exe.', False, '');
  FolderPage.Add('OBS Studio folder:');
  Root := '';
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Root) then
    RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Root);
  if Root = '' then Root := ExpandConstant('{pf64}\obs-studio');
  FolderPage.Values[0] := ExpandConstant('{param:OBSROOT|' + Root + '}');
  WizardForm.FinishedLabel.Caption := 'Restart OBS and open Docks > Multi-Length Replay and Tools > Multi-Buffer Settings.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Code: Integer;
  Params: String;
  NeedsAdmin: Boolean;
begin
  Result := True;
  if CurPageID <> FolderPage.ID then exit;
  ExtractKit;
  Result := RunBackend('Inspect', GetMode(''), GetObsRoot(''), ExpandConstant('{tmp}'), ExpandConstant('{tmp}'), Summary);
  if not Result then begin
    SuppressibleMsgBox(Summary, mbError, MB_OK, IDOK);
    exit;
  end;
  NeedsAdmin := not IsPortable;
  if IsPortable then begin
    NeedsAdmin := not (CanWriteDirectory(GetObsRoot('')) and
      CanWriteDirectory(GetObsRoot('') + '\bin\64bit') and
      CanWriteDirectory(GetObsRoot('') + '\obs-plugins\64bit') and
      CanWriteDirectory(GetObsRoot('') + '\data\obs-plugins'));
  end;
  if NeedsAdmin and not IsAdminInstallMode then begin
    Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
      Quote(ExpandConstant('{tmp}\Setup.Relaunch.ps1')) + ' -Installer ' + Quote(ExpandConstant('{srcexe}')) +
      ' -ObsRoot ' + Quote(GetObsRoot('')) + ' -Mode ' + GetMode('');
    if WizardSilent then Params := Params + ' -Silent';
    if Exec(PowerShell, Params, '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0) then begin
      Relaunched := True;
      WizardForm.Close;
    end else SuppressibleMsgBox('Administrator access was not granted or setup could not finish.', mbError, MB_OK, IDOK);
    Result := False;
  end;
  WizardForm.DirEdit.Text := GetSupportDir('');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := Summary + NewLine + NewLine + 'Settings, hotkeys and recordings will be preserved.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Message: String;
begin
  Result := '';
  ExtractKit;
  if not RunBackend('Inspect', GetMode(''), GetObsRoot(''), ExpandConstant('{tmp}'), ExpandConstant('{tmp}'), Message) then
    Result := Message;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Message: String;
begin
  if CurStep = ssPostInstall then
    if not RunBackend('Install', GetMode(''), GetObsRoot(''), ExpandConstant('{tmp}'), ExpandConstant('{tmp}'), Message) then
      RaiseException(Message);
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if Relaunched then Confirm := False;
  if CurPageID = wpInstalling then Cancel := False;
end;

function InitializeUninstall: Boolean;
var Mode, Root, Message, SupportDir: String;
begin
  SupportDir := ExtractFileDir(ExpandConstant('{uninstallexe}'));
  Mode := GetIniString('Setup', 'Mode', '', SupportDir + '\installation.ini');
  Result := (Mode = 'Portable') or (Mode = 'Standard');
  if not Result then SuppressibleMsgBox('Installation metadata is missing. Rerun setup to repair before removing the plugin.', mbError, MB_OK, IDOK);
  if not Result then exit;
  if Mode = 'Portable' then Root := ExtractFileDir(RemoveBackslash(SupportDir))
  else Root := GetIniString('Setup', 'ObsRoot', '', SupportDir + '\installation.ini');
  Result := RunBackend('CheckRemove', Mode, Root, SupportDir, SupportDir, Message);
  if not Result then SuppressibleMsgBox(Message, mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Mode, Root, Message, SupportDir: String;
begin
  SupportDir := ExtractFileDir(ExpandConstant('{uninstallexe}'));
  if CurUninstallStep = usPostUninstall then begin
    DeleteFile(SupportDir + '\Installer.Core.ps1');
    DeleteFile(SupportDir + '\Setup.Backend.ps1');
    DeleteFile(SupportDir + '\installation.ini');
    exit;
  end;
  if CurUninstallStep <> usUninstall then exit;
  Mode := GetIniString('Setup', 'Mode', '', SupportDir + '\installation.ini');
  if Mode = 'Portable' then Root := ExtractFileDir(RemoveBackslash(SupportDir))
  else Root := GetIniString('Setup', 'ObsRoot', '', SupportDir + '\installation.ini');
  if not RunBackend('Uninstall', Mode, Root, SupportDir, SupportDir, Message) then
    RaiseException(Message);
end;
