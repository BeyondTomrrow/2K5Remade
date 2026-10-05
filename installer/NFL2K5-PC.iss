; Native NFL 2K5 installer.  The retail game is never packaged: the player
; chooses an existing, legally extracted disc folder and the install creates a
; junction to it at {app}\original\disc.

#define MyAppName "ESPN NFL 2K5 Native PC"
#define MyAppVersion "0.1.0-preview"
#define MyAppExeName "NFL2K5.exe"

[Setup]
AppId={{CB2EF8A3-0F06-4A18-B4B0-2BFA1C68E9D3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=NFL2K5-PC Project
DefaultDirName={autopf}\ESPN NFL 2K5
DefaultGroupName=ESPN NFL 2K5
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=NFL2K5-PC-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=classic
WizardImageFile=assets\wizard-side.bmp
WizardSmallImageFile=assets\wizard-small.bmp
SetupIconFile=assets\nfl2k5.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\Release\NFL2K5.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "assets\nfl2k5.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\tools\extract-xiso.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\nfl2k5_video.ini"; DestDir: "{app}"; Flags: ignoreversion
; Ship editable presentation definitions and art by an explicit extension
; allow-list. This cannot accidentally capture local packs, Xbox executables,
; debug symbols, saves, or user-supplied audio/video.
Source: "..\mods\README.md"; DestDir: "{app}\mods"; Flags: ignoreversion
Source: "..\mods\presentation.ini"; DestDir: "{app}\mods"; Flags: ignoreversion
Source: "..\mods\presentations\*.html"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.css"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.js"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.json"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.png"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.ttf"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.md"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\presentations\*.txt"; DestDir: "{app}\mods\presentations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\teams\*.json"; DestDir: "{app}\mods\teams"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\mods\teams\*.png"; DestDir: "{app}\mods\teams"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\Music\README.txt"; DestDir: "{app}\Music"; Flags: ignoreversion
Source: "..\original\README.md"; DestDir: "{app}\original"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\PROJECT_STATUS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\HANDOFF-CHATGPT.md"; DestDir: "{app}\docs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\docs\RELEASE-NOTES.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\third-party\extract-xiso-LICENSE.txt"; DestDir: "{app}\docs\third-party"; Flags: ignoreversion

[Dirs]
Name: "{app}\original"
Name: "{app}\saves"

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\nfl2k5.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\nfl2k5.ico"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent; Check: HasGameData

[Code]
var
  GameDirPage: TInputDirWizardPage;
  SourceTypePage: TInputOptionWizardPage;
  IsoFilePage: TInputFileWizardPage;
  IsoDestinationPage: TInputDirWizardPage;
  GameSourceDir: String;
  IsoPath: String;
  IsoOutputParent: String;

function DiscXbe(const Dir: String): String;
begin
  Result := AddBackslash(Dir) + 'default.xbe';
end;

function InstalledDiscDir: String;
begin
  Result := ExpandConstant('{app}\original\disc');
end;

function HasGameData: Boolean;
begin
  Result := FileExists(DiscXbe(InstalledDiscDir));
end;

procedure InitializeWizard;
begin
  SourceTypePage := CreateInputOptionPage(wpSelectDir,
    'Choose your game source',
    'Use an extracted disc folder or extract your own ISO',
    'Choose how Setup should find the NFL 2K5 game files. Setup never downloads game data.',
    True, False);
  SourceTypePage.Add('I already have an extracted NFL 2K5 disc folder');
  SourceTypePage.Add('Extract my legally owned Xbox ISO now');
  SourceTypePage.SelectedValueIndex := 0;

  GameDirPage := CreateInputDirPage(SourceTypePage.ID,
    'Locate your extracted NFL 2K5 game files',
    'Select the folder containing your legally extracted disc',
    'Choose the folder that contains default.xbe and the vc_53450030 folder. ' +
    'The installer does not include or download game files.',
    False, '');
  GameDirPage.Add('Extracted game folder:');
  GameDirPage.Values[0] := ExpandConstant('{param:GAMEPATH|}');

  IsoFilePage := CreateInputFilePage(GameDirPage.ID,
    'Choose your NFL 2K5 ISO',
    'Select the ISO or XISO you legally created from your own game disc',
    'Setup will extract it locally. The ISO itself is never copied into the install folder.');
  IsoFilePage.Add('Xbox ISO/XISO file:', 'Xbox ISO files|*.iso;*.xiso|All files|*.*', '.iso');
  IsoFilePage.Values[0] := ExpandConstant('{param:ISO|}');

  IsoDestinationPage := CreateInputDirPage(IsoFilePage.ID,
    'Choose where to extract the game',
    'Select a parent folder for the extracted game',
    'Setup creates an ESPN NFL 2K5 folder at this location and extracts your game there.',
    False, '');
  IsoDestinationPage.Add('Parent extraction folder:');
  IsoDestinationPage.Values[0] := ExpandConstant('{param:EXTRACTTO|}');
  if IsoFilePage.Values[0] <> '' then SourceTypePage.SelectedValueIndex := 1;
end;

function UsingIso: Boolean;
begin
  Result := (ExpandConstant('{param:ISO|}') <> '') or (SourceTypePage.SelectedValueIndex = 1);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = GameDirPage.ID) and UsingIso) or
            (((PageID = IsoFilePage.ID) or (PageID = IsoDestinationPage.ID)) and not UsingIso);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = GameDirPage.ID then begin
    if GameSourceDir = '' then GameSourceDir := RemoveBackslashUnlessRoot(GameDirPage.Values[0]);
    if not FileExists(DiscXbe(GameSourceDir)) then begin
      MsgBox('Select the extracted disc folder that contains default.xbe.', mbError, MB_OK);
      Result := False;
      Exit;
    end;
    if not DirExists(AddBackslash(GameSourceDir) + 'vc_53450030') then begin
      MsgBox('That folder has default.xbe but is missing vc_53450030. Select the complete extracted disc folder.', mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = IsoFilePage.ID then begin
    IsoPath := IsoFilePage.Values[0];
    if (not FileExists(IsoPath)) or
       ((Lowercase(ExtractFileExt(IsoPath)) <> '.iso') and (Lowercase(ExtractFileExt(IsoPath)) <> '.xiso')) then begin
      MsgBox('Select an existing .iso or .xiso file.', mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = IsoDestinationPage.ID then begin
    IsoOutputParent := RemoveBackslashUnlessRoot(IsoDestinationPage.Values[0]);
    if IsoOutputParent = '' then begin
      MsgBox('Select where ESPN NFL 2K5 should be extracted.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure ExtractIso;
var
  ResultCode: Integer;
  OutputDir, Params: String;
begin
  IsoPath := IsoFilePage.Values[0];
  if IsoPath = '' then IsoPath := ExpandConstant('{param:ISO|}');
  IsoOutputParent := RemoveBackslashUnlessRoot(IsoDestinationPage.Values[0]);
  if IsoOutputParent = '' then IsoOutputParent := RemoveBackslashUnlessRoot(ExpandConstant('{param:EXTRACTTO|}'));
  OutputDir := AddBackslash(IsoOutputParent) + 'ESPN NFL 2K5';
  if DirExists(OutputDir) then
    RaiseException('The selected extraction location already contains an ESPN NFL 2K5 folder. Choose an empty parent folder.');
  if not ForceDirectories(OutputDir) then
    RaiseException('Could not create the ESPN NFL 2K5 extraction folder.');
  Params := '-x -d "' + OutputDir + '" "' + IsoPath + '"';
  if (not Exec(ExpandConstant('{app}\\tools\\extract-xiso.exe'), Params, '', SW_SHOW, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    RaiseException('ISO extraction failed. Your ISO was not changed.');
  if not FileExists(DiscXbe(OutputDir)) then
    RaiseException('Extraction finished but default.xbe was not found. Choose a valid NFL 2K5 Xbox ISO.');
  GameSourceDir := OutputDir;
end;

procedure CreateDiscLink;
var
  ResultCode: Integer;
  Target, LinkPath, Params: String;
begin
  Target := GameSourceDir;
  LinkPath := InstalledDiscDir;
  if DirExists(LinkPath) then begin
    if FileExists(DiscXbe(LinkPath)) then Exit;
    RaiseException('The install folder contains an unexpected original\\disc directory. Choose another install folder to avoid overwriting it.');
  end;
  Params := '/C mklink /J "' + LinkPath + '" "' + Target + '"';
  if (not Exec(ExpandConstant('{cmd}'), Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    RaiseException('Could not link the selected game folder. Setup has not changed your game files.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if UsingIso then ExtractIso
    else if GameSourceDir = '' then
      GameSourceDir := RemoveBackslashUnlessRoot(ExpandConstant('{param:GAMEPATH|}'));
    if not FileExists(DiscXbe(GameSourceDir)) then
      RaiseException('Setup needs a complete extracted game folder. Restart setup and select it, or use /GAMEPATH="C:\\Your\\Extracted\\Disc".');
    CreateDiscLink;
  end;
end;
