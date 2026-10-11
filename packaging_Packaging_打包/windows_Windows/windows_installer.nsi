; ============================================================================
;  windows_installer.nsi -- NSIS 3.x installer for TCTools-Build_Web
;  windows_installer.nsi —— TCTools-Build_Web 的 NSIS 3.x 安装包脚本
;
;  Product  TCTools-Build_Web (TC-BuildWeb)
;  Version  0.1.0-rc1
;  Publisher turtlelnc
;
;  Default is a PER-USER install (no elevation). Passing `/ALLUSERS` (or the
;  installer's optional /S silent mode combined with /ALLUSERS) performs an
;  ALL-USERS install and requests elevation.
;  默认按“当前用户”安装（无需提权）。传入 `/ALLUSERS` （或静默 `/S /ALLUSERS`）
;  则执行“所有用户”安装并请求提权。
;
;  Build 构建:
;    makensis /V2 /DTCBW_VERSION=0.1.0-rc1 ^
;             /DTCBW_STAGE=C:\path\to\dist\stage ^
;             /DTCBW_OUTFILE=C:\path\to\out.exe ^
;             /DTCBW_ARCH=x64 windows_installer.nsi
; ============================================================================

Unicode true
ManifestDPIAware true

; NSIS 3.x is the minimum supported build tool.
; NSIS 3.x 为最低支持的构建工具版本。
!ifndef NSIS_VERSION
  !error "NSIS_VERSION is undefined -- NSIS 3.x is required."
!endif

; ----------------------------------------------------------------------------
;  Build-time parameters (override with /D<NAME>=<VALUE> on the makensis command)
;  构建期参数（可在 makensis 命令行用 /D<NAME>=<VALUE> 覆盖）
; ----------------------------------------------------------------------------
!ifndef TCBW_VERSION
  !define TCBW_VERSION "0.1.0-rc1"
!endif
!ifndef TCBW_STAGE
  ; Default assumes the CI staging layout <repo>/dist/stage, resolved relative to
  ; THIS script so the result no longer depends on the caller's working dir.
  ; 默认假定 CI 的分阶段目录 <repo>/dist/stage，并相对于本脚本解析，
  ; 因此结果不再依赖调用者的工作目录。
  !define TCBW_STAGE "!__FILEDIR__\..\..\..\dist\stage"
!endif
!ifndef TCBW_OUTFILE
  !define TCBW_OUTFILE "TCBuildWeb-${TCBW_VERSION}-windows-setup.exe"
!endif
!ifndef TCBW_ARCH
  !define TCBW_ARCH "x64"
!endif
!ifndef TCBW_LICENSE
  ; Resolved relative to this script, for the same reason as TCBW_STAGE.
  ; 与 TCBW_STAGE 同理，相对于本脚本解析。
  !define TCBW_LICENSE "!__FILEDIR__\..\..\..\LICENSE"
!endif

; ----------------------------------------------------------------------------
;  Numeric version -- VIProductVersion REQUIRES exactly four integers, so the
;  textual "0.1.0-rc1" cannot be used there. It tracks the CMake project version
;  (0.1.0) with the revision component zeroed.
;  数值版本 —— VIProductVersion 必须恰好是四个整数，因此文本形式的
;  “0.1.0-rc1” 无法用于此处。它跟随 CMake 工程版本（0.1.0），修订位补零。
; ----------------------------------------------------------------------------
!define TCBW_VERSION_NUM "0.1.0.0"

; ----------------------------------------------------------------------------
;  Product identity 产品标识
; ----------------------------------------------------------------------------
!define PRODUCT_NAME        "TCTools-Build_Web"
!define PRODUCT_SHORT_NAME  "TC-BuildWeb"
!define PRODUCT_PUBLISHER   "turtlelnc"
!define PRODUCT_WEB_SITE    "https://github.com/turtlelnc/TCTools-Build_Web"
!define PRODUCT_EXE         "TCTools-Build_Web.exe"
!define PRODUCT_VERSION     "${TCBW_VERSION}"
!define PRODUCT_REGKEY      "Software\${PRODUCT_PUBLISHER}\${PRODUCT_NAME}"
!define PRODUCT_UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_EXT         ".tcbw"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION} (${TCBW_ARCH})"
BrandingText "${PRODUCT_NAME} ${PRODUCT_VERSION} - ${PRODUCT_PUBLISHER}"
OutFile "${TCBW_OUTFILE}"
Caption "${PRODUCT_NAME} ${PRODUCT_VERSION} Setup"

; ----------------------------------------------------------------------------
;  Install scope -- per-user by default, per-machine with /ALLUSERS
;  安装范围 —— 默认当前用户，传入 /ALLUSERS 则为所有用户
; ----------------------------------------------------------------------------
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "MultiUser.nsh"

; Require Windows 10 1709+ (build 16299) -- our documented minimum.
; 要求 Windows 10 1709+（内部版本 16299）—— 我们声明的最低版本。
!define TCBW_MIN_BUILD 16299

; Per-user installs never prompt for elevation; /ALLUSERS does.
; 当前用户安装不请求提权；/ALLUSERS 会请求提权。
RequestExecutionLevel user

VIProductVersion "${TCBW_VERSION_NUM}"
VIAddVersionKey /LANG=1033 "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "CompanyName"     "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey /LANG=1033 "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion"  "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright"  "Copyright (c) ${PRODUCT_PUBLISHER}"

; ----------------------------------------------------------------------------
;  Pages 向导页面
; ----------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON   "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${TCBW_LICENSE}"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU "Application" $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

; ----------------------------------------------------------------------------
;  Init 初始化
; ----------------------------------------------------------------------------
Function .onInit
  ; MultiUser decides between per-user and per-machine at runtime.
  ; MultiUser 在运行时决定“当前用户”或“所有用户”。
  !insertmacro MULTIUSER_INIT

  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP|MB_OK \
      "TCTools-Build_Web requires Windows 10 or newer.$\r$\nTCTools-Build_Web 需要 Windows 10 或更高版本。"
    Abort
  ${EndIf}

  ; ${AtLeastWin10} is true for Windows 10; the 1709 build gate is checked via
  ; the registry because NSIS has no ${AtMostWin}${AtLeastWin10_1709} macro.
  ; ${AtLeastWin10} 只判定“Windows 10”；1709 版本门槛通过注册表检查，
  ; 因为 NSIS 没有内建的 1709 判定宏。
  ReadRegDWord $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentBuildNumber"
  ${If} $0 < ${TCBW_MIN_BUILD}
    MessageBox MB_ICONSTOP|MB_OK \
      "Windows 10 version 1709 (build ${TCBW_MIN_BUILD}) or newer is required.$\r$\nDetected build: $0$\r$\n需要 Windows 10 1709（内部版本 ${TCBW_MIN_BUILD}）或更高版本。检测到：$0"
    Abort
  ${EndIf}
FunctionEnd

; ----------------------------------------------------------------------------
;  Install 安装
; ----------------------------------------------------------------------------
Section "!${PRODUCT_NAME} (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetOverwrite on

  ; Copy the fully deployed staging tree (exe + Qt/QML runtime + MinGW DLLs).
  ; 复制已完整部署的分阶段目录（可执行文件 + Qt/QML 运行时 + MinGW DLL）。
  ; `deploy_windows.ps1` must have run against ${TCBW_STAGE} beforehand; running
  ; windeployqt afterwards would leave the installer without the Qt runtime.
  ; 必须已对 ${TCBW_STAGE} 执行过 `deploy_windows.ps1`；否则安装包会缺失 Qt 运行时。
  !if /FILEEXISTS "${TCBW_STAGE}\${PRODUCT_EXE}"
    File /r "${TCBW_STAGE}\*.*"
  !else
    ; Fail loudly at compile time rather than shipping an empty installer.
    ; 在编译期直接失败，而不是产出一个空安装包。
    !error "Staging tree not found: '${TCBW_STAGE}'. Run deploy_windows.ps1 first, or pass /DTCBW_STAGE=<path>."
  !endif
SectionEnd

Section "Start Menu shortcuts 开始菜单快捷方式" SecStartMenu
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME}.lnk" \
      "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0 SW_SHOWNORMAL "" \
      "${PRODUCT_NAME} ${PRODUCT_VERSION}"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall ${PRODUCT_NAME}.lnk" \
      "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0 SW_SHOWNORMAL "" \
      "Uninstall ${PRODUCT_NAME} / 卸载 ${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME} Website.lnk" \
      "${PRODUCT_WEB_SITE}"
  !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section "Desktop shortcut 桌面快捷方式" SecDesktop
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" \
    "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0 SW_SHOWNORMAL "" \
    "${PRODUCT_NAME} ${PRODUCT_VERSION}"
SectionEnd

Section "File association (.tcbw project) 文件关联" SecFileAssoc
  ; Per-user associations go to HKCU; per-machine ones to HKLM.
  ; 当前用户关联写入 HKCU；所有用户关联写入 HKLM。
  WriteRegStr SHCTX "Software\Classes\${PRODUCT_EXT}" "" "${PRODUCT_SHORT_NAME} Project"
  WriteRegStr SHCTX "Software\Classes\${PRODUCT_EXT}" "FriendlyTypeName" "${PRODUCT_NAME} Project"
  WriteRegStr SHCTX "Software\Classes\${PRODUCT_EXT}\DefaultIcon" "" "$INSTDIR\${PRODUCT_EXE},0"
  WriteRegStr SHCTX "Software\Classes\${PRODUCT_EXT}\shell\open\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'
  WriteRegStr SHCTX "Software\Classes\Applications\${PRODUCT_EXE}\shell\open\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'

  ; Notify Explorer that the association table changed.
  ; 通知资源管理器文件关联已变更。
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section -Uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; --- Add/Remove Programs entry -------------------------------------------
  ; --- “应用和功能”条目 -----------------------------------------------------
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME} ${PRODUCT_VERSION}"
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${PRODUCT_EXE},0"
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_WEB_SITE}"
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD SHCTX "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD SHCTX "${PRODUCT_UNINST_KEY}" "NoRepair" 1

  ; Estimated size, computed from the installed tree.
  ; 估算占用空间，由已安装目录计算得出。
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD SHCTX "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"

  WriteRegStr SHCTX "${PRODUCT_REGKEY}" "" "${PRODUCT_VERSION}"
  WriteRegStr SHCTX "${PRODUCT_REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr SHCTX "${PRODUCT_REGKEY}" "Architecture" "${TCBW_ARCH}"
SectionEnd

; ----------------------------------------------------------------------------
;  Section descriptions 组件说明
; ----------------------------------------------------------------------------
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} \
    "Core application files and the Qt runtime. / 核心程序文件与 Qt 运行时。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} \
    "Create Start Menu shortcuts. / 创建开始菜单快捷方式。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} \
    "Create a Desktop shortcut. / 创建桌面快捷方式。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFileAssoc} \
    "Associate .tcbw project files with ${PRODUCT_SHORT_NAME}. / 将 .tcbw 工程文件关联到 ${PRODUCT_SHORT_NAME}。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ----------------------------------------------------------------------------
;  Uninstall 卸载
; ----------------------------------------------------------------------------
Function un.onInit
  !insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "Uninstall"
  ; Remove shortcuts first so Explorer never holds a lock on the install dir.
  ; 先删除快捷方式，避免资源管理器占用安装目录。
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  Delete "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall ${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME} Website.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  ; Remove file associations.
  ; 删除文件关联。
  DeleteRegKey SHCTX "Software\Classes\Applications\${PRODUCT_EXE}"
  DeleteRegKey SHCTX "Software\Classes\${PRODUCT_EXT}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'

  ; Remove the installed tree.
  ; 删除已安装目录树。
  RMDir /r "$INSTDIR\qml"
  RMDir /r "$INSTDIR\plugins"
  RMDir /r "$INSTDIR\translations"
  RMDir /r "$INSTDIR\resources"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.exe"
  Delete "$INSTDIR\*.qm"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  ; Remove registry entries.
  ; 删除注册表项。
  DeleteRegKey SHCTX "${PRODUCT_UNINST_KEY}"
  DeleteRegKey SHCTX "${PRODUCT_REGKEY}"
  DeleteRegKey SHCTX "Software\${PRODUCT_PUBLISHER}"

  SetAutoClose true
SectionEnd
