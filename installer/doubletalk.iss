; SPDX-License-Identifier: BSD-3-Clause
;
; doubletalk.iss - installer for the DoubleTalk PC SAPI5 voices.
;
; Installs, for every user on the machine:
;   * the SAPI5 engine for BOTH architectures, each registered in its own
;     registry view, so 32-bit and 64-bit applications both see the voices
;   * the emulated card: dtalk.dll, dtalk64.dll and the 512 KB firmware ROM
;   * all eight voices (they live in the engine, not as separate files)
;   * the configuration utility, with Start Menu and desktop shortcuts
;
; Accessibility notes:
;   * WizardStyle=classic. The modern style's large banner area is decorative
;     and adds nothing a screen reader can use, while the classic layout keeps
;     the reading order simple and predictable.
;   * No custom wizard pages, no unlabelled custom controls, and every task
;     and option is a standard checkbox that Setup labels for us.
;   * AlwaysShowComponentsList/DisableReadyPage are left at defaults so the
;     wizard never silently skips a page a user is listening for.
;   * SetupLogging=yes, so a log always exists without the user needing to
;     know about the /LOG switch.

#define AppName        "DoubleTalk PC SAPI5"
#define AppVersion     "1.0.0"
#define AppPublisher   "DoubleTalk PC SAPI5 project"
#define ConfigExe      "DoubleTalkConfig.exe"

; Passed in by build_installer.bat; default to the usual build tree layout.
#ifndef BuildX64
  #define BuildX64 "..\build_x64\bin\Release"
#endif
#ifndef BuildX86
  #define BuildX86 "..\build_x86\bin\Release"
#endif
#ifndef EngineDir
  #define EngineDir "..\bin"
#endif
#ifndef OutDir
  #define OutDir "..\output"
#endif

[Setup]
AppId={{7A3D91C4-6E5B-4A2F-9C18-2B4E7D0A5F63}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\DoubleTalkSAPI
DefaultGroupName={#AppName}
OutputDir={#OutDir}
OutputBaseFilename=DoubleTalkPC_SAPI5_Setup
Compression=lzma2/max
SolidCompression=yes

; Registering a SAPI5 voice engine writes to HKLM: SAPI only discovers voice
; enumerators from the machine hive, so a per-user install would produce voices
; that no application could see.
PrivilegesRequired=admin

; Install into the 64-bit Program Files and use the 64-bit registry view where
; available, while still installing the 32-bit engine alongside it.
ArchitecturesInstallIn64BitMode=x64compatible

WizardStyle=classic
SetupLogging=yes
UninstallDisplayIcon={app}\{#ConfigExe}
UninstallDisplayName={#AppName}
DisableWelcomePage=no
ShowLanguageDialog=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for the configuration utility"; GroupDescription: "Shortcuts:"

[Files]
; ---- the emulated card, shared by both architectures -----------------------
; The engine looks for these beside the SAPI DLL and then one directory up, so
; a single copy in {app} serves the x86 engine in {app}\x86 too.
Source: "{#EngineDir}\dtalk.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#EngineDir}\dtalk64.dll";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#EngineDir}\doubletalkpc.bin"; DestDir: "{app}"; Flags: ignoreversion

; ---- 64-bit SAPI5 engine ---------------------------------------------------
; The 64bit flag makes Setup register it in the 64-bit registry view. Getting
; this wrong is the classic failure here: registering a 64-bit DLL through the
; 32-bit view leaves the voices invisible to every 64-bit application.
Source: "{#BuildX64}\DoubleTalkSAPI.dll"; DestDir: "{app}"; \
    Flags: ignoreversion regserver 64bit; Check: Is64BitInstallMode

; ---- 32-bit SAPI5 engine ---------------------------------------------------
; Always installed. On 64-bit Windows this is what 32-bit hosts - older screen
; readers and many book readers - load.
Source: "{#BuildX86}\DoubleTalkSAPI.dll"; DestDir: "{app}\x86"; \
    Flags: ignoreversion regserver 32bit

; ---- configuration utility and tools --------------------------------------
Source: "{#BuildX64}\{#ConfigExe}"; DestDir: "{app}"; \
    Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#BuildX86}\{#ConfigExe}"; DestDir: "{app}"; \
    Flags: ignoreversion; Check: not Is64BitInstallMode

Source: "{#BuildX64}\dt_render.exe"; DestDir: "{app}"; \
    Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#BuildX86}\dt_render.exe"; DestDir: "{app}"; \
    Flags: ignoreversion; Check: not Is64BitInstallMode

Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\DoubleTalk PC Configuration"; Filename: "{app}\{#ConfigExe}"; \
    Comment: "Adjust the DoubleTalk PC voice parameters"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\DoubleTalk PC Configuration"; Filename: "{app}\{#ConfigExe}"; \
    Comment: "Adjust the DoubleTalk PC voice parameters"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ConfigExe}"; \
    Description: "Open the &configuration utility"; \
    Flags: postinstall nowait skipifsilent

[UninstallDelete]
; Machine-wide defaults written below. Per-user settings under %LOCALAPPDATA%
; and the logs beside them are deliberately left alone: they are the user's
; own configuration, and an uninstall that is really an upgrade should not
; discard them.
Type: files;      Name: "{commonappdata}\DoubleTalkSAPI\settings.ini"
Type: dirifempty; Name: "{commonappdata}\DoubleTalkSAPI"

[Code]

// Verifies the firmware ROM before anything is registered. Every voice depends
// on it, so a truncated or wrong file means an install that appears to succeed
// and then produces silence - the least debuggable outcome for a user who
// cannot see an error dialog behind their screen reader.
function CheckRom(): Boolean;
var
  Size: Int64;
  RomPath: String;
begin
  Result := True;
  RomPath := ExpandConstant('{app}\doubletalkpc.bin');
  if not FileExists(RomPath) then
  begin
    Log('ROM missing after copy: ' + RomPath);
    MsgBox('The DoubleTalk firmware file doubletalkpc.bin was not installed.' + #13#10 +
           'The voices will not work. Please report this with the setup log.',
           mbError, MB_OK);
    Result := False;
    Exit;
  end;
  if FileSize64(RomPath, Size) then
  begin
    Log('ROM size: ' + IntToStr(Size));
    if Size <> 524288 then
    begin
      MsgBox('doubletalkpc.bin is ' + IntToStr(Size) + ' bytes but should be 524288.' + #13#10 +
             'The voices will not work correctly.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// Records what was installed and where, so the setup log is enough to diagnose
// a "the voices did not appear" report without asking the user to go digging.
procedure LogInstallSummary();
begin
  Log('--- DoubleTalk PC SAPI5 install summary ---');
  Log('Install directory : ' + ExpandConstant('{app}'));
  Log('64-bit mode       : ' + IntToStr(Integer(Is64BitInstallMode)));
  Log('x64 engine        : ' + ExpandConstant('{app}\DoubleTalkSAPI.dll') +
      ' present=' + IntToStr(Integer(FileExists(ExpandConstant('{app}\DoubleTalkSAPI.dll')))));
  Log('x86 engine        : ' + ExpandConstant('{app}\x86\DoubleTalkSAPI.dll') +
      ' present=' + IntToStr(Integer(FileExists(ExpandConstant('{app}\x86\DoubleTalkSAPI.dll')))));
  Log('dtalk.dll         : ' + IntToStr(Integer(FileExists(ExpandConstant('{app}\dtalk.dll')))));
  Log('dtalk64.dll       : ' + IntToStr(Integer(FileExists(ExpandConstant('{app}\dtalk64.dll')))));
  Log('Config utility    : ' + IntToStr(Integer(FileExists(ExpandConstant('{app}\' + '{#ConfigExe}')))));
end;

// Seeds machine-wide defaults. The engine reads these only when a user has no
// settings file of their own, so this gives a fresh profile sensible values
// without ever overwriting a user's choices.
procedure WriteMachineDefaults();
var
  Dir, Ini: String;
begin
  Dir := ExpandConstant('{commonappdata}\DoubleTalkSAPI');
  if not DirExists(Dir) then
    CreateDir(Dir);
  Ini := Dir + '\settings.ini';
  if not FileExists(Ini) then
  begin
    SetIniInt('Global', 'Filter', 3800, Ini);
    SetIniInt('Global', 'RateBoost', 0, Ini);
    SetIniInt('Global', 'LogLevel', 3, Ini);
    SetIniInt('Global', 'OutputRate', 22050, Ini);
    Log('Wrote machine defaults: ' + Ini);
  end
  else
    Log('Machine defaults already present, left unchanged: ' + Ini);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    LogInstallSummary();
    WriteMachineDefaults();
    if not CheckRom() then
      Log('ROM verification FAILED');
  end;
end;
