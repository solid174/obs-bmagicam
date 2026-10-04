; Windows installer for obs-bmagicam (docs/releasing.md). Built in CI by Package-Windows.ps1:
;   ISCC /DVersion=<version> /DSource=<release\Configuration> /DOutputDir=<release> /DOutputName=<name> installer.iss
; It installs the plugin where OBS Studio finds plugins for every user of the computer.

[Setup]
AppId={{6F3E8C52-1A9B-4D7E-9B0C-2E5F7A1D4C63}
AppName=obs-bmagicam
AppVersion={#Version}
AppVerName=obs-bmagicam {#Version}
AppPublisher=solid174
AppPublisherURL=https://github.com/solid174/obs-bmagicam
AppSupportURL=https://github.com/solid174/obs-bmagicam/blob/main/docs/setup.md
DefaultDirName={commonappdata}\obs-studio\plugins\obs-bmagicam
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=obs-bmagicam (OBS Studio plugin)
CloseApplications=no

[Messages]
WelcomeLabel2=This installs the iPhone Camera plugin for OBS Studio.%n%nClose OBS Studio before you continue.

[Files]
Source: "{#Source}\obs-bmagicam\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{app}"
