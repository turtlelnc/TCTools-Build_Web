# ============================================================================
#  TCTools-Build_Web (TC-BuildWeb)  --  Top-level build script
#  TCTools-Build_Web（TC-BuildWeb）—— 顶层构建脚本
#
#  Cross-platform C++20 / Qt 6 desktop application.
#  跨平台 C++20 / Qt 6 桌面应用程序。
# ============================================================================
cmake_minimum_required(VERSION 3.24)

# ============================================================================
#  Windows + GNU binutils + bilingual folder names
#  Windows + GNU binutils + 双语文件夹名
#
#  Every project directory is bilingual ("src_Source_源代码"), so every object
#  file path contains non-ASCII characters. The traditional GNU binutils
#  shipped alongside MinGW open input files through the ANSI code page instead
#  of the wide-character API, so they cannot read those paths at all:
#
#      ar.exe: .../Types.cpp.obj: No such file or directory
#      ld.exe: cannot find .../Types.cpp.obj: No such file or directory
#
#  `gcc`/`g++` themselves are unaffected (they use the wide API), which is why
#  every source file compiles and only archiving and linking fail.
#
#  The fix is to use LLVM's Unicode-aware replacements, which the same
#  MinGW-w64 14 distribution already ships: `llvm-ar` / `llvm-ranlib` for
#  archiving and `ld.lld` for linking. Nothing else about the toolchain
#  changes -- the compiler stays g++ 14.
#
#  本项目每个目录名都是双语的（"src_Source_源代码"），因此每个目标文件路径都含
#  非 ASCII 字符。随 MinGW 一同提供的传统 GNU binutils 通过 ANSI 代码页而非宽字符
#  API 打开输入文件，因此完全无法读取这些路径：
#
#      ar.exe: .../Types.cpp.obj: No such file or directory
#      ld.exe: cannot find .../Types.cpp.obj: No such file or directory
#
#  `gcc`/`g++` 本身不受影响（它们使用宽字符 API），这正是"每个源文件都能编译、
#  只有归档与链接失败"的原因。
#
#  解决办法是改用 LLVM 的 Unicode 感知替代品，而同一个 MinGW-w64 14 发行版已经
#  自带它们：归档用 `llvm-ar` / `llvm-ranlib`，链接用 `ld.lld`。工具链其余部分
#  完全不变 —— 编译器仍然是 g++ 14。
# ============================================================================
if(WIN32 AND NOT MSVC)
    string(REGEX MATCH "[^ -~]" _tcbw_needs_unicode_tools "${CMAKE_CURRENT_SOURCE_DIR}")

    if(_tcbw_needs_unicode_tools)
        find_program(TCBW_LLVM_AR NAMES llvm-ar llvm-ar.exe
                     HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../Mingw-w64/mingw64/bin")
        find_program(TCBW_LLVM_RANLIB NAMES llvm-ranlib llvm-ranlib.exe
                     HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../Mingw-w64/mingw64/bin")
        find_program(TCBW_LLD NAMES ld.lld ld.lld.exe lld lld.exe)

        if(NOT TCBW_LLVM_AR OR NOT TCBW_LLD)
            message(FATAL_ERROR
                "This source tree uses bilingual (non-ASCII) folder names.\n"
                "本源码树使用双语（非 ASCII）文件夹名。\n"
                "GNU binutils (ar/ld) cannot read such paths on Windows, and the "
                "Unicode-aware LLVM replacements were not found.\n"
                "GNU binutils（ar/ld）在 Windows 上无法读取此类路径，且未找到 Unicode "
                "感知的 LLVM 替代工具。\n"
                "Fix / 解决办法:\n"
                "  * install LLVM and add it to PATH, or / 安装 LLVM 并加入 PATH，或\n"
                "  * use an MSVC Visual Studio generator / 使用 MSVC 的 Visual Studio 生成器，或\n"
                "  * build from an ASCII-only copy of the tree / 从仅含 ASCII 的副本构建。")
        endif()

        set(CMAKE_AR     "${TCBW_LLVM_AR}"     CACHE FILEPATH "LLVM archiver for non-ASCII paths" FORCE)
        set(CMAKE_RANLIB "${TCBW_LLVM_RANLIB}" CACHE FILEPATH "LLVM ranlib for non-ASCII paths" FORCE)
        #  Only the archiver is swapped; the linker stays GNU ld. Setting
        #  TCBW_USE_LLD here would defeat the opt-in `option()` below, because a
        #  normal variable defined before option() wins over the cache entry.
        #  仅替换归档器；链接器仍为 GNU ld。在此设置 TCBW_USE_LLD 会破坏下方
        #  需显式开启的 `option()`，因为在 option() 之前定义的普通变量优先于缓存项。
        set(TCBW_NON_ASCII_PATHS TRUE)

        message(STATUS "Non-ASCII path detected: using llvm-ar for archiving "
                       "(GNU ar cannot read bilingual folder names)")
        message(STATUS "检测到非 ASCII 路径：归档改用 llvm-ar"
                       "（GNU ar 无法读取双语文件夹名）")
    endif()
endif()

project(TCTools-Build_Web
    VERSION 0.1.0
    DESCRIPTION "Visual web page builder with Scratch-style block programming and AI assistance"
    HOMEPAGE_URL "https://github.com/turtlelnc/TCTools-Build_Web"
    LANGUAGES CXX)

# --- Release channel --------------------------------------------------------
# --- 发布通道 ---------------------------------------------------------------
set(TCBW_VERSION_CHANNEL "rc1" CACHE STRING "Release channel / 发布通道")
set(TCBW_VERSION_FULL "${PROJECT_VERSION}-${TCBW_VERSION_CHANNEL}")
message(STATUS "TC-BuildWeb version: ${TCBW_VERSION_FULL}")

# --- Global language / platform settings ------------------------------------
# --- 全局语言 / 平台设置 ----------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_CMake_构建")

# ---------------------------------------------------------------------------
#  Guard: Ninja cannot build this tree on Windows
#  防护：在 Windows 上 Ninja 无法构建本源码树
#
#  The bilingual folder names make every object path non-ASCII. Ninja on Windows
#  writes build.ninja as UTF-8 but hands the command line to the compiler as
#  bytes, so the compiler is asked to create a mojibake path and fails with
#  "can't create ...obj: No such file or directory". Failing here, with the fix
#  in the message, is far kinder than 146 confusing compiler errors.
#  双语文件夹名使每个目标文件路径都含非 ASCII 字符。Windows 上的 Ninja 以 UTF-8
#  写出 build.ninja，却以字节形式把命令行交给编译器，于是编译器被要求创建乱码路径，
#  并以 "can't create ...obj: No such file or directory" 失败。在此处失败并在消息中
#  给出解决办法，远比对用户抛出 146 条令人困惑的编译错误要友善。
# ---------------------------------------------------------------------------
if(WIN32 AND CMAKE_GENERATOR MATCHES "Ninja")
    string(REGEX MATCH "[^ -~]" _tcbw_non_ascii "${CMAKE_CURRENT_SOURCE_DIR}")
    if(_tcbw_non_ascii)
        message(FATAL_ERROR
            "This source tree uses bilingual (non-ASCII) folder names, which Ninja "
            "cannot handle on Windows.\n"
            "本源码树使用双语（非 ASCII）文件夹名，Windows 上的 Ninja 无法处理。\n"
            "Use / 请使用:\n"
            "    cmake -S . -B build_构建 -G \"MinGW Makefiles\" -DCMAKE_PREFIX_PATH=<Qt>\n"
            "or an MSVC Visual Studio generator / 或使用 MSVC 的 Visual Studio 生成器。")
    endif()
endif()

# --- Options ----------------------------------------------------------------
# --- 构建选项 ---------------------------------------------------------------
option(TCBW_BUILD_TESTS      "Build unit tests / 构建单元测试"            ON)
option(TCBW_ENABLE_WEBENGINE "Use Qt WebEngine preview when available / 可用时启用 WebEngine 预览" ON)
option(TCBW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors / 将编译警告视为错误" OFF)

# --- Qt ---------------------------------------------------------------------
# --- Qt 依赖 ----------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 QuickLayouts Network Sql Concurrent Svg Test)

qt_standard_project_setup(REQUIRES 6.5)

# Optional Qt modules -- detected, never required.
# 可选 Qt 模块 —— 仅检测，绝不强制要求。
if(TCBW_ENABLE_WEBENGINE)
    find_package(Qt6 QUIET COMPONENTS WebEngineQuick WebEngineCore)
    if(Qt6WebEngineQuick_FOUND)
        set(TCBW_HAVE_WEBENGINE ON)
        message(STATUS "Qt WebEngine found -> embedded browser preview ENABLED")
    else()
        set(TCBW_HAVE_WEBENGINE OFF)
        message(STATUS "Qt WebEngine not found -> using QML canvas + external browser preview")
    endif()
endif()

# ============================================================================
#  Sources -- bilingual directory names (English first, then Chinese)
#  源码 —— 中英双语目录命名（先英语，后中文）
# ============================================================================
set(TCBW_CORE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src_Source_源代码/core_Core_核心")
set(TCBW_CODEGEN_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src_Source_源代码/codegen_CodeGen_代码生成")
set(TCBW_BLOCKS_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/src_Source_源代码/blocks_Blocks_积木")
set(TCBW_AI_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/src_Source_源代码/ai_AI_智能")
set(TCBW_BACKEND_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src_Source_源代码/backend_Backend_后端")
set(TCBW_APP_DIR      "${CMAKE_CURRENT_SOURCE_DIR}/src_Source_源代码/app_应用")

# --- tcbw_core : all non-UI logic, linkable by tests ------------------------
# --- tcbw_core : 全部非界面逻辑，可被测试链接 -------------------------------
set(TCBW_CORE_SOURCES
    "${TCBW_CORE_DIR}/Types.cpp"
    "${TCBW_CORE_DIR}/IdGenerator.cpp"
    "${TCBW_CORE_DIR}/StyleSheet.cpp"
    "${TCBW_CORE_DIR}/DocumentNode.cpp"
    "${TCBW_CORE_DIR}/Page.cpp"
    "${TCBW_CORE_DIR}/ProjectDocument.cpp"
    "${TCBW_CORE_DIR}/ElementCatalog.cpp"
    "${TCBW_CORE_DIR}/ProjectIO.cpp"
    "${TCBW_CORE_DIR}/AppSettings.cpp"
    "${TCBW_CORE_DIR}/Commands.cpp"

    "${TCBW_CODEGEN_DIR}/EmitOptions.cpp"
    "${TCBW_CODEGEN_DIR}/HtmlEmitter.cpp"
    "${TCBW_CODEGEN_DIR}/CssEmitter.cpp"
    "${TCBW_CODEGEN_DIR}/JsEmitter.cpp"
    "${TCBW_CODEGEN_DIR}/CodeGenerator.cpp"

    "${TCBW_BLOCKS_DIR}/BlockDef.cpp"
    "${TCBW_BLOCKS_DIR}/BlockRegistry.cpp"
    "${TCBW_BLOCKS_DIR}/BlockModel.cpp"
    "${TCBW_BLOCKS_DIR}/BlockCompiler.cpp"

    "${TCBW_AI_DIR}/AiTypes.cpp"
    "${TCBW_AI_DIR}/ProviderRegistry.cpp"
    "${TCBW_AI_DIR}/SecretStore.cpp"
    "${TCBW_AI_DIR}/AuthManager.cpp"
    "${TCBW_AI_DIR}/AiProvider.cpp"
    "${TCBW_AI_DIR}/McpClient.cpp"
    "${TCBW_AI_DIR}/AgentTools.cpp"
    "${TCBW_AI_DIR}/AgentRuntime.cpp"

    "${TCBW_BACKEND_DIR}/Endpoint.cpp"
    "${TCBW_BACKEND_DIR}/BackendConnector.cpp"
    "${TCBW_BACKEND_DIR}/BackendScaffold.cpp"
    "${TCBW_BACKEND_DIR}/PreviewServer.cpp"
)

add_library(tcbw_core STATIC ${TCBW_CORE_SOURCES})
add_library(TCTools::BuildWeb::Core ALIAS tcbw_core)

target_include_directories(tcbw_core PUBLIC
    "${TCBW_CORE_DIR}"
    "${TCBW_CODEGEN_DIR}"
    "${TCBW_BLOCKS_DIR}"
    "${TCBW_AI_DIR}"
    "${TCBW_BACKEND_DIR}"
)

target_compile_definitions(tcbw_core PUBLIC
    TCBW_VERSION="${PROJECT_VERSION}"
    TCBW_VERSION_FULL="${TCBW_VERSION_FULL}"
    TCBW_VERSION_CHANNEL="${TCBW_VERSION_CHANNEL}"
)

if(TCBW_HAVE_WEBENGINE)
    target_compile_definitions(tcbw_core PUBLIC TCBW_HAVE_WEBENGINE=1)
endif()

target_link_libraries(tcbw_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Concurrent Qt6::Svg)

#  Windows platform libraries.
#  1. `crypt32` provides DPAPI (CryptProtectData / CryptUnprotectData), which
#     SecretStore uses to bind the credential vault to the current user account.
#  2. `advapi32` and `user32` are pulled in by Qt, but are named explicitly so a
#     future contributor removing Qt's transitive hints cannot break the build.
#  Windows 平台库。
#  1. `crypt32` 提供 DPAPI（CryptProtectData / CryptUnprotectData），SecretStore
#     用它把凭据库绑定到当前用户账户。
#  2. `advapi32` 与 `user32` 本由 Qt 引入，此处显式声明，以免将来有人移除 Qt 的
#     传递性提示而破坏构建。
if(WIN32)
    target_link_libraries(tcbw_core PUBLIC crypt32 advapi32 user32)
endif()

if(MSVC)
    target_compile_options(tcbw_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tcbw_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(TCBW_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(tcbw_core PRIVATE /WX)
    else()
        target_compile_options(tcbw_core PRIVATE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
#  Linker
#  链接器
#
#  `llvm-ar` alone is enough to fix the observed Windows failure: GNU binutils'
#  `ar` opens input files through the ANSI code page and cannot read the
#  non-ASCII object paths at all, whereas GNU `ld` (reached through the g++
#  driver) does handle them. Routing every link through `ld.lld` was tried and
#  rejected: the MinGW flavour of lld fails to resolve `__argc`, which Qt's
#  entry-point library references, so GNU ld stays the linker of record. The
#  option below exists only so the alternative can be tested deliberately.
#
#  仅 `llvm-ar` 就足以修复在 Windows 上观察到的问题：GNU binutils 的 `ar` 通过
#  ANSI 代码页打开输入文件，完全无法读取非 ASCII 目标文件路径，而 GNU `ld`
#  （经 g++ 驱动程序调用）可以处理它们。曾尝试把所有链接改由 `ld.lld` 完成，但已
#  否决：lld 的 MinGW 变体无法解析 Qt 入口点库所引用的 `__argc`，因此仍以 GNU ld
#  为正式链接器。下面的选项仅供有意测试该替代方案时使用。
# ---------------------------------------------------------------------------
option(TCBW_USE_LLD "Link with ld.lld instead of GNU ld / 使用 ld.lld 而非 GNU ld 链接" OFF)
if(TCBW_USE_LLD)
    add_link_options(-fuse-ld=lld)
    message(STATUS "Linker: ld.lld (opt-in) / 链接器：ld.lld（需显式开启）")
endif()

# ============================================================================
#  Application target
#  应用程序目标
# ============================================================================
qt_add_executable(TCTools-Build_Web
    "${TCBW_APP_DIR}/main.cpp"
    "${TCBW_APP_DIR}/AppController.cpp"
    "${TCBW_APP_DIR}/AppController.h"
    "${TCBW_APP_DIR}/BuilderController.cpp"
    "${TCBW_APP_DIR}/BuilderController.h"
    "${TCBW_APP_DIR}/BlocksController.cpp"
    "${TCBW_APP_DIR}/BlocksController.h"
    "${TCBW_APP_DIR}/AiController.cpp"
    "${TCBW_APP_DIR}/AiController.h"
    "${TCBW_APP_DIR}/BackendController.cpp"
    "${TCBW_APP_DIR}/BackendController.h"
)

#  Windows subsystem / entry point.
#  Windows 子系统 / 入口点。
#
#  Qt's `Qt6EntryPoint` library exists to turn `main()` into a GUI-subsystem
#  entry point. Its MinGW build references `__imp___argc`, a symbol that MinGW's
#  UCRT import library does not export (UCRT only provides `__p___argc`), so
#  linking fails with
#      undefined reference to `__imp___argc'
#  On MinGW the entry point is therefore supplied by main.cpp itself and the GUI
#  subsystem is selected with `-mwindows`, which avoids Qt6EntryPoint entirely
#  while still producing a windowed application with no console.
#
#  Qt 的 `Qt6EntryPoint` 库用于把 `main()` 转换为图形子系统的入口点。其 MinGW 版本
#  引用了 `__imp___argc`，而 MinGW 的 UCRT 导入库并不导出该符号（UCRT 只提供
#  `__p___argc`），因此链接会失败并报
#      undefined reference to `__imp___argc'
#  因此在 MinGW 上改由 main.cpp 自己提供入口点，并用 `-mwindows` 选择图形子系统。
#  这既完全避开了 Qt6EntryPoint，又仍然生成没有控制台窗口的窗口应用。
set_target_properties(TCTools-Build_Web PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<BOOL:${TCBW_MINGW_OWN_ENTRYPOINT}>>
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "TCTools-Build_Web"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.turtlelnc.tctools.buildweb"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    OUTPUT_NAME "TCTools-Build_Web"
)

if(WIN32 AND NOT MSVC)
    #  See the note above / 见上方说明
    set(TCBW_MINGW_OWN_ENTRYPOINT TRUE)
    set_target_properties(TCTools-Build_Web PROPERTIES WIN32_EXECUTABLE FALSE)
    target_compile_definitions(TCTools-Build_Web PRIVATE TCBW_OWN_WINMAIN=1)
    target_link_options(TCTools-Build_Web PRIVATE -mwindows)
endif()

# --- QML module -------------------------------------------------------------
# --- QML 模块 ---------------------------------------------------------------
set(TCBW_QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources_Resources_资源/qml_QML_界面")
set(TCBW_QML_URI "TCTools.BuildWeb")

file(GLOB_RECURSE TCBW_QML_FILES CONFIGURE_DEPENDS
    "${TCBW_QML_DIR}/*.qml"
    "${TCBW_QML_DIR}/*.js")

#  GLOB_RECURSE yields absolute paths. qt_add_qml_module refuses absolute paths
#  inside a Qt resource unless each source carries a relative QT_RESOURCE_ALIAS,
#  so every QML/JS file is aliased to its bare file name. That also flattens the
#  module: `Main.qml` resolves as `qrc:/qt/qml/TCTools/BuildWeb/Main.qml`, which
#  is what QQmlApplicationEngine::loadFromModule() expects.
#  GLOB_RECURSE 产生的是绝对路径。除非每个源文件都带有相对的 QT_RESOURCE_ALIAS，
#  否则 qt_add_qml_module 会拒绝把绝对路径放进 Qt 资源，因此把每个 QML/JS 文件
#  都别名化为其裸文件名。这也把模块拍平：`Main.qml` 解析为
#  `qrc:/qt/qml/TCTools/BuildWeb/Main.qml`，正是
#  QQmlApplicationEngine::loadFromModule() 所期望的路径。
foreach(_qml_file IN LISTS TCBW_QML_FILES)
    get_filename_component(_qml_name "${_qml_file}" NAME)
    set_source_files_properties("${_qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${_qml_name}")
endforeach()

# Files that are QML singletons / QML 单例文件
set(TCBW_QML_SINGLETONS
    "${TCBW_QML_DIR}/Theme.qml"
    "${TCBW_QML_DIR}/Tr.qml"
)
foreach(_singleton IN LISTS TCBW_QML_SINGLETONS)
    if(EXISTS "${_singleton}")
        set_source_files_properties("${_singleton}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

#  The bundled .qrc has the same absolute-path constraint as the QML sources.
#  bundled .qrc 与 QML 源文件有相同的绝对路径限制。
set(TCBW_APP_QRC "${CMAKE_CURRENT_SOURCE_DIR}/resources_Resources_资源/icons_Icons_图标/app.qrc")
set_source_files_properties("${TCBW_APP_QRC}" PROPERTIES QT_RESOURCE_ALIAS "app.qrc")

qt_add_qml_module(TCTools-Build_Web
    URI "${TCBW_QML_URI}"
    VERSION 1.0
    QML_FILES ${TCBW_QML_FILES}
    RESOURCES
        "${TCBW_APP_QRC}"
)

target_include_directories(TCTools-Build_Web PRIVATE "${TCBW_APP_DIR}")
target_link_libraries(TCTools-Build_Web PRIVATE
    tcbw_core Qt6::Quick Qt6::QuickControls2 Qt6::QuickLayouts)

if(TCBW_HAVE_WEBENGINE)
    target_link_libraries(TCTools-Build_Web PRIVATE Qt6::WebEngineQuick)
endif()

# ============================================================================
#  Tests
#  测试
# ============================================================================
if(TCBW_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tests_Tests_测试" tests_Tests_测试)
endif()

# ============================================================================
#  Install & deployment
#  安装与部署
# ============================================================================
include(GNUInstallDirs)
install(TARGETS TCTools-Build_Web
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

qt_generate_deploy_qml_app_script(
    TARGET TCTools-Build_Web
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

message(STATUS "TC-BuildWeb ${TCBW_VERSION_FULL} configured | tests=${TCBW_BUILD_TESTS} | webengine=${TCBW_HAVE_WEBENGINE}")
