; The installer of Anime Download Manager, built by Inno Setup 6.
;
; Everything the application needs goes into Program Files: the executable,
; the host of the browser extension, the layers of the folder icons with the
; portable ImageMagick, and the toolbar packs. The application only reads
; there; what it writes lives in %APPDATA%\anime-dm.
;
; Defines the build passes: AppVersion (N.NN), BuildDir (anime-dm.exe and
; adm-host.exe), MagickDir (the portable ImageMagick, unpacked).

#ifndef AppVersion
  #define AppVersion "0.00"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#ifndef MagickDir
  #define MagickDir "..\magick"
#endif

#define AppName "Anime Download Manager"
#define AppExe "anime-dm.exe"
#define HostName "com.animedm.host"
#define ChromiumExtension "kajalpjiomebkclalgjggcgjeiibkfcg"
#define FirefoxExtension "adm@animedm.app"

[Setup]
AppId={{86219BCB-ED33-48F8-86F3-DF3C6B00CA89}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=goddivor
AppPublisherURL=https://goddivor.github.io/anime-dm/
AppSupportURL=https://goddivor.github.io/anime-dm/support/
AppUpdatesURL=https://github.com/goddivor/anime-dm/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=Output
OutputBaseFilename=AnimeDownloadManager-{#AppVersion}-setup
SetupIconFile=..\app\resources\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
; An update runs while the application may still be open, and the browser may
; hold the host of the extension: both are closed first.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.AddonsTitle=Désinstallation d'Anime Download Manager
french.AddonsNote=Votre liste, vos réglages et les caches de l'application vont être supprimés. Vos vidéos téléchargées ne sont pas touchées.%n%nLes add-ons installés sont gardés : ils reviendront tels quels si vous réinstallez l'application.
french.AddonsRemove=Supprimer aussi les add-ons installés
french.AddonsContinue=Continuer
english.AddonsTitle=Uninstalling Anime Download Manager
english.AddonsNote=Your list, your settings and the caches of the application are about to be removed. Your downloaded videos are not touched.%n%nThe installed add-ons are kept: they come back as they are if you install the application again.
english.AddonsRemove=Also remove the installed add-ons
english.AddonsContinue=Continue

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\adm-host.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\folder-templates\images\*"; DestDir: "{app}\resources\folder-templates\images"; Flags: ignoreversion recursesubdirs
Source: "{#MagickDir}\magick.exe"; DestDir: "{app}\resources\folder-templates\bin"; Flags: ignoreversion
Source: "{#MagickDir}\*.xml"; DestDir: "{app}\resources\folder-templates\bin"; Flags: ignoreversion
Source: "{#MagickDir}\*.icc"; DestDir: "{app}\resources\folder-templates\bin"; Flags: ignoreversion
Source: "{#MagickDir}\LICENSE.txt"; DestDir: "{app}\resources\folder-templates\bin"; Flags: ignoreversion
Source: "{#MagickDir}\NOTICE.txt"; DestDir: "{app}\resources\folder-templates\bin"; Flags: ignoreversion
Source: "..\resources\toolbar\*"; DestDir: "{app}\resources\toolbar"; Flags: ignoreversion recursesubdirs
Source: "..\resources\extension\*"; DestDir: "{app}\resources\extension"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; An update the application started runs silently, then opens it again.
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: WizardSilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "CloseApp"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM adm-host.exe"; Flags: runhidden; RunOnceId: "CloseHost"

[Code]
var
  RemoveAddons: Boolean;

// Asks, once the uninstall is confirmed, whether the add-ons go too; they
// stay unless the box is ticked, and a silent uninstall keeps them.
procedure AskAboutAddons();
var
  Form: TSetupForm;
  Note: TNewStaticText;
  Check: TNewCheckBox;
  Button: TNewButton;
begin
  RemoveAddons := False;
  if UninstallSilent then
    Exit;
  Form := CreateCustomForm();
  try
    Form.ClientWidth := ScaleX(420);
    Form.ClientHeight := ScaleY(170);
    Form.Caption := ExpandConstant('{cm:AddonsTitle}');

    Note := TNewStaticText.Create(Form);
    Note.Parent := Form;
    Note.AutoSize := False;
    Note.WordWrap := True;
    Note.Left := ScaleX(16);
    Note.Top := ScaleY(16);
    Note.Width := Form.ClientWidth - ScaleX(32);
    Note.Height := ScaleY(80);
    Note.Caption := ExpandConstant('{cm:AddonsNote}');

    Check := TNewCheckBox.Create(Form);
    Check.Parent := Form;
    Check.Left := ScaleX(16);
    Check.Top := ScaleY(104);
    Check.Width := Form.ClientWidth - ScaleX(32);
    Check.Height := ScaleY(17);
    Check.Caption := ExpandConstant('{cm:AddonsRemove}');

    Button := TNewButton.Create(Form);
    Button.Parent := Form;
    Button.Width := ScaleX(90);
    Button.Height := ScaleY(25);
    Button.Left := Form.ClientWidth - Button.Width - ScaleX(16);
    Button.Top := Form.ClientHeight - Button.Height - ScaleY(12);
    Button.Caption := ExpandConstant('{cm:AddonsContinue}');
    Button.ModalResult := mrOk;
    Button.Default := True;
    Button.Cancel := True;

    Form.ActiveControl := Button;
    Form.ShowModal();
    RemoveAddons := Check.Checked;
  finally
    Form.Free();
  end;
end;

// Empties the data folder of the application, the add-ons aside when they
// are kept.
procedure RemoveData();
var
  Dir: String;
  Found: TFindRec;
begin
  Dir := ExpandConstant('{userappdata}\anime-dm');
  if FindFirst(Dir + '\*', Found) then
  begin
    try
      repeat
        if (Found.Name <> '.') and (Found.Name <> '..') and
           (RemoveAddons or (CompareText(Found.Name, 'addons') <> 0)) then
        begin
          if (Found.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
            DelTree(Dir + '\' + Found.Name, True, True, True)
          else
            DeleteFile(Dir + '\' + Found.Name);
        end;
      until not FindNext(Found);
    finally
      FindClose(Found);
    end;
  end;
  RemoveDir(Dir);
end;

// Takes back from the registry what the application wrote there: its start
// with the session, the host it declared to each browser, and the requests to
// install the extension.
procedure RemoveRegistry();
var
  Browsers: TArrayOfString;
  Index: Integer;
begin
  RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AnimeDownloadManager');
  SetArrayLength(Browsers, 6);
  Browsers[0] := 'Software\Google\Chrome';
  Browsers[1] := 'Software\Microsoft\Edge';
  Browsers[2] := 'Software\BraveSoftware\Brave-Browser';
  Browsers[3] := 'Software\Chromium';
  Browsers[4] := 'Software\Vivaldi';
  Browsers[5] := 'Software\Opera Software';
  for Index := 0 to GetArrayLength(Browsers) - 1 do
  begin
    RegDeleteKeyIncludingSubkeys(HKCU, Browsers[Index] + '\NativeMessagingHosts\{#HostName}');
    RegDeleteKeyIncludingSubkeys(HKCU, Browsers[Index] + '\Extensions\{#ChromiumExtension}');
  end;
  RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Mozilla\NativeMessagingHosts\{#HostName}');
  RegDeleteValue(HKCU, 'Software\Mozilla\Firefox\Extensions', '{#FirefoxExtension}');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    AskAboutAddons();
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveRegistry();
    RemoveData();
  end;
end;
