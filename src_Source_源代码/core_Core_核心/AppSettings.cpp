// ============================================================================
//  AppSettings.cpp -- user preferences (never credentials)
//  AppSettings.cpp —— 用户偏好设置（绝不保存凭据）
// ============================================================================
#include "AppSettings.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace tcbw {

namespace {

//  Default keys used inside the "tcbw/" namespace.
//  "tcbw/" 命名空间中使用的默认键。
constexpr const char *kThemeMode = "appearance/themeMode";
constexpr const char *kLanguage = "appearance/language";
constexpr const char *kUiScale = "appearance/uiScale";
constexpr const char *kReduceMotion = "appearance/reduceMotion";

constexpr const char *kShowGrid = "editor/showGrid";
constexpr const char *kGridSize = "editor/gridSize";
constexpr const char *kSnapToGrid = "editor/snapToGrid";
constexpr const char *kSnapToGuides = "editor/snapToGuides";
constexpr const char *kShowRulers = "editor/showRulers";
constexpr const char *kAutoSave = "editor/autoSave";
constexpr const char *kAutoSaveInterval = "editor/autoSaveIntervalSeconds";
constexpr const char *kDefaultBreakpoint = "editor/defaultBreakpoint";

constexpr const char *kExportMode = "export/mode";
constexpr const char *kPrettyPrint = "export/prettyPrint";
constexpr const char *kIndentWidth = "export/indentWidth";
constexpr const char *kIncludeResetCss = "export/includeResetCss";
constexpr const char *kOpenAfterExport = "export/openAfterExport";
constexpr const char *kLastExportDir = "export/lastDirectory";
constexpr const char *kLastProjectDir = "project/lastDirectory";

constexpr const char *kProviders = "ai/providers";
constexpr const char *kLastVendor = "ai/lastVendor";
constexpr const char *kAiEnabled = "ai/enabled";
constexpr const char *kAiAutoApproveSafe = "ai/autoApproveSafeTools";
constexpr const char *kAiApprovalPolicy = "ai/approvalPolicy";
constexpr const char *kAiMaxToolIterations = "ai/maxToolIterations";
constexpr const char *kAiMaxTranscript = "ai/maxTranscriptMessages";
constexpr const char *kAiIncludeOutline = "ai/includeProjectOutline";
constexpr const char *kMcpServers = "ai/mcpServers";

constexpr const char *kTelemetry = "privacy/telemetryEnabled";
constexpr const char *kSendOutline = "privacy/sendProjectOutlineToModel";
constexpr const char *kRedactSecrets = "privacy/redactSecretsInTranscripts";

constexpr const char *kPreviewEnabled = "preview/serverEnabled";
constexpr const char *kPreviewReload = "preview/reloadOnChange";
constexpr const char *kPreviewBrowser = "preview/externalBrowserCommand";

//  The language a fresh installation starts in: the OS locale decides.
//  全新安装时的起始语言：由操作系统区域设置决定。
QString detectLanguage()
{
    const QString code = QLocale::system().name();       // e.g. "zh_CN"
    if (code.startsWith(QLatin1String("zh"), Qt::CaseInsensitive))
        return QStringLiteral("zh");
    return QStringLiteral("en");
}

//  Path of the INI file that backs both AppSettings and ProjectIO.
//  同时支撑 AppSettings 与 ProjectIO 的 INI 文件路径。
QString settingsDirectoryPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (base.isEmpty())
            base = QDir::homePath();
    }
    return base;
}

} // namespace

// ---------------------------------------------------------------------------
//  Impl -- the typed view over QSettings
//  Impl —— QSettings 之上的类型化视图
// ---------------------------------------------------------------------------
class AppSettings::Impl
{
public:
    Impl()
        : settings(QSettings::IniFormat, QSettings::UserScope,
                   QStringLiteral("turtlelnc"), QStringLiteral("TCTools-Build_Web"))
    {
        //  Keep the ini file in the application's own configuration directory.
        //  将 ini 文件保存在应用自身的配置目录中。
        const QString dir = settingsDirectoryPath();
        if (!dir.isEmpty() && QDir().mkpath(dir))
            settings.setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    }

    QString key(const QString &suffix) const
    {
        return QStringLiteral("tcbw/") + suffix;
    }

    QString stringValue(const char *suffix, const QString &fallback) const
    {
        const QVariant value = settings.value(key(QString::fromLatin1(suffix)));
        if (!value.isValid())
            return fallback;
        const QString text = value.toString();
        return text.isEmpty() ? fallback : text;
    }

    bool boolValue(const char *suffix, bool fallback) const
    {
        const QVariant value = settings.value(key(QString::fromLatin1(suffix)));
        return value.isValid() ? value.toBool() : fallback;
    }

    int intValue(const char *suffix, int fallback) const
    {
        const QVariant value = settings.value(key(QString::fromLatin1(suffix)));
        return value.isValid() ? value.toInt() : fallback;
    }

    double doubleValue(const char *suffix, double fallback) const
    {
        const QVariant value = settings.value(key(QString::fromLatin1(suffix)));
        return value.isValid() ? value.toDouble() : fallback;
    }

    QSettings settings;
};

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
}

AppSettings::~AppSettings() = default;

AppSettings &AppSettings::instance()
{
    //  Function-local static: created on first use, destroyed with the program.
    //  函数内静态对象：首次使用时创建，随程序结束销毁。
    static AppSettings singleton;
    return singleton;
}

QString AppSettings::key(const QString &suffix) const
{
    return d ? d->key(suffix) : QStringLiteral("tcbw/") + suffix;
}

// ---------------------------------------------------------------------------
//  Appearance
//  外观
// ---------------------------------------------------------------------------
ThemeMode AppSettings::themeMode() const
{
    return themeModeFromString(d->stringValue(kThemeMode, QStringLiteral("auto")),
                               ThemeMode::Auto);
}

void AppSettings::setThemeMode(ThemeMode mode)
{
    if (themeMode() == mode)
        return;
    d->settings.setValue(key(QString::fromLatin1(kThemeMode)), toString(mode));
    d->settings.sync();
    emit themeChanged(mode);
}

QString AppSettings::language() const
{
    return d->stringValue(kLanguage, detectLanguage());
}

void AppSettings::setLanguage(const QString &code)
{
    const QString next = code.trimmed().isEmpty() ? detectLanguage() : code.trimmed();
    if (language() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kLanguage)), next);
    d->settings.sync();
    emit languageChanged(next);
}

double AppSettings::uiScale() const
{
    const double value = d->doubleValue(kUiScale, 1.0);
    //  Clamp to a range the layouts can actually survive.
    //  限制在布局真正能承受的范围内。
    return std::clamp(value, 0.5, 3.0);
}

void AppSettings::setUiScale(double scale)
{
    const double next = std::clamp(scale, 0.5, 3.0);
    if (std::fabs(uiScale() - next) < 0.001)
        return;
    d->settings.setValue(key(QString::fromLatin1(kUiScale)), next);
    d->settings.sync();
    emit editorSettingsChanged();
}

bool AppSettings::reduceMotion() const
{
    return d->boolValue(kReduceMotion, false);
}

void AppSettings::setReduceMotion(bool value)
{
    if (reduceMotion() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kReduceMotion)), value);
    d->settings.sync();
    emit themeChanged(themeMode());
}

// ---------------------------------------------------------------------------
//  Editor
//  编辑器
// ---------------------------------------------------------------------------
bool AppSettings::showGrid() const
{
    return d->boolValue(kShowGrid, true);
}

void AppSettings::setShowGrid(bool value)
{
    if (showGrid() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kShowGrid)), value);
    d->settings.sync();
    emit editorSettingsChanged();
}

int AppSettings::gridSize() const
{
    //  A grid smaller than 2 px is not useful to anyone.
    //  小于 2 像素的网格对任何人都没有意义。
    return std::clamp(d->intValue(kGridSize, 8), 2, 256);
}

void AppSettings::setGridSize(int size)
{
    const int next = std::clamp(size, 2, 256);
    if (gridSize() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kGridSize)), next);
    d->settings.sync();
    emit editorSettingsChanged();
}

bool AppSettings::snapToGrid() const
{
    return d->boolValue(kSnapToGrid, true);
}

void AppSettings::setSnapToGrid(bool value)
{
    if (snapToGrid() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kSnapToGrid)), value);
    d->settings.sync();
    emit editorSettingsChanged();
}

bool AppSettings::snapToGuides() const
{
    return d->boolValue(kSnapToGuides, true);
}

void AppSettings::setSnapToGuides(bool value)
{
    if (snapToGuides() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kSnapToGuides)), value);
    d->settings.sync();
    emit editorSettingsChanged();
}

bool AppSettings::showRulers() const
{
    return d->boolValue(kShowRulers, true);
}

void AppSettings::setShowRulers(bool value)
{
    if (showRulers() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kShowRulers)), value);
    d->settings.sync();
    emit editorSettingsChanged();
}

bool AppSettings::autoSave() const
{
    return d->boolValue(kAutoSave, true);
}

void AppSettings::setAutoSave(bool value)
{
    if (autoSave() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAutoSave)), value);
    d->settings.sync();
    emit editorSettingsChanged();
}

int AppSettings::autoSaveIntervalSeconds() const
{
    return std::clamp(d->intValue(kAutoSaveInterval, 120), 5, 3600);
}

void AppSettings::setAutoSaveIntervalSeconds(int seconds)
{
    const int next = std::clamp(seconds, 5, 3600);
    if (autoSaveIntervalSeconds() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAutoSaveInterval)), next);
    d->settings.sync();
    emit editorSettingsChanged();
}

Breakpoint AppSettings::defaultBreakpoint() const
{
    return breakpointFromString(d->stringValue(kDefaultBreakpoint, QStringLiteral("desktop")),
                                Breakpoint::Desktop);
}

void AppSettings::setDefaultBreakpoint(Breakpoint bp)
{
    if (defaultBreakpoint() == bp)
        return;
    d->settings.setValue(key(QString::fromLatin1(kDefaultBreakpoint)), toString(bp));
    d->settings.sync();
    emit editorSettingsChanged();
}

// ---------------------------------------------------------------------------
//  Code generation
//  代码生成
// ---------------------------------------------------------------------------
ExportMode AppSettings::defaultExportMode() const
{
    return exportModeFromString(d->stringValue(kExportMode, QStringLiteral("single-html")),
                                ExportMode::SingleHtml);
}

void AppSettings::setDefaultExportMode(ExportMode mode)
{
    if (defaultExportMode() == mode)
        return;
    d->settings.setValue(key(QString::fromLatin1(kExportMode)), toString(mode));
    d->settings.sync();
    emit exportSettingsChanged();
}

bool AppSettings::prettyPrint() const
{
    return d->boolValue(kPrettyPrint, true);
}

void AppSettings::setPrettyPrint(bool value)
{
    if (prettyPrint() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kPrettyPrint)), value);
    d->settings.sync();
    emit exportSettingsChanged();
}

int AppSettings::indentWidth() const
{
    return std::clamp(d->intValue(kIndentWidth, 2), 1, 8);
}

void AppSettings::setIndentWidth(int width)
{
    const int next = std::clamp(width, 1, 8);
    if (indentWidth() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kIndentWidth)), next);
    d->settings.sync();
    emit exportSettingsChanged();
}

bool AppSettings::includeResetCss() const
{
    return d->boolValue(kIncludeResetCss, true);
}

void AppSettings::setIncludeResetCss(bool value)
{
    if (includeResetCss() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kIncludeResetCss)), value);
    d->settings.sync();
    emit exportSettingsChanged();
}

bool AppSettings::openAfterExport() const
{
    return d->boolValue(kOpenAfterExport, false);
}

void AppSettings::setOpenAfterExport(bool value)
{
    if (openAfterExport() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kOpenAfterExport)), value);
    d->settings.sync();
    emit exportSettingsChanged();
}

QString AppSettings::lastExportDirectory() const
{
    return d->stringValue(kLastExportDir, QStandardPaths::writableLocation(
                                             QStandardPaths::DocumentsLocation));
}

void AppSettings::setLastExportDirectory(const QString &directory)
{
    if (lastExportDirectory() == directory)
        return;
    d->settings.setValue(key(QString::fromLatin1(kLastExportDir)), directory);
    d->settings.sync();
    emit exportSettingsChanged();
}

QString AppSettings::lastProjectDirectory() const
{
    return d->stringValue(kLastProjectDir, QStandardPaths::writableLocation(
                                              QStandardPaths::DocumentsLocation));
}

void AppSettings::setLastProjectDirectory(const QString &directory)
{
    if (lastProjectDirectory() == directory)
        return;
    d->settings.setValue(key(QString::fromLatin1(kLastProjectDir)), directory);
    d->settings.sync();
    emit editorSettingsChanged();
}

// ---------------------------------------------------------------------------
//  AI / TI-UseAgent
//  AI / TI-UseAgent
// ---------------------------------------------------------------------------
ProviderConfig AppSettings::providerConfig(const QString &vendorId) const
{
    const QString stored = d->settings.value(key(QString::fromLatin1(kProviders)))
                               .toString();
    if (stored.isEmpty()) {
        ProviderConfig empty;
        empty.vendorId = vendorId;
        return empty;
    }

    const QJsonObject map = QJsonDocument::fromJson(stored.toUtf8()).object();
    if (!map.contains(vendorId)) {
        ProviderConfig empty;
        empty.vendorId = vendorId;
        return empty;
    }

    ProviderConfig config =
        ProviderConfig::fromJson(map.value(vendorId).toObject());
    if (config.vendorId.isEmpty())
        config.vendorId = vendorId;
    return config;
}

void AppSettings::setProviderConfig(const ProviderConfig &config)
{
    if (config.vendorId.trimmed().isEmpty())
        return;

    //  The whole map is rewritten because QSettings stores one JSON string; it
    //  is small, and a single write keeps the file consistent.
    //  整个映射会被重写，因为 QSettings 只保存一个 JSON 字符串；它很小，一次写入
    //  就能保持文件一致。
    const QString stored = d->settings.value(key(QString::fromLatin1(kProviders)))
                               .toString();
    QJsonObject map;
    if (!stored.isEmpty())
        map = QJsonDocument::fromJson(stored.toUtf8()).object();

    const QJsonObject previous = map.value(config.vendorId).toObject();
    const QJsonObject updated = config.toJson();
    if (previous == updated)
        return;

    map.insert(config.vendorId, updated);
    d->settings.setValue(key(QString::fromLatin1(kProviders)),
                         QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
    d->settings.sync();
    emit aiSettingsChanged();
}

QString AppSettings::lastUsedVendor() const
{
    return d->stringValue(kLastVendor, QString());
}

void AppSettings::setLastUsedVendor(const QString &vendorId)
{
    if (lastUsedVendor() == vendorId)
        return;
    d->settings.setValue(key(QString::fromLatin1(kLastVendor)), vendorId);
    d->settings.sync();
    emit aiSettingsChanged();
}

bool AppSettings::aiEnabled() const
{
    return d->boolValue(kAiEnabled, true);
}

void AppSettings::setAiEnabled(bool value)
{
    if (aiEnabled() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAiEnabled)), value);
    d->settings.sync();
    emit aiSettingsChanged();
}

bool AppSettings::aiAutoApproveSafeTools() const
{
    return d->boolValue(kAiAutoApproveSafe, true);
}

void AppSettings::setAiAutoApproveSafeTools(bool value)
{
    if (aiAutoApproveSafeTools() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAiAutoApproveSafe)), value);
    d->settings.sync();
    emit aiSettingsChanged();
}

QString AppSettings::aiApprovalPolicy() const
{
    return d->stringValue(kAiApprovalPolicy, QStringLiteral("ask-destructive"));
}

void AppSettings::setAiApprovalPolicy(const QString &policy)
{
    const QString next = policy.trimmed().isEmpty() ? QStringLiteral("ask-destructive")
                                                    : policy.trimmed();
    if (aiApprovalPolicy() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAiApprovalPolicy)), next);
    d->settings.sync();
    emit aiSettingsChanged();
}

int AppSettings::aiMaxToolIterations() const
{
    return std::clamp(d->intValue(kAiMaxToolIterations, 12), 1, 100);
}

void AppSettings::setAiMaxToolIterations(int iterations)
{
    const int next = std::clamp(iterations, 1, 100);
    if (aiMaxToolIterations() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAiMaxToolIterations)), next);
    d->settings.sync();
    emit aiSettingsChanged();
}

int AppSettings::aiMaxTranscriptMessages() const
{
    return std::clamp(d->intValue(kAiMaxTranscript, 40), 4, 1000);
}

void AppSettings::setAiMaxTranscriptMessages(int messages)
{
    const int next = std::clamp(messages, 4, 1000);
    if (aiMaxTranscriptMessages() == next)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAiMaxTranscript)), next);
    d->settings.sync();
    emit aiSettingsChanged();
}

bool AppSettings::aiIncludeProjectOutline() const
{
    return d->boolValue(kAiIncludeOutline, true);
}

void AppSettings::setAiIncludeProjectOutline(bool value)
{
    if (aiIncludeProjectOutline() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kAiIncludeOutline)), value);
    d->settings.sync();
    emit aiSettingsChanged();
}

QVector<McpServerConfig> AppSettings::mcpServers() const
{
    QVector<McpServerConfig> servers;
    const QString stored = d->settings.value(key(QString::fromLatin1(kMcpServers)))
                               .toString();
    if (stored.isEmpty())
        return servers;

    const QJsonArray array = QJsonDocument::fromJson(stored.toUtf8()).array();
    servers.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const McpServerConfig server = McpServerConfig::fromJson(value.toObject());
        if (!server.id.trimmed().isEmpty())
            servers.append(server);
    }
    return servers;
}

void AppSettings::setMcpServers(const QVector<McpServerConfig> &servers)
{
    //  Compare through JSON so an unchanged list never triggers a write.
    //  通过 JSON 比较，使未变化的列表绝不触发写入。
    QJsonArray previous;
    const QString stored = d->settings.value(key(QString::fromLatin1(kMcpServers)))
                               .toString();
    if (!stored.isEmpty())
        previous = QJsonDocument::fromJson(stored.toUtf8()).array();

    QJsonArray updated;
    for (const McpServerConfig &server : servers)
        updated.append(server.toJson());

    if (previous == updated)
        return;

    d->settings.setValue(key(QString::fromLatin1(kMcpServers)),
                         QString::fromUtf8(
                             QJsonDocument(updated).toJson(QJsonDocument::Compact)));
    d->settings.sync();
    emit mcpServersChanged();
}

void AppSettings::addMcpServer(const McpServerConfig &server)
{
    if (server.id.trimmed().isEmpty())
        return;

    QVector<McpServerConfig> servers = mcpServers();
    for (McpServerConfig &existing : servers) {
        if (existing.id == server.id) {
            if (existing.toJson() == server.toJson())
                return;
            existing = server;
            setMcpServers(servers);
            return;
        }
    }
    servers.append(server);
    setMcpServers(servers);
}

void AppSettings::removeMcpServer(const QString &id)
{
    if (id.trimmed().isEmpty())
        return;

    QVector<McpServerConfig> servers = mcpServers();
    const int before = servers.size();
    for (int i = servers.size() - 1; i >= 0; --i) {
        if (servers.at(i).id == id)
            servers.remove(i);
    }
    if (servers.size() == before)
        return;
    setMcpServers(servers);
}

// ---------------------------------------------------------------------------
//  Privacy
//  隐私
// ---------------------------------------------------------------------------
bool AppSettings::telemetryEnabled() const
{
    //  This project ships zero telemetry, so the setting exists only so the UI
    //  can show a permanently disabled switch. The getter ignores whatever is on
    //  disk -- an older build or a hand-edited ini file must not be able to turn
    //  network reporting of user behaviour back on.
    //  本项目不包含任何遥测，因此该设置仅用于让界面显示一个永久禁用的开关。读取函数
    //  忽略磁盘上的值——旧版本或手工编辑过的 ini 文件都不能重新开启对用户行为的
    //  网络上报。
    return false;
}

void AppSettings::setTelemetryEnabled(bool value)
{
    //  Writes are recorded (so the UI can remember that the user tried) but the
    //  value is always stored as false, because telemetryEnabled() can never
    //  return true in this build.
    //  允许记录写入（以便界面记住用户的尝试），但该值始终以 false 存储，因为在本版本
    //  中 telemetryEnabled() 永远不会返回 true。
    const bool effective = value && false;
    if (d->boolValue(kTelemetry, false) == effective)
        return;
    d->settings.setValue(key(QString::fromLatin1(kTelemetry)), effective);
    d->settings.sync();
}

bool AppSettings::sendProjectOutlineToModel() const
{
    return d->boolValue(kSendOutline, true);
}

void AppSettings::setSendProjectOutlineToModel(bool value)
{
    if (sendProjectOutlineToModel() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kSendOutline)), value);
    d->settings.sync();
    emit aiSettingsChanged();
}

bool AppSettings::redactSecretsInTranscripts() const
{
    return d->boolValue(kRedactSecrets, true);
}

void AppSettings::setRedactSecretsInTranscripts(bool value)
{
    if (redactSecretsInTranscripts() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kRedactSecrets)), value);
    d->settings.sync();
    emit aiSettingsChanged();
}

// ---------------------------------------------------------------------------
//  Preview
//  预览
// ---------------------------------------------------------------------------
bool AppSettings::previewServerEnabled() const
{
    return d->boolValue(kPreviewEnabled, true);
}

void AppSettings::setPreviewServerEnabled(bool value)
{
    if (previewServerEnabled() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kPreviewEnabled)), value);
    d->settings.sync();
    emit previewSettingsChanged();
}

bool AppSettings::previewReloadOnChange() const
{
    return d->boolValue(kPreviewReload, true);
}

void AppSettings::setPreviewReloadOnChange(bool value)
{
    if (previewReloadOnChange() == value)
        return;
    d->settings.setValue(key(QString::fromLatin1(kPreviewReload)), value);
    d->settings.sync();
    emit previewSettingsChanged();
}

QString AppSettings::externalBrowserCommand() const
{
    return d->stringValue(kPreviewBrowser, QString());
}

void AppSettings::setExternalBrowserCommand(const QString &command)
{
    if (externalBrowserCommand() == command)
        return;
    d->settings.setValue(key(QString::fromLatin1(kPreviewBrowser)), command);
    d->settings.sync();
    emit previewSettingsChanged();
}

// ---------------------------------------------------------------------------
//  Maintenance
//  维护
// ---------------------------------------------------------------------------
void AppSettings::sync()
{
    d->settings.sync();
}

void AppSettings::resetToDefaults()
{
    //  Dropping the whole "tcbw/" group restores every default at once; the
    //  signals below then tell each part of the UI to re-read its section.
    //  删除整个 "tcbw/" 分组即可一次性恢复全部默认值；下面的信号随后通知界面各部
    //  分重新读取各自的配置段。
    d->settings.remove(QStringLiteral("tcbw"));
    d->settings.sync();

    emit themeChanged(themeMode());
    emit languageChanged(language());
    emit editorSettingsChanged();
    emit exportSettingsChanged();
    emit aiSettingsChanged();
    emit mcpServersChanged();
    emit previewSettingsChanged();
}

QString AppSettings::settingsFilePath() const
{
    return d->settings.fileName();
}

} // namespace tcbw
