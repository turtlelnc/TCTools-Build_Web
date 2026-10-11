// ============================================================================
//  main.cpp -- application entry point
//  main.cpp —— 应用程序入口
//
//  TCTools-Build_Web (TC-BuildWeb)
//  A visual web page builder with Scratch-style block programming and the
//  TI-UseAgent AI assistant, for people who have never written code.
//  一款面向从未写过代码的用户的可视化网页构建工具，具备类 Scratch 积木编程
//  与 TI-UseAgent 人工智能助手。
//
//  Generated with DeepSeek V4.1 Flash on DeepSeek Harness.
//  本项目由 DeepSeek V4.1 Flash 在 DeepSeek Harness 上生成。
// ============================================================================
#include "AppController.h"
#include "AiController.h"
#include "AppSettings.h"
#include "BackendController.h"
#include "BlocksController.h"
#include "BuilderController.h"
#include "SecretStore.h"
#include "Types.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QtQml>

#include <cstdio>

// ============================================================================
//  Windows GUI entry point
//  Windows 图形入口点
//
//  Qt's `Qt6EntryPoint` library normally supplies this, but its MinGW build
//  references `__imp___argc`, which MinGW's UCRT import library does not export
//  (UCRT offers only `__p___argc`), so linking fails. The entry point is
//  therefore defined here (see the CMake note that sets WIN32_EXECUTABLE FALSE
//  and passes -mwindows).
//
//  A GUI-subsystem process has no console, which would silently swallow the
//  output of `--doctor` and `--headless --export`. WinMain therefore attaches to
//  the launching console when there is one and reopens the standard streams, so
//  the command-line interface keeps working when run from a terminal while a
//  normal launch still shows no console window.
//
//  Qt 的 `Qt6EntryPoint` 库通常提供该入口点，但其 MinGW 版本引用了
//  `__imp___argc`，而 MinGW 的 UCRT 导入库并不导出该符号（UCRT 只提供
//  `__p___argc`），因此链接失败。故在此定义入口点（参见 CMake 中设置
//  WIN32_EXECUTABLE FALSE 并传入 -mwindows 的说明）。
//
//  图形子系统进程没有控制台，这会静默吞掉 `--doctor` 与 `--headless --export`
//  的输出。因此 WinMain 在存在启动控制台时附加到该控制台并重新打开标准流，
//  使命令行接口在终端中运行时仍然可用，而正常启动时依旧不显示控制台窗口。
// ============================================================================
#if defined(Q_OS_WIN) && defined(TCBW_OWN_WINMAIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>

#  include <QByteArray>
#  include <QString>
#  include <QVector>

extern int main(int argc, char *argv[]);

namespace {

//  Attaches to the console of the process that launched us, if any, and points
//  the C runtime streams at it. Returns false when there is no parent console,
//  which is the normal double-click launch.
//  附加到启动本进程的那个进程的控制台（若有），并把 C 运行时标准流指向它。
//  没有父控制台时（即常见的双击启动）返回 false。
bool attachParentConsole()
{
    if (!::AttachConsole(ATTACH_PARENT_PROCESS))
        return false;

    FILE *stream = nullptr;
    //  freopen_s is the MSVC/UCRT secure variant; MinGW provides it too.
    //  freopen_s 是 MSVC/UCRT 的安全变体，MinGW 同样提供。
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
    return true;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    attachParentConsole();

    //  Rebuild the argument vector from the wide command line so non-ASCII paths
    //  survive. Qt itself re-reads the command line on Windows, but `main` and
    //  QCommandLineParser rely on this vector.
    //  依据宽字符命令行重建参数向量，使非 ASCII 路径得以保留。Qt 在 Windows 上会
    //  自行重读命令行，但 `main` 与 QCommandLineParser 依赖此向量。
    int argumentCount = 0;
    LPWSTR *wideArguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (!wideArguments)
        return 1;

    QVector<QByteArray> storage;
    storage.reserve(argumentCount);
    for (int i = 0; i < argumentCount; ++i)
        storage.append(QString::fromWCharArray(wideArguments[i]).toUtf8());
    ::LocalFree(wideArguments);

    QVector<char *> arguments;
    arguments.reserve(argumentCount + 1);
    for (int i = 0; i < argumentCount; ++i)
        arguments.append(storage[i].data());
    arguments.append(nullptr);

    return main(argumentCount, arguments.data());
}
#endif

namespace {

//  Prints a line to stdout in a way that works for both GUI and console
//  subsystems on Windows.
//  以在 Windows 的图形与命令行子系统下都可行的方式向标准输出打印一行。
void consoleLine(const QString &text)
{
    const QByteArray utf8 = text.toUtf8() + '\n';
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fflush(stdout);
}

void configureSurface()
{
    //  Prefer a smooth, Apple-like presentation: 8-bit colour, no depth buffer,
    //  and no multisampling on low-power devices.
    //  优先获得平滑、类 Apple 的呈现效果：8 位色深、无深度缓冲，并在低功耗
    //  设备上关闭多重采样。
    QSurfaceFormat format;
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setSamples(4);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("turtlelnc"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("turtlelnc.github.io"));
    QCoreApplication::setApplicationName(QStringLiteral("TCTools-Build_Web"));
    QCoreApplication::setApplicationVersion(tcbw::versionString());

    //  Use the Basic style as the base layer: the product ships its own
    //  Apple-inspired design system on top of it, and the platform styles
    //  differ too much across Windows, macOS and Linux to theme reliably.
    //  以 Basic 样式作为基础层：产品在其上自带一套 Apple 风格设计系统，而各平台
    //  原生样式差异过大，无法可靠地统一主题。
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    configureSurface();

    // --- command line ------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("TCTools-Build_Web -- build web pages visually.\n"
                       "TCTools-Build_Web —— 用可视化方式构建网页。"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption openOption(
        {QStringLiteral("o"), QStringLiteral("open")},
        QStringLiteral("Open a .tcbw project file on start-up. / 启动时打开 .tcbw 工程文件。"),
        QStringLiteral("file"));
    const QCommandLineOption exportOption(
        {QStringLiteral("e"), QStringLiteral("export")},
        QStringLiteral("Export the project to a directory and exit. / 将工程导出到目录后退出。"),
        QStringLiteral("directory"));
    const QCommandLineOption headlessOption(
        QStringLiteral("headless"),
        QStringLiteral("Run without showing a window (implies --export). / "
                       "不显示窗口运行（隐含 --export）。"));
    const QCommandLineOption doctorOption(
        QStringLiteral("doctor"),
        QStringLiteral("Print environment diagnostics and exit. / 打印环境诊断信息后退出。"));
    parser.addOption(openOption);
    parser.addOption(exportOption);
    parser.addOption(headlessOption);
    parser.addOption(doctorOption);
    parser.process(app);

    if (parser.isSet(doctorOption)) {
        consoleLine(QStringLiteral("TCTools-Build_Web %1 (%2)")
                        .arg(tcbw::versionString(), tcbw::versionChannel()));
        consoleLine(QStringLiteral("Qt runtime      : %1").arg(QString::fromLatin1(qVersion())));
        consoleLine(QStringLiteral("Qt build        : %1").arg(QString::fromLatin1(QT_VERSION_STR)));
        consoleLine(QStringLiteral("Platform        : %1 / %2")
                        .arg(tcbw::versionChannel().isEmpty()
                                 ? QGuiApplication::platformName()
                                 : QGuiApplication::platformName(),
                             QSysInfo::prettyProductName()));
        consoleLine(QStringLiteral("Architecture    : %1").arg(QSysInfo::currentCpuArchitecture()));
        consoleLine(QStringLiteral("Kernel          : %1 %2")
                        .arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()));
        consoleLine(QStringLiteral("Locale          : %1").arg(QLocale::system().name()));
        consoleLine(QStringLiteral("Settings file   : %1")
                        .arg(tcbw::AppSettings::instance().settingsFilePath()));
        consoleLine(QStringLiteral("Secret backend  : %1 (%2)")
                        .arg(tcbw::SecretStore::instance().backendName(),
                             tcbw::SecretStore::instance().isHardwareOrOsBacked()
                                 ? QStringLiteral("OS-backed")
                                 : QStringLiteral("obfuscated")));
        consoleLine(QStringLiteral("Vendors         : %1")
                        .arg(tcbw::ProviderRegistry::instance().vendorCount()));
        consoleLine(QStringLiteral("Models          : %1")
                        .arg(tcbw::ProviderRegistry::instance().modelCount()));
        consoleLine(QStringLiteral("HTML elements   : %1")
                        .arg(tcbw::ElementCatalog::instance().elements().size()));
        consoleLine(QStringLiteral("Block types     : %1")
                        .arg(tcbw::BlockRegistry::instance().definitions().size()));
        consoleLine(QStringLiteral("WebEngine       : %1")
                        .arg(
#ifdef TCBW_HAVE_WEBENGINE
                            QStringLiteral("available")
#else
                            QStringLiteral("not available (external browser preview)")
#endif
                        ));
        return 0;
    }

    // --- controllers -------------------------------------------------------
    //  The controllers are created here and registered as QML singletons so
    //  that every QML file can reach them by name without context lookups.
    //  控制器在此创建并注册为 QML 单例，使每个 QML 文件都能按名称访问它们，
    //  无需上下文查找。
    auto *appController = new tcbw::AppController(&app);
    auto *builderController = new tcbw::BuilderController(appController, &app);
    auto *blocksController = new tcbw::BlocksController(appController, &app);
    auto *aiController = new tcbw::AiController(appController, &app);
    auto *backendController = new tcbw::BackendController(appController, &app);

    aiController->setBuilderController(builderController);

    const char *uri = "TCTools.BuildWeb";
    qmlRegisterSingletonInstance(uri, 1, 0, "App", appController);
    qmlRegisterSingletonInstance(uri, 1, 0, "Builder", builderController);
    qmlRegisterSingletonInstance(uri, 1, 0, "Blocks", blocksController);
    qmlRegisterSingletonInstance(uri, 1, 0, "Agent", aiController);
    qmlRegisterSingletonInstance(uri, 1, 0, "Backend", backendController);

    // --- QML ---------------------------------------------------------------
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tcApplication"), &app);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() {
            consoleLine(QStringLiteral(
                "Fatal: the user interface failed to load. / 致命错误：用户界面加载失败。"));
            QCoreApplication::exit(2);
        },
        Qt::QueuedConnection);

    engine.loadFromModule(uri, QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
        return 2;

    // --- start-up actions --------------------------------------------------
    if (parser.isSet(openOption)) {
        const QString path = parser.value(openOption);
        if (!appController->openProject(path)) {
            consoleLine(QStringLiteral("Warning: could not open %1 / 警告：无法打开 %1")
                            .arg(path));
        }
    }

    if (parser.isSet(exportOption) || parser.isSet(headlessOption)) {
        const QString directory = parser.value(exportOption);
        if (directory.isEmpty()) {
            consoleLine(QStringLiteral(
                "Error: --headless requires --export <directory>. / "
                "错误：--headless 需要同时指定 --export <目录>。"));
            return 3;
        }
        const QString result = appController->exportToDirectory(directory);
        if (result.isEmpty()) {
            consoleLine(QStringLiteral("Error: export failed. / 错误：导出失败。"));
            return 4;
        }
        consoleLine(result);
        if (parser.isSet(headlessOption))
            return 0;
    }

    return app.exec();
}
