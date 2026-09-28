!define APP_NAME        "CeroClient"
!define APP_VERSION     "3.2.17F"
!define APP_VERSION_NUM "3.2.17.0"  ; VIProductVersion n'accepte QUE du numérique
!define APP_PUBLISHER   "CeroClient"
!define APP_EXE         "ceroclient-bootstrapper.exe"
!define APP_ICON        "assets\favicon.ico"
!define INSTALL_DIR     "$LOCALAPPDATA\CeroClient"
!define CDN_URL         "https://github.com/CeroWorks/Cero-Client/releases/latest/download/CeroClient-bootstrapper-windows-x86_64.zip"
!define UNINST_KEY      "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

!include "MUI2.nsh"
!include "LogicLib.nsh"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "CeroClient-Setup-${APP_VERSION}.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma

VIProductVersion "${APP_VERSION_NUM}"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "ProductVersion"  "${APP_VERSION}"
VIAddVersionKey "CompanyName"     "${APP_PUBLISHER}"
VIAddVersionKey "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey "FileVersion"     "${APP_VERSION}"
VIAddVersionKey "LegalCopyright"  "© ${APP_PUBLISHER}"

!define MUI_ICON   "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Lancer ${APP_NAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "French"

Section "Install"
    SetOutPath "$INSTDIR"
    File "/oname=icon.ico" "${APP_ICON}"

    WriteUninstaller "$INSTDIR\uninstall.exe"   ; AVANT de créer le raccourci vers uninstall.exe

    DetailPrint "Téléchargement du bootstrapper..."
    nsExec::ExecToStack '"$SYSDIR\curl.exe" --ssl-no-revoke --silent --fail --show-error -L --retry 3 -o "$INSTDIR\bootstrapper.zip" "${CDN_URL}"'
    Pop $0  ; code de sortie
    Pop $1  ; sortie (stderr de curl avec --show-error)
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "Échec du téléchargement du bootstrapper (code $0) :$\n$1"
        Abort
    ${EndIf}

    DetailPrint "Extraction du bootstrapper..."

    nsExec::ExecToLog 'tar -xf "$INSTDIR\bootstrapper.zip" -C "$INSTDIR"'
    Pop $0
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "Échec de l'extraction du bootstrapper (code $0)."
        Abort
    ${EndIf}

    Delete "$INSTDIR\bootstrapper.zip"

    ${IfNot} ${FileExists} "$INSTDIR\${APP_EXE}"
        MessageBox MB_ICONSTOP "${APP_EXE} introuvable après extraction."
        Abort
    ${EndIf}

    CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\icon.ico"
    Pop $0
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\icon.ico"
    Pop $0
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\uninstaller.lnk" "$INSTDIR\uninstall.exe"
    Pop $0

    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\icon.ico"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\uninstaller.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    Delete "$INSTDIR\bootstrapper.zip"
    RMDir /r "$INSTDIR"

    ; RMDir /r "$APPDATA\.ceroclient"

    DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd