; Kennel.gg Wardogs Streaming Tool - Windows installer (Inno Setup 6)
; Installs the portable-plugin layout into OBS's shared plugin folder, which OBS 30+ scans on start:
;   C:\ProgramData\obs-studio\plugins\kennelgg\bin\64bit\kennelgg.dll  +  data\

#ifndef VERSION
  #define VERSION "0.0.0"
#endif
#ifndef SRC
  #define SRC "..\release\RelWithDebInfo\kennelgg"
#endif
#ifndef OUTDIR
  #define OUTDIR "..\release"
#endif
#ifndef APPSRC
  #define APPSRC "..\release\app\ClipHound"
#endif

[Setup]
AppId={{7C1E6B0A-4F5D-4C7B-9C0E-KENNELWD0001}
AppName=Kennel.gg Wardogs Streaming Tool
AppVersion={#VERSION}
AppVerName=Kennel.gg Wardogs Streaming Tool {#VERSION}
AppPublisher=Sombrero / The Kennel
AppPublisherURL=https://kennel.gg
DefaultDirName={commonappdata}\obs-studio\plugins\kennelgg
; The AppId is the same as before 0.7.0 so Windows sees an upgrade, not a second program - but Inno
; then reuses the PREVIOUS install folder by default, which put 0.7.0 back into plugins\kennel-wardogs
; where OBS cannot find a module called kennelgg. Always install where DefaultDirName says.
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#OUTDIR}
OutputBaseFilename=kennelgg-{#VERSION}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Kennel.gg Wardogs Streaming Tool
WizardStyle=modern
SetupLogging=yes

[Types]
Name: "full"; Description: "OBS plugin + ClipHound app (recommended)"
Name: "plugin"; Description: "OBS plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "plugin"; Description: "Kennel.gg Wardogs OBS plugin (POV swap, clips)"; Types: full plugin custom; Flags: fixed
Name: "app"; Description: "ClipHound - kill-feed OCR clipping app (auto-started by the plugin)"; Types: full

[InstallDelete]
; the plugin used to live under its old module id; two copies would both load
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\kennel-wardogs"
; and under its first name (POVBridge). A stale copy loads next to this one, holds sources of its own
; at exit and fights over ClipHound's bridge port
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\povbridge"
Type: files; Name: "{autopf}\obs-studio\obs-plugins\64bit\povbridge.dll"
Type: files; Name: "{autopf}\obs-studio\obs-plugins\64bit\povbridge.pdb"
Type: filesandordirs; Name: "{autopf}\obs-studio\data\obs-plugins\povbridge"
Type: filesandordirs; Name: "{commonprograms}\Kennel WARDOGS"

[Dirs]
Name: "{commonappdata}\Kennel.gg\ClipHound"; Permissions: users-modify; Components: app

[Files]
Source: "{#SRC}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: plugin
Source: "{#APPSRC}\*"; DestDir: "{commonappdata}\Kennel.gg\ClipHound"; Excludes: "config.yaml"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: app
Source: "{#APPSRC}\config.default.yaml"; DestDir: "{commonappdata}\Kennel.gg\ClipHound"; DestName: "config.yaml"; Flags: onlyifdoesntexist uninsneveruninstall; Components: app

[Code]
// ClipHound moved from ProgramData\Kennel WARDOGS to ProgramData\Kennel.gg: carry its config over once,
// then take the old folder away so nothing stale is left running from it.
procedure CurStepChanged(CurStep: TSetupStep);
var
  OldDir, NewDir, OldPlugin: String;
begin
  if CurStep = ssInstall then begin
    OldDir := ExpandConstant('{commonappdata}\Kennel WARDOGS\ClipHound');
    NewDir := ExpandConstant('{commonappdata}\Kennel.gg\ClipHound');
    if DirExists(OldDir) then begin
      if FileExists(OldDir + '\config.yaml') and not FileExists(NewDir + '\config.yaml') then begin
        ForceDirectories(NewDir);
        FileCopy(OldDir + '\config.yaml', NewDir + '\config.yaml', False);
      end;
      DelTree(ExpandConstant('{commonappdata}\Kennel WARDOGS'), True, True, True);
    end;
    // Two copies of the plugin both load and fight over ClipHound's bridge port; InstallDelete
    // cannot remove a DLL that a still-running OBS has open, so say so plainly rather than leave
    // someone with a plugin that sits at "starting" for ever.
    OldPlugin := ExpandConstant('{commonappdata}\obs-studio\plugins\kennel-wardogs');
    if not DirExists(OldPlugin) then
      OldPlugin := ExpandConstant('{commonappdata}\obs-studio\plugins\povbridge');
    if DirExists(OldPlugin) then begin
      DelTree(OldPlugin, True, True, True);
      if DirExists(OldPlugin) then
        MsgBox('The previous version could not be removed from' + #13#10 + OldPlugin + #13#10#13#10 +
               'That is nearly always because OBS is still running. Close OBS, delete that folder by hand, ' +
               'then start OBS again - with both versions installed they fight over ClipHound''s connection ' +
               'and clips never fire.', mbError, MB_OK);
    end;
  end;
end;

[Icons]
Name: "{commonprograms}\Kennel.gg\ClipHound"; Filename: "{commonappdata}\Kennel.gg\ClipHound\ClipHound.exe"; WorkingDir: "{commonappdata}\Kennel.gg\ClipHound"; Components: app


[Messages]
WelcomeLabel2=This installs the Kennel.gg Wardogs OBS plugin into OBS Studio's plugin folder and, optionally, the ClipHound clipping app, which the plugin starts and configures from inside OBS.%n%nClose OBS before continuing. After installing, start OBS and open View > Docks > Kennel.gg Wardogs.%n%nMade by The Kennel [KNL], the WARDOGS community at kennel.gg - guides, Bootcamp, loadout builder, leaderboard and Discord. Free, and built from what streamers ask for.

[Code]
function IsOBSRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if Exec('cmd.exe', '/c tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" >nul', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while IsOBSRunning() do
  begin
    if MsgBox('OBS Studio is running. Close it, then press Retry.', mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

// ----- uninstall: keep the settings (the default) or remove them for a fresh start -----
var
  RemoveSettings: Boolean;

function InitializeUninstall(): Boolean;
var
  Form: TSetupForm;
  Info, Detail: TNewStaticText;
  Box: TNewCheckBox;
  OkBtn, CancelBtn: TNewButton;
begin
  Result := True;
  RemoveSettings := False;
  while IsOBSRunning() do
  begin
    if MsgBox('OBS Studio is running. Close it, then press Retry.', mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
  // a silent uninstall (scripts, a reinstall) never takes anyone's settings
  if UninstallSilent() then
    Exit;

  Form := CreateCustomForm();
  try
    Form.Caption := 'Uninstall Kennel.gg Wardogs Streaming Tool';
    Form.ClientWidth := ScaleX(460);
    Form.ClientHeight := ScaleY(250);
    Form.Position := poScreenCenter;

    Info := TNewStaticText.Create(Form);
    Info.Parent := Form;
    Info.Left := ScaleX(16);
    Info.Top := ScaleY(16);
    Info.Width := Form.ClientWidth - ScaleX(32);
    Info.AutoSize := False;
    Info.WordWrap := True;
    Info.Height := ScaleY(46);
    Info.Caption := 'This removes the OBS plugin and the ClipHound app. Your settings are kept, so ' +
                    'installing again later picks up where you left off.';

    Box := TNewCheckBox.Create(Form);
    Box.Parent := Form;
    Box.Left := ScaleX(16);
    Box.Top := Info.Top + Info.Height + ScaleY(8);
    Box.Width := Form.ClientWidth - ScaleX(32);
    Box.Height := ScaleY(20);
    Box.Caption := 'Also remove all my settings (start fresh)';
    Box.Checked := False;

    Detail := TNewStaticText.Create(Form);
    Detail.Parent := Form;
    Detail.Left := ScaleX(34);
    Detail.Top := Box.Top + Box.Height + ScaleY(4);
    Detail.Width := Form.ClientWidth - ScaleX(50);
    Detail.AutoSize := False;
    Detail.WordWrap := True;
    Detail.Height := ScaleY(96);
    Detail.Caption := 'Squad list, hotkeys, dock and setup answers, the clip list, ClipHound''s settings ' +
                      'and Twitch login, its logs, learned weapon icons and language packs. Your recorded ' +
                      'clips are not touched. Sources the plugin added to your OBS scenes (named ' +
                      '"Kennel.gg ...") stay in OBS: delete them there if you want them gone too.';

    OkBtn := TNewButton.Create(Form);
    OkBtn.Parent := Form;
    OkBtn.Caption := 'Uninstall';
    OkBtn.Width := ScaleX(90);
    OkBtn.Height := ScaleY(26);
    OkBtn.Left := Form.ClientWidth - ScaleX(16) - 2 * OkBtn.Width - ScaleX(8);
    OkBtn.Top := Form.ClientHeight - ScaleY(16) - OkBtn.Height;
    OkBtn.ModalResult := mrOk;
    OkBtn.Default := True;

    CancelBtn := TNewButton.Create(Form);
    CancelBtn.Parent := Form;
    CancelBtn.Caption := 'Cancel';
    CancelBtn.Width := OkBtn.Width;
    CancelBtn.Height := OkBtn.Height;
    CancelBtn.Left := Form.ClientWidth - ScaleX(16) - CancelBtn.Width;
    CancelBtn.Top := OkBtn.Top;
    CancelBtn.ModalResult := mrCancel;
    CancelBtn.Cancel := True;

    Form.ActiveControl := OkBtn;
    if Form.ShowModal() <> mrOk then
      Result := False
    else
      RemoveSettings := Box.Checked;
  finally
    Form.Free();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  // ClipHound runs from the folder being removed, and can outlive OBS
  if CurUninstallStep = usUninstall then
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM ClipHound.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if (CurUninstallStep = usPostUninstall) and RemoveSettings then
  begin
    DelTree(ExpandConstant('{userappdata}\obs-studio\plugin_config\kennelgg'), True, True, True);
    DelTree(ExpandConstant('{userappdata}\obs-studio\plugin_config\kennel-wardogs'), True, True, True);
    DelTree(ExpandConstant('{commonappdata}\Kennel.gg'), True, True, True);
    DelTree(ExpandConstant('{commonappdata}\Kennel WARDOGS'), True, True, True);
    DelTree(ExpandConstant('{app}'), True, True, True);
  end;
end;
