; Instalador de OBS SoundGrid Input para Windows x64 (Inno Setup 6).
; Lo genera packaging\windows\build-installer.ps1, que pasa AppVersion, RootDir y BinDir.
; Instala en %ProgramData%\obs-studio\plugins\obs-soundgrid-input (formato de plugins de OBS 33):
;   obs-soundgrid-input.dll, data\locale\*.ini, docs\
; No instala ni modifica OBS, el driver de Waves ni la consola.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef RootDir
  #define RootDir "..\.."
#endif
#ifndef BinDir
  #define BinDir RootDir + "\build\src\obs\Release"
#endif

#define AppName "OBS SoundGrid Input"
#define PluginName "obs-soundgrid-input"
#define Publisher "Joan Palacio"

[Setup]
AppId={{6E0C3A52-7D41-4B8E-9C1F-2A5D8B47F3C9}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppCopyright=Copyright (C) 2026 {#Publisher} · GPLv3
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Instalador de {#AppName} (plugin de OBS Studio para Waves SoundGrid)
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableDirPage=yes
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=no
; Escribe en %ProgramData%: pide permisos de administrador (UAC). /CURRENTUSER permite instalar
; sin elevar si la carpeta de plugins es escribible por el usuario.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#RootDir}\LICENSE
OutputDir={#RootDir}\dist
OutputBaseFilename={#PluginName}-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion} (plugin de OBS)
UninstallDisplayIcon={sys}\shell32.dll,168
CloseApplications=no

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
spanish.ObsRunning=OBS Studio está abierto.%n%nCiérralo para continuar (el plugin no se puede reemplazar mientras OBS lo usa) y pulsa Reintentar.
english.ObsRunning=OBS Studio is running.%n%nClose it to continue (the plugin cannot be replaced while OBS uses it) and press Retry.
spanish.ObsMissing=No se encontró OBS Studio en este equipo.%n%nEl plugin necesita OBS Studio 33 o posterior (64 bits). Puedes instalarlo igualmente y añadir OBS después.%n%n¿Continuar?
english.ObsMissing=OBS Studio was not found on this computer.%n%nThe plugin needs OBS Studio 33 or later (64-bit). You can install it anyway and add OBS later.%n%nContinue?
spanish.WavesMissing=Aviso: no se encontró el driver Waves SoundGrid V17. Para recibir audio de la consola instálalo con Waves Central, elige la tarjeta de red en su Control Panel y asigna el driver en la LV1 (Setup > Inventory).
english.WavesMissing=Note: the Waves SoundGrid V17 driver was not found. To receive audio from the console, install it with Waves Central, choose the network card in its Control Panel and assign the driver on the LV1 (Setup > Inventory).
spanish.WavesFound=Se detectó el driver Waves SoundGrid. En OBS, añade la fuente "SoundGrid Input (Waves)": se configura sola con el driver y la tarjeta de red.
english.WavesFound=The Waves SoundGrid driver was detected. In OBS, add the "SoundGrid Input (Waves)" source: it configures itself with the driver and the network card.
spanish.OpenObs=Abrir OBS Studio
english.OpenObs=Open OBS Studio
spanish.OpenGuide=Ver la guía de instalación y configuración
english.OpenGuide=Open the installation and setup guide
spanish.GuideShortcut=Guía de instalación
english.GuideShortcut=Installation guide
spanish.TroubleShortcut=Solución de problemas
english.TroubleShortcut=Troubleshooting
spanish.AlertsShortcut=Alertas
english.AlertsShortcut=Alerts

[InstallDelete]
; Formato antiguo (bin\64bit) de instalaciones previas.
Type: filesandordirs; Name: "{app}\bin"

[UninstallDelete]
; Carpetas que pudo crear una instalación manual anterior (sin instalador): solo si quedan vacías.
Type: dirifempty; Name: "{app}\data\locale"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}\docs"
Type: dirifempty; Name: "{app}"

[Files]
Source: "{#BinDir}\{#PluginName}.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RootDir}\LICENSE"; DestDir: "{app}\docs"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RootDir}\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RootDir}\docs\INSTALL.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RootDir}\docs\TROUBLESHOOTING.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RootDir}\docs\ALERTS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RootDir}\docs\AUDIO_MONITOR.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RootDir}\docs\LIMITATIONS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RootDir}\docs\RELEASE_NOTES.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\{cm:GuideShortcut}"; Filename: "{win}\notepad.exe"; Parameters: """{app}\docs\INSTALL.md"""
Name: "{group}\{cm:TroubleShortcut}"; Filename: "{win}\notepad.exe"; Parameters: """{app}\docs\TROUBLESHOOTING.md"""
Name: "{group}\{cm:AlertsShortcut}"; Filename: "{win}\notepad.exe"; Parameters: """{app}\docs\ALERTS.md"""
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Run]
Filename: "{win}\notepad.exe"; Parameters: """{app}\docs\INSTALL.md"""; Description: "{cm:OpenGuide}"; Flags: postinstall nowait skipifsilent unchecked
Filename: "{code:ObsExe}"; WorkingDir: "{code:ObsDir}"; Description: "{cm:OpenObs}"; Flags: postinstall nowait skipifsilent unchecked; Check: ObsInstalled

[Code]
function ObsInstallDir(): String;
begin
  Result := '';
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    Result := '';
  if (Result = '') and DirExists(ExpandConstant('{commonpf64}\obs-studio')) then
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

function ObsDir(Param: String): String;
begin
  Result := AddBackslash(ObsInstallDir()) + 'bin\64bit';
end;

function ObsExe(Param: String): String;
begin
  Result := ObsDir('') + '\obs64.exe';
end;

function ObsInstalled(): Boolean;
begin
  Result := (ObsInstallDir() <> '') and FileExists(ObsExe(''));
end;

function WavesDriverInstalled(): Boolean;
begin
  Result := RegKeyExists(HKLM64, 'SOFTWARE\ASIO\Waves SoundGrid') or
            RegKeyExists(HKLM32, 'SOFTWARE\ASIO\Waves SoundGrid');
end;

function IsObsRunning(): Boolean;
var
  Locator, Service, Procs: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Procs.Count > 0;
  except
    Result := False;
  end;
end;

function WaitForObsClosed(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
  begin
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := WaitForObsClosed();
  if Result and not ObsInstalled() then
    Result := SuppressibleMsgBox(CustomMessage('ObsMissing'), mbConfirmation, MB_YESNO, IDYES) = IDYES;
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForObsClosed();
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    if WavesDriverInstalled() then
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + CustomMessage('WavesFound')
    else
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + CustomMessage('WavesMissing');
  end;
end;
