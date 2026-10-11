// ============================================================================
//  AppController.cpp -- implementation of the QML-facing application facade
//  AppController.cpp —— 面向 QML 的应用程序门面的实现
//
//  Part of TCTools-Build_Web (TC-BuildWeb).
//  属于 TCTools-Build_Web（TC-BuildWeb）。
// ============================================================================
#include "AppController.h"

#include "CodeGenerator.h"
#include "EmitOptions.h"
#include "IdGenerator.h"
#include "Page.h"
#include "Types.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QSysInfo>
#include <QUrl>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Transient bookkeeping
//  The public headers of this module are frozen, so state that is an
//  implementation detail (a cache used to avoid redundant signals) lives in
//  dynamic properties of the controller instead of in new data members.
//  临时记录
//  本模块的公开头文件已冻结，因此属于实现细节的状态（用于避免冗余信号的缓存）
//  保存在控制器的动态属性中，而不是新增数据成员。
// ---------------------------------------------------------------------------
constexpr char kPageIdsProperty[] = "_tcbwPageIds";
constexpr char kThemeForwardProperty[] = "_tcbwForwardedTheme";
constexpr char kLanguageForwardProperty[] = "_tcbwForwardedLanguage";
constexpr char kExportForwardProperty[] = "_tcbwForwardedExportMode";

// ---------------------------------------------------------------------------
//  Test hooks
//  A headless test cannot drive a QML dialog, so the shims below accept a path
//  from the environment. They are never used by the shipped user interface.
//  测试钩子
//  无界面测试无法驱动 QML 对话框，因此下面的垫片允许从环境变量取得路径。
//  正式发布的用户界面永远不会用到它们。
// ---------------------------------------------------------------------------
constexpr char kOpenDialogHook[] = "TCBW_OPEN_DIALOG_HOOK";
constexpr char kSaveDialogHook[] = "TCBW_SAVE_DIALOG_HOOK";
constexpr char kExportDirHook[] = "TCBW_EXPORT_DIR_HOOK";

// ---------------------------------------------------------------------------
//  The ids of every page, in document order.
//  按文档顺序排列的全部页面 id。
// ---------------------------------------------------------------------------
QStringList pageIdList(const ProjectDocument *document)
{
    QStringList ids;
    if (!document)
        return ids;
    ids.reserve(document->pageCount());
    for (int i = 0; i < document->pageCount(); ++i) {
        if (const Page *page = document->pageAt(i))
            ids.append(page->id);
    }
    return ids;
}

// ---------------------------------------------------------------------------
//  The page that should become active after a document was installed: the one
//  it remembers, otherwise the home page, otherwise the first page.
//  安装文档后应成为活动页面的页面：文档记住的那个，否则首页，否则第一个页面。
// ---------------------------------------------------------------------------
QString pickCurrentPageId(const ProjectDocument *document)
{
    if (!document)
        return QString();
    if (Page *page = document->pageById(document->activePageId))
        return page->id;
    if (Page *page = document->homePage())
        return page->id;
    if (Page *page = document->pageAt(0))
        return page->id;
    return QString();
}

// ---------------------------------------------------------------------------
//  Installs a freshly loaded document together with a brand new undo stack.
//  The old stack is destroyed first: it holds raw pointers into the old model.
//  安装刚载入的文档以及一套全新的撤销栈。
//  旧栈先被销毁：它持有指向旧模型的裸指针。
// ---------------------------------------------------------------------------
void installDocument(std::unique_ptr<ProjectDocument> &slot,
                     std::unique_ptr<CommandStack> &stack,
                     std::unique_ptr<ProjectDocument> next, QObject *parent)
{
    stack.reset();
    slot = std::move(next);
    stack = std::make_unique<CommandStack>(slot.get(), parent);
}

// ---------------------------------------------------------------------------
//  Announces a value exactly once.
//  A change may originate here (the settings object then echoes it back) or in
//  the settings object itself. Remembering what was already forwarded keeps the
//  property-change signal free of duplicates in both directions.
//  只通告一次某个值。
//  变化可能源自本控制器（settings 随后回传），也可能源自 settings 自身。记住已经
//  转发过的值，可使属性变更信号在两种方向上都不重复。
// ---------------------------------------------------------------------------
bool forwardOnce(QObject *self, const char *key, const QString &value)
{
    if (self->property(key).toString() == value)
        return false;
    self->setProperty(key, value);
    return true;
}

// ---------------------------------------------------------------------------
//  A short, localised "there is nothing to work on" message.
//  简短的本地化"无可操作对象"消息。
// ---------------------------------------------------------------------------
QString noProjectMessage()
{
    return QStringLiteral("No project is open. / 当前没有打开的工程。");
}

// ---------------------------------------------------------------------------
//  Details of a file picker that this layer deliberately does not own.
//  本层刻意不拥有的文件选择器的说明。
// ---------------------------------------------------------------------------
QString filePickerIsOwnedByQml()
{
    return QStringLiteral(
        "This layer does not open native file pickers: use the QML FileDialog and call "
        "openProject(path) / saveProjectTo(path). "
        "/ 本层不打开原生文件选择器：请使用 QML 的 FileDialog 并调用 openProject(path) "
        "/ saveProjectTo(path)。");
}

QString folderPickerIsOwnedByQml()
{
    return QStringLiteral(
        "This layer does not open native folder pickers: use the QML FolderDialog and call "
        "exportToDirectory(directory). "
        "/ 本层不打开原生文件夹选择器：请使用 QML 的 FolderDialog 并调用 "
        "exportToDirectory(directory)。");
}

// ---------------------------------------------------------------------------
//  A path injected by a test through one of the hook variables, or an empty
//  string when no hook is set.
//  测试通过某个钩子变量注入的路径；未设置钩子时为空字符串。
// ---------------------------------------------------------------------------
QString dialogHookPath(const char *specificKey)
{
    const QString specific = qEnvironmentVariable(specificKey);
    if (!specific.isEmpty())
        return specific;
    return qEnvironmentVariable(kOpenDialogHook);
}

// ---------------------------------------------------------------------------
//  Builds the emitter options for one export from the persisted preferences.
//  依据持久化的偏好为一次导出构建生成器选项。
// ---------------------------------------------------------------------------
EmitOptions emitOptionsFor(const AppSettings &settings, const ProjectDocument &project,
                           ExportMode mode)
{
    EmitOptions options;
    options.mode = mode;
    options.pretty = settings.prettyPrint();
    options.indentWidth = qBound(1, settings.indentWidth(), 8);
    options.includeResetCss = settings.includeResetCss();
    options.siteTitle = project.name;
    options.siteDescription = project.description;
    options.author = project.author;
    options.language = project.defaultLanguage.isEmpty() ? QStringLiteral("en")
                                                         : project.defaultLanguage;
    //  Only a self-contained single file is inlined; every other mode keeps the
    //  separate .css/.js files the mode is named after.
    //  只有自包含的单文件模式使用内联；其他模式都保留与其名称相符的独立 .css/.js。
    options.inlineCss = (mode == ExportMode::SingleHtml);
    options.inlineJs = (mode == ExportMode::SingleHtml);
    return options;
}

// ---------------------------------------------------------------------------
//  Renders the error list of a failed emit as a single localised line.
//  将失败生成的错误列表渲染为一行本地化文本。
// ---------------------------------------------------------------------------
QString emitErrorText(const EmitResult &result)
{
    if (!result.errors.isEmpty())
        return result.errors.join(QStringLiteral("\n"));
    return QStringLiteral("Code generation failed. / 代码生成失败。");
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction / destruction
//  构造 / 析构
// ---------------------------------------------------------------------------
AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_io(std::make_unique<ProjectIO>(this))
    , m_settings(AppSettings::instance())
{
    //  The application starts on the welcome screen: the constructor restores
    //  the preferences and the recent-file list but deliberately creates no
    //  document.
    //  应用程序从欢迎界面开始：构造函数恢复偏好设置与最近文件列表，但刻意不创建文档。
    m_breakpoint = m_settings.defaultBreakpoint();
    m_exportMode = m_settings.defaultExportMode();

    m_recentFiles = m_io->recentFiles();
    m_io->enableAutosave(m_settings.autoSave(), m_settings.autoSaveIntervalSeconds());

    //  Every preference that has a matching property here is mirrored back so
    //  that a change made elsewhere in the shell (or by the AI settings panel)
    //  updates the whole user interface.
    //  每个在此处有对应属性的偏好都会被镜像回来，使外壳其他部分（或 AI 设置面板）
    //  做出的修改能更新整个用户界面。
    connect(&m_settings, &AppSettings::themeChanged, this, [this](ThemeMode mode) {
        if (forwardOnce(this, kThemeForwardProperty, toString(mode)))
            emit themeModeChanged();
    });
    connect(&m_settings, &AppSettings::languageChanged, this, [this](const QString &code) {
        if (forwardOnce(this, kLanguageForwardProperty, code))
            emit languageChanged();
    });
    connect(&m_settings, &AppSettings::exportSettingsChanged, this, [this]() {
        const ExportMode mode = m_settings.defaultExportMode();
        if (mode == m_exportMode)
            return;
        m_exportMode = mode;
        forwardOnce(this, kExportForwardProperty, toString(mode));
        applyExportModeToGenerator();
        emit exportModeChanged();
    });
}

AppController::~AppController()
{
    //  Nothing but the preferences is persisted: the autosave timer must stop
    //  first, then the settings are flushed to disk.
    //  除偏好设置外不持久化任何内容：先停止自动保存计时器，然后把设置刷写到磁盘。
    if (m_io)
        m_io->enableAutosave(false);
    m_settings.sync();
}

// ---------------------------------------------------------------------------
//  Constants
//  常量
// ---------------------------------------------------------------------------
QString AppController::version() const
{
    return tcbw::versionString();
}

QString AppController::versionFull() const
{
    return tcbw::versionString();
}

QString AppController::versionChannel() const
{
    return tcbw::versionChannel();
}

QString AppController::buildStamp() const
{
    return tcbw::buildTimestamp();
}

QString AppController::generatorAttribution() const
{
    return QStringLiteral("Generated with DeepSeek V4.1 Flash on DeepSeek Harness");
}

// ---------------------------------------------------------------------------
//  Document identity
//  文档标识
// ---------------------------------------------------------------------------
QString AppController::projectName() const
{
    return m_document ? m_document->name : QString();
}

void AppController::setProjectName(const QString &name)
{
    if (!m_document || m_document->name == name)
        return;
    m_document->name = name;
    m_document->setModified(true);
    emit projectChanged();
}

QString AppController::projectPath() const
{
    return m_document ? m_document->filePath : QString();
}

QString AppController::projectDisplayPath() const
{
    //  Empty until the project has been saved once; the QML layer renders its
    //  own "not saved yet" hint for that case.
    //  在工程首次保存之前为空；该情况下由 QML 层自行显示"尚未保存"的提示。
    if (!m_document || m_document->filePath.isEmpty())
        return QString();
    return QDir::toNativeSeparators(m_document->filePath);
}

bool AppController::modified() const
{
    return m_document && m_document->modified;
}

// ---------------------------------------------------------------------------
//  Pages
//  页面
// ---------------------------------------------------------------------------
QString AppController::currentPageId() const
{
    return m_document ? m_document->activePageId : QString();
}

void AppController::setCurrentPageId(const QString &pageId)
{
    if (!m_document)
        return;
    if (pageId.isEmpty() || !m_document->pageById(pageId)) {
        if (!pageId.isEmpty()) {
            reportError(QStringLiteral("No page with that id exists. / 不存在具有该 id 的页面。"));
            return;
        }
    }
    if (m_document->activePageId == pageId)
        return;
    m_document->activePageId = pageId;
    emit currentPageChanged(pageId);
}

Page *AppController::currentPage() const
{
    if (!m_document)
        return nullptr;
    if (Page *page = m_document->pageById(m_document->activePageId))
        return page;
    if (Page *page = m_document->homePage())
        return page;
    return m_document->pageAt(0);
}

QString AppController::currentPageTitle() const
{
    const Page *page = currentPage();
    return page ? page->title : QString();
}

QStringList AppController::pageNames() const
{
    QStringList names;
    if (!m_document)
        return names;
    names.reserve(m_document->pageCount());
    for (int i = 0; i < m_document->pageCount(); ++i) {
        if (const Page *page = m_document->pageAt(i))
            names.append(page->name);
    }
    return names;
}

QVariantList AppController::pages() const
{
    QVariantList list;
    if (!m_document)
        return list;
    const QString activeId = m_document->activePageId;
    list.reserve(m_document->pageCount());
    for (int i = 0; i < m_document->pageCount(); ++i) {
        const Page *page = m_document->pageAt(i);
        if (!page)
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("id"), page->id);
        map.insert(QStringLiteral("name"), page->name);
        map.insert(QStringLiteral("fileName"), page->fileName);
        map.insert(QStringLiteral("title"), page->title);
        map.insert(QStringLiteral("isHome"), page->isHome);
        map.insert(QStringLiteral("nodeCount"), page->nodeCount());
        map.insert(QStringLiteral("isCurrent"), page->id == activeId);
        list.append(map);
    }
    return list;
}

// ---------------------------------------------------------------------------
//  View state
//  视图状态
// ---------------------------------------------------------------------------
QString AppController::breakpoint() const
{
    return toString(m_breakpoint);
}

void AppController::setBreakpoint(const QString &key)
{
    //  An unknown key is ignored rather than treated as Desktop, so a typo in
    //  QML cannot silently move the whole canvas.
    //  未知键会被忽略而不是当作桌面端，因此 QML 中的拼写错误不会悄悄移动整个画布。
    const Breakpoint wanted = breakpointFromString(key, m_breakpoint);
    if (wanted == m_breakpoint)
        return;
    m_breakpoint = wanted;
    if (m_document)
        m_document->activeBreakpoint = m_breakpoint;
    emit breakpointChanged(toString(m_breakpoint));
}

QString AppController::themeMode() const
{
    return toString(m_settings.themeMode());
}

void AppController::setThemeMode(const QString &key)
{
    const ThemeMode wanted = themeModeFromString(key, m_settings.themeMode());
    if (wanted == m_settings.themeMode())
        return;
    forwardOnce(this, kThemeForwardProperty, toString(wanted));
    m_settings.setThemeMode(wanted);
    emit themeModeChanged();
}

bool AppController::darkTheme() const
{
    if (m_settings.themeMode() == ThemeMode::Auto) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        //  Follow the operating system only while it actually reports a scheme;
        //  otherwise fall back to the light presentation.
        //  仅当操作系统确实报告了配色方案时跟随它，否则回退到浅色呈现。
        if (QCoreApplication::instance() && qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            if (const QStyleHints *hints = QGuiApplication::styleHints())
                return hints->colorScheme() == Qt::ColorScheme::Dark;
        }
#endif
        return false;
    }
    return themeMode() == QLatin1String("dark");
}

QString AppController::language() const
{
    return m_settings.language();
}

void AppController::setLanguage(const QString &code)
{
    const QString normalised = code.trimmed().toLower();
    if (normalised.isEmpty() || normalised == m_settings.language())
        return;
    forwardOnce(this, kLanguageForwardProperty, normalised);
    m_settings.setLanguage(normalised);
    emit languageChanged();
}

bool AppController::chinese() const
{
    return m_settings.isChinese();
}

// ---------------------------------------------------------------------------
//  Undo / redo
//  撤销 / 重做
// ---------------------------------------------------------------------------
bool AppController::canUndo() const
{
    return m_commands && m_commands->canUndo();
}

bool AppController::canRedo() const
{
    return m_commands && m_commands->canRedo();
}

QString AppController::undoText() const
{
    return m_commands ? m_commands->undoText() : QString();
}

QString AppController::redoText() const
{
    return m_commands ? m_commands->redoText() : QString();
}

void AppController::undo()
{
    if (m_commands)
        m_commands->undo();
}

void AppController::redo()
{
    if (m_commands)
        m_commands->redo();
}

void AppController::clearUndoHistory()
{
    if (!m_commands)
        return;
    m_commands->clear();
    if (m_document)
        m_document->setModified(false);
    emit undoStackChanged();
}

// ---------------------------------------------------------------------------
//  Status reporting
//  状态报告
// ---------------------------------------------------------------------------
void AppController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void AppController::reportError(const QString &message, bool alsoLog)
{
    m_lastError = message;
    emit lastErrorChanged();
    setStatusMessage(message);
    if (alsoLog) {
        //  Every diagnostic line goes through the redactor: a message may quote
        //  a URL or a tool argument that carries a credential.
        //  每条诊断信息都要经过脱敏器：消息中可能引用带凭据的 URL 或工具参数。
        qWarning("%s", qUtf8Printable(tcbw::redactSecrets(message)));
    }
}

void AppController::setBusy(bool value)
{
    if (m_busy == value)
        return;
    m_busy = value;
    emit busyChanged();
}

// ---------------------------------------------------------------------------
//  Recent files
//  最近文件
// ---------------------------------------------------------------------------
QStringList AppController::recentFiles() const
{
    return m_recentFiles;
}

void AppController::refreshRecentFiles()
{
    const QStringList files = m_io ? m_io->recentFiles() : QStringList();
    if (files == m_recentFiles)
        return;
    m_recentFiles = files;
    emit recentFilesChanged();
}

void AppController::clearRecentFiles()
{
    if (m_io)
        m_io->clearRecentFiles();
    const bool hadEntries = !m_recentFiles.isEmpty();
    m_recentFiles.clear();
    if (hadEntries)
        emit recentFilesChanged();
}

bool AppController::removeRecentFile(const QString &filePath)
{
    const bool wasPresent = m_recentFiles.contains(filePath);
    if (m_io)
        m_io->removeRecentFile(filePath);
    refreshRecentFiles();
    return wasPresent;
}

// ---------------------------------------------------------------------------
//  Export mode
//  导出模式
// ---------------------------------------------------------------------------
QString AppController::exportMode() const
{
    return toString(m_exportMode);
}

void AppController::setExportMode(const QString &key)
{
    const ExportMode wanted = exportModeFromString(key, m_exportMode);
    if (wanted == m_exportMode)
        return;
    m_exportMode = wanted;
    forwardOnce(this, kExportForwardProperty, toString(wanted));
    applyExportModeToGenerator();
    emit exportModeChanged();
}

void AppController::applyExportModeToGenerator()
{
    //  There is no long-lived CodeGenerator to configure: exportToDirectory()
    //  builds EmitOptions and a CodeGenerator for every single export, which is
    //  what keeps generation re-entrant and free of stale state. This hook is
    //  therefore intentionally empty; it exists for API symmetry and as the one
    //  place a future cached generator would be refreshed.
    //  并不存在长期存在的 CodeGenerator 可供配置：exportToDirectory() 会为每一次
    //  导出构建 EmitOptions 与 CodeGenerator，这正是生成过程可重入且不残留状态的
    //  原因。因此本钩子刻意留空；它用于保持 API 对称，也是将来引入缓存生成器时
    //  唯一需要刷新它的地方。
}

// ---------------------------------------------------------------------------
//  Project lifecycle
//  工程生命周期
// ---------------------------------------------------------------------------
bool AppController::newProject(bool chineseDefaults)
{
    //  The QML layer has already asked the user about unsaved changes by the
    //  time this is called, so no dialog is shown here: the document is simply
    //  replaced.
    //  调用本函数时 QML 层已经就未保存的修改征询过用户，因此这里不显示任何对话框：
    //  直接替换文档。
    std::unique_ptr<ProjectDocument> document = ProjectDocument::makeStarter(chineseDefaults);
    if (!document) {
        reportError(QStringLiteral("The starter project could not be created. / "
                                   "无法创建初始工程。"));
        return false;
    }

    installDocument(m_document, m_commands, std::move(document), this);
    wireDocument();

    //  A fresh project follows the user's current view state instead of the
    //  state stored in a file.
    //  新建工程沿用用户当前的视图状态，而不是文件中保存的状态。
    m_document->activeBreakpoint = m_breakpoint;
    m_document->activePageId = pickCurrentPageId(m_document.get());
    m_document->setModified(false);

    m_io->enableAutosave(m_settings.autoSave(), m_settings.autoSaveIntervalSeconds());

    setProperty(kPageIdsProperty, pageIdList(m_document.get()));
    emit projectChanged();
    emit currentPageChanged(m_document->activePageId);
    emit pagesChanged();
    emit projectStructureChanged();
    emit modifiedChanged(false);
    emit undoStackChanged();
    setStatusMessage(QStringLiteral("New project created. / 已创建新工程。"));
    return true;
}

bool AppController::openProject(const QString &filePath)
{
    if (!m_io) {
        reportError(QStringLiteral("The project reader is unavailable. / 工程读取器不可用。"));
        return false;
    }
    if (filePath.trimmed().isEmpty()) {
        reportError(QStringLiteral("No file was given. / 未提供文件。"));
        return false;
    }

    QString error;
    std::unique_ptr<ProjectDocument> loaded = m_io->load(filePath, &error);
    if (!loaded) {
        reportError(error.isEmpty()
                        ? QStringLiteral("The project could not be opened. / 无法打开该工程。")
                        : error);
        return false;
    }

    const QString previousPageId = m_document ? m_document->activePageId : QString();

    installDocument(m_document, m_commands, std::move(loaded), this);
    wireDocument();
    m_document->filePath = QFileInfo(filePath).absoluteFilePath();
    m_document->setModified(false);

    //  Opening a project restores the breakpoint it was left at.
    //  打开工程会恢复离开时所在的断点。
    if (m_document->activeBreakpoint != m_breakpoint) {
        m_breakpoint = m_document->activeBreakpoint;
        emit breakpointChanged(toString(m_breakpoint));
    }

    //  A project whose remembered page no longer exists falls back to its home
    //  page, and then to its first page.
    //  若工程记住的页面已不存在，则回退到首页，再回退到第一个页面。
    m_document->activePageId = pickCurrentPageId(m_document.get());

    m_io->addRecentFile(m_document->filePath);
    refreshRecentFiles();
    m_settings.setLastProjectDirectory(QFileInfo(m_document->filePath).absolutePath());
    m_io->enableAutosave(m_settings.autoSave(), m_settings.autoSaveIntervalSeconds());

    setProperty(kPageIdsProperty, pageIdList(m_document.get()));
    emit projectChanged();
    if (m_document->activePageId != previousPageId)
        emit currentPageChanged(m_document->activePageId);
    emit pagesChanged();
    emit projectStructureChanged();
    emit modifiedChanged(false);
    emit undoStackChanged();
    setStatusMessage(QStringLiteral("Opened %1 / 已打开 %1").arg(m_document->filePath));
    return true;
}

bool AppController::openProjectDialog()
{
    //  ------------------------------------------------------------------------
    //  Compatibility shim -- the QML layer owns the file pickers.
    //  This layer must not link Qt Widgets (the product is Qt Quick only, and
    //  QtGui offers no portable native file dialog), so no picker can be opened
    //  from here. QML opens its own FileDialog and then calls
    //  openProject(path). For automated tests a path may be injected through the
    //  TCBW_OPEN_DIALOG_HOOK environment variable, in which case this shim
    //  behaves exactly as if the user had picked that file.
    //  ------------------------------------------------------------------------
    //  兼容垫片 —— 文件选择器由 QML 层负责。
    //  本层不得链接 Qt Widgets（本产品只使用 Qt Quick，且 QtGui 不提供可移植的原生
    //  文件对话框），因此无法从这里打开任何选择器。QML 打开自己的 FileDialog，然后
    //  调用 openProject(path)。自动化测试可通过环境变量 TCBW_OPEN_DIALOG_HOOK 注入
    //  路径，此时本垫片的行为与用户真的选择了该文件完全一致。
    //  ------------------------------------------------------------------------
    const QString hooked = dialogHookPath(kOpenDialogHook);
    if (!hooked.isEmpty())
        return openProject(hooked);

    reportError(filePickerIsOwnedByQml());
    return false;
}

bool AppController::saveProject()
{
    if (!m_document) {
        reportError(noProjectMessage());
        return false;
    }
    if (m_document->filePath.isEmpty())
        return saveProjectAsDialog();
    return saveProjectTo(m_document->filePath);
}

bool AppController::saveProjectAsDialog()
{
    //  Same shim as openProjectDialog(): QML owns the picker. See the long
    //  bilingual note there -- and note that TCBW_SAVE_DIALOG_HOOK takes
    //  precedence over TCBW_OPEN_DIALOG_HOOK for this one.
    //  与 openProjectDialog() 相同的垫片：选择器由 QML 负责。请参阅那里的双语说明
    //  —— 另外注意，此处 TCBW_SAVE_DIALOG_HOOK 优先于 TCBW_OPEN_DIALOG_HOOK。
    if (!m_document) {
        reportError(noProjectMessage());
        return false;
    }
    const QString hooked = dialogHookPath(kSaveDialogHook);
    if (!hooked.isEmpty())
        return saveProjectTo(hooked);

    reportError(filePickerIsOwnedByQml());
    return false;
}

bool AppController::saveProjectTo(const QString &filePath)
{
    if (!m_document || !m_io) {
        reportError(noProjectMessage());
        return false;
    }

    QString target = filePath.trimmed();
    if (target.isEmpty()) {
        reportError(QStringLiteral("No file was given. / 未提供文件。"));
        return false;
    }
    //  A path without any extension is completed to the project extension; an
    //  explicit extension chosen by the user is respected as-is.
    //  没有任何扩展名的路径会补全为工程扩展名；用户明确选择的扩展名会被原样保留。
    if (QFileInfo(target).suffix().isEmpty())
        target += QStringLiteral(".") + ProjectIO::fileExtension();
    target = QFileInfo(target).absoluteFilePath();

    QString error;
    if (!m_io->save(*m_document, target, &error)) {
        reportError(error.isEmpty()
                        ? QStringLiteral("The project could not be saved. / 无法保存该工程。")
                        : error);
        return false;
    }

    m_document->filePath = target;
    m_document->setModified(false);

    m_io->addRecentFile(target);
    refreshRecentFiles();
    //  The saved state is now the truth, so a stale crash-recovery file would
    //  only offer the user older work.
    //  已保存的状态才是真相，因此过期的崩溃恢复文件只会向用户提供更旧的内容。
    m_io->discardAutosave(target);
    m_settings.setLastProjectDirectory(QFileInfo(target).absolutePath());

    emit projectChanged();
    emit modifiedChanged(false);
    setStatusMessage(QStringLiteral("Saved %1 / 已保存 %1").arg(target));
    return true;
}

void AppController::closeProject()
{
    if (!m_document)
        return;

    m_commands.reset();
    m_document.reset();
    setProperty(kPageIdsProperty, QStringList());

    emit projectChanged();
    emit currentPageChanged(QString());
    emit pagesChanged();
    emit projectStructureChanged();
    emit modifiedChanged(false);
    emit undoStackChanged();
    setStatusMessage(QStringLiteral("Project closed. / 工程已关闭。"));
}

bool AppController::hasRecoverableAutosave() const
{
    if (!m_io || !m_document || m_document->filePath.isEmpty())
        return false;
    return m_io->hasRecoverableAutosave(m_document->filePath);
}

bool AppController::recoverAutosave()
{
    if (!m_io || !m_document) {
        reportError(noProjectMessage());
        return false;
    }
    const QString path = m_document->filePath;
    if (path.isEmpty()) {
        reportError(QStringLiteral(
            "This project has never been saved, so it has no recovery file. / "
            "此工程从未保存过，因此没有恢复文件。"));
        return false;
    }

    QString error;
    std::unique_ptr<ProjectDocument> recovered = m_io->recoverAutosave(path, &error);
    if (!recovered) {
        reportError(error.isEmpty()
                        ? QStringLiteral("The recovery file could not be read. / "
                                         "无法读取恢复文件。")
                        : error);
        return false;
    }

    installDocument(m_document, m_commands, std::move(recovered), this);
    wireDocument();
    m_document->filePath = path;
    m_document->activeBreakpoint = m_breakpoint;
    m_document->activePageId = pickCurrentPageId(m_document.get());
    //  Recovered work is by definition unsaved work.
    //  恢复出来的内容按定义就是尚未保存的内容。
    m_document->setModified(true);

    m_io->enableAutosave(m_settings.autoSave(), m_settings.autoSaveIntervalSeconds());

    setProperty(kPageIdsProperty, pageIdList(m_document.get()));
    emit projectChanged();
    emit currentPageChanged(m_document->activePageId);
    emit pagesChanged();
    emit projectStructureChanged();
    emit modifiedChanged(true);
    emit undoStackChanged();
    setStatusMessage(QStringLiteral("Recovered the autosave of %1 / 已恢复 %1 的自动保存")
                         .arg(path));
    return true;
}

void AppController::discardAutosave()
{
    if (!m_io || !m_document || m_document->filePath.isEmpty())
        return;
    m_io->discardAutosave(m_document->filePath);
}

// ---------------------------------------------------------------------------
//  Page operations -- every one of them is undoable
//  页面操作 —— 每一个都可撤销
// ---------------------------------------------------------------------------
QString AppController::addPage(const QString &name)
{
    if (!m_document || !m_commands) {
        reportError(noProjectMessage());
        return QString();
    }

    QString pageName = name.trimmed();
    if (pageName.isEmpty())
        pageName = QStringLiteral("page-%1").arg(m_document->pageCount() + 1);

    //  Layer labels are used as tab captions, so they are kept unique.
    //  图层标签会被用作标签页标题，因此必须保持唯一。
    if (m_document->pageByName(pageName)) {
        int suffix = 2;
        QString candidate = QStringLiteral("%1-%2").arg(pageName).arg(suffix);
        while (m_document->pageByName(candidate)) {
            ++suffix;
            candidate = QStringLiteral("%1-%2").arg(pageName).arg(suffix);
        }
        pageName = candidate;
    }

    std::unique_ptr<Page> page = Page::makeDefault(pageName, chinese());
    if (!page) {
        reportError(QStringLiteral("The page could not be created. / 无法创建页面。"));
        return QString();
    }
    if (page->id.isEmpty())
        page->id = IdGenerator::pageId();

    //  The id is captured before the command is handed to the stack: the stack
    //  owns (and may delete) the command the moment it is pushed.
    //  在把命令交给撤销栈之前先取出 id：命令一旦被压入，所有权就归栈所有
    //  （并可能被删除）。
    const QString newId = page->id;
    m_commands->pushIfEffective(new AddPageCommand(m_document.get(), std::move(page), -1));
    onDocumentStructureChanged();
    setStatusMessage(QStringLiteral("Added page %1 / 已添加页面 %1").arg(pageName));
    return newId;
}

bool AppController::removePage(const QString &pageId)
{
    if (!m_document || !m_commands) {
        reportError(noProjectMessage());
        return false;
    }
    const Page *page = m_document->pageById(pageId);
    if (!page) {
        reportError(QStringLiteral("No page with that id exists. / 不存在具有该 id 的页面。"));
        return false;
    }
    if (m_document->pageCount() <= 1) {
        reportError(QStringLiteral("A project must keep at least one page. / "
                                   "工程必须至少保留一个页面。"));
        return false;
    }
    const int index = m_document->indexOfPage(page);
    if (index < 0)
        return false;

    //  The label is captured before the command is pushed: the command owns the
    //  page from that moment on, and the stack may even delete the command.
    //  在压入命令之前先取出名称：从那一刻起页面归命令所有，而栈甚至可能删除该命令。
    const QString removedName = page->name;

    m_commands->pushIfEffective(new RemovePageCommand(m_document.get(), index));

    //  Removing the active page moves the editor to whichever page survives.
    //  删除活动页面后，编辑器切换到仍然存在的页面。
    if (!m_document->pageById(m_document->activePageId)) {
        m_document->activePageId = pickCurrentPageId(m_document.get());
        emit currentPageChanged(m_document->activePageId);
    }
    onDocumentStructureChanged();
    setStatusMessage(QStringLiteral("Removed page %1 / 已删除页面 %1").arg(removedName));
    return true;
}

bool AppController::renamePage(const QString &pageId, const QString &newName)
{
    if (!m_document || !m_commands) {
        reportError(noProjectMessage());
        return false;
    }
    const Page *page = m_document->pageById(pageId);
    if (!page) {
        reportError(QStringLiteral("No page with that id exists. / 不存在具有该 id 的页面。"));
        return false;
    }
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        reportError(QStringLiteral("A page name cannot be empty. / 页面名称不能为空。"));
        return false;
    }
    if (page->name == trimmed)
        return true;

    //  Keep the tab captions unambiguous, but never block the user: a clash
    //  gets a numeric suffix.
    //  保持标签页标题无歧义，但不阻塞用户：冲突时追加数字后缀。
    QString unique = trimmed;
    if (const Page *clash = m_document->pageByName(unique); clash && clash->id != pageId) {
        int suffix = 2;
        QString candidate = QStringLiteral("%1-%2").arg(trimmed).arg(suffix);
        while (const Page *other = m_document->pageByName(candidate)) {
            if (other->id == pageId)
                break;
            ++suffix;
            candidate = QStringLiteral("%1-%2").arg(trimmed).arg(suffix);
        }
        unique = candidate;
    }

    m_commands->pushIfEffective(new SetPageMetaCommand(
        m_document.get(), pageId, SetPageMetaCommand::Name, unique));
    //  The id list did not change, so the page list is announced explicitly:
    //  its captions did change.
    //  id 列表没有变化，因此这里显式通告页面列表：它的标题确实变了。
    emit pagesChanged();
    onDocumentStructureChanged();
    return true;
}

bool AppController::setPageTitle(const QString &pageId, const QString &title)
{
    if (!m_document || !m_commands) {
        reportError(noProjectMessage());
        return false;
    }
    const Page *page = m_document->pageById(pageId);
    if (!page) {
        reportError(QStringLiteral("No page with that id exists. / 不存在具有该 id 的页面。"));
        return false;
    }
    if (page->title == title)
        return true;

    m_commands->pushIfEffective(new SetPageMetaCommand(
        m_document.get(), pageId, SetPageMetaCommand::Title, title));
    emit pagesChanged();
    onDocumentStructureChanged();
    return true;
}

bool AppController::setPageMeta(const QString &pageId, const QString &field, const QString &value)
{
    if (!m_document || !m_commands) {
        reportError(noProjectMessage());
        return false;
    }
    const Page *page = m_document->pageById(pageId);
    if (!page) {
        reportError(QStringLiteral("No page with that id exists. / 不存在具有该 id 的页面。"));
        return false;
    }

    SetPageMetaCommand::Field target = SetPageMetaCommand::Title;
    const QString key = field.trimmed().toLower();
    if (key == QLatin1String("name"))
        target = SetPageMetaCommand::Name;
    else if (key == QLatin1String("filename") || key == QLatin1String("file"))
        target = SetPageMetaCommand::FileName;
    else if (key == QLatin1String("title"))
        target = SetPageMetaCommand::Title;
    else if (key == QLatin1String("description"))
        target = SetPageMetaCommand::Description;
    else if (key == QLatin1String("language") || key == QLatin1String("lang"))
        target = SetPageMetaCommand::Language;
    else if (key == QLatin1String("author"))
        target = SetPageMetaCommand::Author;
    else if (key == QLatin1String("ishome") || key == QLatin1String("home"))
        target = SetPageMetaCommand::IsHome;
    else {
        reportError(QStringLiteral("Unknown page field \"%1\". / 未知的页面字段 \"%1\"。")
                        .arg(field));
        return false;
    }

    m_commands->pushIfEffective(
        new SetPageMetaCommand(m_document.get(), pageId, target, value));
    emit pagesChanged();
    onDocumentStructureChanged();
    return true;
}

QVariantMap AppController::pageMeta(const QString &pageId) const
{
    QVariantMap map;
    if (!m_document)
        return map;
    const Page *page = m_document->pageById(pageId);
    if (!page)
        return map;
    map.insert(QStringLiteral("id"), page->id);
    map.insert(QStringLiteral("name"), page->name);
    map.insert(QStringLiteral("fileName"), page->fileName);
    map.insert(QStringLiteral("title"), page->title);
    map.insert(QStringLiteral("description"), page->description);
    map.insert(QStringLiteral("language"), page->language);
    map.insert(QStringLiteral("author"), page->author);
    map.insert(QStringLiteral("isHome"), page->isHome);
    return map;
}

// ---------------------------------------------------------------------------
//  Export
//  导出
// ---------------------------------------------------------------------------
QString AppController::exportToDirectory(const QString &directory, const QString &mode)
{
    if (!m_document) {
        reportError(noProjectMessage());
        return QString();
    }
    if (directory.trimmed().isEmpty()) {
        reportError(QStringLiteral("No export directory was given. / 未提供导出目录。"));
        return QString();
    }

    //  The requested mode wins; an unparsable (or absent) mode falls back to the
    //  mode the user last selected in the shell.
    //  请求的模式优先；无法解析（或缺失）的模式回退到用户在界面中最后选择的模式。
    const ExportMode chosen = mode.trimmed().isEmpty()
                                  ? m_exportMode
                                  : exportModeFromString(mode, m_exportMode);

    const QString target = QDir::fromNativeSeparators(directory.trimmed());
    if (!QDir().mkpath(target)) {
        reportError(QStringLiteral("The export directory could not be created: %1 / "
                                   "无法创建导出目录：%1")
                        .arg(target));
        return QString();
    }

    const CodeGenerator generator(emitOptionsFor(m_settings, *m_document, chosen));
    const EmitResult result = generator.generateProject(*m_document);
    if (!result.ok) {
        reportError(emitErrorText(result));
        return QString();
    }

    QStringList writeErrors;
    if (!CodeGenerator::writeToDirectory(result, target, &writeErrors)) {
        reportError(writeErrors.isEmpty()
                        ? QStringLiteral("The exported files could not be written to %1 / "
                                         "无法把导出的文件写入 %1")
                              .arg(target)
                        : writeErrors.join(QStringLiteral("\n")));
        return QString();
    }

    m_settings.setLastExportDirectory(target);
    const QString summary = CodeGenerator::summarise(result, chinese());
    setStatusMessage(summary);
    return summary;
}

QString AppController::exportToDialog(const QString &mode)
{
    //  Same shim as openProjectDialog(): QML owns the folder picker. See the long
    //  bilingual note there. For automated tests the target directory may be
    //  injected through TCBW_EXPORT_DIR_HOOK; otherwise the directory the user
    //  exported to last is reused.
    //  与 openProjectDialog() 相同的垫片：文件夹选择器由 QML 负责，详见那里的双语
    //  说明。自动化测试可通过 TCBW_EXPORT_DIR_HOOK 注入目标目录，否则复用用户上次
    //  导出的目录。
    const QString hooked = qEnvironmentVariable(kExportDirHook);
    if (!hooked.isEmpty())
        return exportToDirectory(hooked, mode);

    const QString remembered = m_settings.lastExportDirectory();
    if (!remembered.isEmpty())
        return exportToDirectory(remembered, mode);

    reportError(folderPickerIsOwnedByQml());
    return QString();
}

QVariantMap AppController::previewExport(const QString &mode)
{
    QVariantMap preview;
    preview.insert(QStringLiteral("ok"), false);
    preview.insert(QStringLiteral("summary"), QString());
    preview.insert(QStringLiteral("files"), QVariantList());
    preview.insert(QStringLiteral("errors"), QStringList());

    if (!m_document) {
        preview.insert(QStringLiteral("errors"),
                       QStringList{noProjectMessage()});
        return preview;
    }

    const ExportMode chosen = mode.trimmed().isEmpty()
                                  ? m_exportMode
                                  : exportModeFromString(mode, m_exportMode);
    const CodeGenerator generator(emitOptionsFor(m_settings, *m_document, chosen));
    const EmitResult result = generator.generateProject(*m_document);

    //  Nothing is written: the preview only describes what an export would do.
    //  不写入任何内容：预览只描述一次导出将会产生什么。
    QVariantList files;
    files.reserve(result.files.size());
    for (const EmittedFile &file : result.files) {
        QVariantMap entry;
        entry.insert(QStringLiteral("path"), file.relativePath);
        entry.insert(QStringLiteral("size"), file.size());
        entry.insert(QStringLiteral("mime"), file.mimeType);
        files.append(entry);
    }

    preview.insert(QStringLiteral("ok"), result.ok);
    preview.insert(QStringLiteral("summary"), CodeGenerator::summarise(result, chinese()));
    preview.insert(QStringLiteral("files"), files);
    preview.insert(QStringLiteral("errors"), result.errors);
    return preview;
}

QString AppController::suggestExportFileName() const
{
    if (!m_document)
        return QString();
    const Page *page = m_document->homePage();
    if (!page)
        page = currentPage();
    if (!page)
        return QString();
    const CodeGenerator generator(emitOptionsFor(m_settings, *m_document, m_exportMode));
    return generator.suggestedFileName(*page);
}

// ---------------------------------------------------------------------------
//  Well-known directories
//  常见目录
// ---------------------------------------------------------------------------
QString AppController::appDataPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!path.isEmpty())
        QDir().mkpath(path);
    return path;
}

QString AppController::documentsPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString AppController::tempPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}

// ---------------------------------------------------------------------------
//  Preferences
//  偏好设置
// ---------------------------------------------------------------------------
QVariantMap AppController::editorSettings() const
{
    QVariantMap map;
    map.insert(QStringLiteral("themeMode"), toString(m_settings.themeMode()));
    map.insert(QStringLiteral("language"), m_settings.language());
    map.insert(QStringLiteral("uiScale"), m_settings.uiScale());
    map.insert(QStringLiteral("reduceMotion"), m_settings.reduceMotion());
    map.insert(QStringLiteral("showGrid"), m_settings.showGrid());
    map.insert(QStringLiteral("gridSize"), m_settings.gridSize());
    map.insert(QStringLiteral("snapToGrid"), m_settings.snapToGrid());
    map.insert(QStringLiteral("snapToGuides"), m_settings.snapToGuides());
    map.insert(QStringLiteral("showRulers"), m_settings.showRulers());
    map.insert(QStringLiteral("autoSave"), m_settings.autoSave());
    map.insert(QStringLiteral("autoSaveIntervalSeconds"), m_settings.autoSaveIntervalSeconds());
    map.insert(QStringLiteral("defaultBreakpoint"), toString(m_settings.defaultBreakpoint()));
    map.insert(QStringLiteral("defaultExportMode"), toString(m_settings.defaultExportMode()));
    map.insert(QStringLiteral("prettyPrint"), m_settings.prettyPrint());
    map.insert(QStringLiteral("indentWidth"), m_settings.indentWidth());
    map.insert(QStringLiteral("includeResetCss"), m_settings.includeResetCss());
    map.insert(QStringLiteral("openAfterExport"), m_settings.openAfterExport());
    map.insert(QStringLiteral("previewServerEnabled"), m_settings.previewServerEnabled());
    map.insert(QStringLiteral("previewReloadOnChange"), m_settings.previewReloadOnChange());
    map.insert(QStringLiteral("sendProjectOutlineToModel"),
               m_settings.sendProjectOutlineToModel());
    map.insert(QStringLiteral("redactSecretsInTranscripts"),
               m_settings.redactSecretsInTranscripts());
    return map;
}

void AppController::updateEditorSetting(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("themeMode")) {
        setThemeMode(value.toString());
    } else if (key == QLatin1String("language")) {
        setLanguage(value.toString());
    } else if (key == QLatin1String("uiScale")) {
        m_settings.setUiScale(value.toDouble());
    } else if (key == QLatin1String("reduceMotion")) {
        m_settings.setReduceMotion(value.toBool());
    } else if (key == QLatin1String("showGrid")) {
        m_settings.setShowGrid(value.toBool());
    } else if (key == QLatin1String("gridSize")) {
        m_settings.setGridSize(value.toInt());
    } else if (key == QLatin1String("snapToGrid")) {
        m_settings.setSnapToGrid(value.toBool());
    } else if (key == QLatin1String("snapToGuides")) {
        m_settings.setSnapToGuides(value.toBool());
    } else if (key == QLatin1String("showRulers")) {
        m_settings.setShowRulers(value.toBool());
    } else if (key == QLatin1String("autoSave")) {
        m_settings.setAutoSave(value.toBool());
        if (m_io)
            m_io->enableAutosave(m_settings.autoSave(), m_settings.autoSaveIntervalSeconds());
    } else if (key == QLatin1String("autoSaveIntervalSeconds")) {
        m_settings.setAutoSaveIntervalSeconds(value.toInt());
        if (m_io)
            m_io->enableAutosave(m_settings.autoSave(), m_settings.autoSaveIntervalSeconds());
    } else if (key == QLatin1String("defaultBreakpoint")) {
        m_settings.setDefaultBreakpoint(
            breakpointFromString(value.toString(), m_settings.defaultBreakpoint()));
    } else if (key == QLatin1String("defaultExportMode")) {
        m_settings.setDefaultExportMode(
            exportModeFromString(value.toString(), m_settings.defaultExportMode()));
    } else if (key == QLatin1String("prettyPrint")) {
        m_settings.setPrettyPrint(value.toBool());
    } else if (key == QLatin1String("indentWidth")) {
        m_settings.setIndentWidth(value.toInt());
    } else if (key == QLatin1String("includeResetCss")) {
        m_settings.setIncludeResetCss(value.toBool());
    } else if (key == QLatin1String("openAfterExport")) {
        m_settings.setOpenAfterExport(value.toBool());
    } else if (key == QLatin1String("previewServerEnabled")) {
        m_settings.setPreviewServerEnabled(value.toBool());
    } else if (key == QLatin1String("previewReloadOnChange")) {
        m_settings.setPreviewReloadOnChange(value.toBool());
    } else if (key == QLatin1String("sendProjectOutlineToModel")) {
        m_settings.setSendProjectOutlineToModel(value.toBool());
    } else if (key == QLatin1String("redactSecretsInTranscripts")) {
        m_settings.setRedactSecretsInTranscripts(value.toBool());
    }
    //  Unknown keys are ignored on purpose: the QML settings page may be newer
    //  than this build and an unknown preference must never be fatal.
    //  未知键被刻意忽略：QML 设置界面可能比本构建更新，未知偏好绝不能导致致命错误。
}

// ---------------------------------------------------------------------------
//  Shell integration helpers
//  外壳集成辅助函数
// ---------------------------------------------------------------------------
void AppController::openExternalUrl(const QString &url)
{
    const QUrl target(url.trimmed());
    const QString scheme = target.scheme().toLower();

    //  Only the three schemes the product legitimately needs are accepted. This
    //  is the guard that keeps a document (or the AI) from launching
    //  `javascript:`, `file://`, `data:` and friends through the desktop shell.
    //  只接受本产品合理需要的三种协议。这道防线阻止文档（或 AI）借操作系统外壳
    //  启动 `javascript:`、`file://`、`data:` 等协议。
    const bool allowed = (scheme == QLatin1String("http") || scheme == QLatin1String("https")
                          || scheme == QLatin1String("mailto"));
    if (!allowed) {
        reportError(QStringLiteral(
                        "Refused to open \"%1\": only http, https and mailto links are allowed. "
                        "/ 拒绝打开 \"%1\"：只允许 http、https 与 mailto 链接。")
                        .arg(url));
        return;
    }

    if (!QDesktopServices::openUrl(target))
        reportError(QStringLiteral("The system could not open \"%1\". / 系统无法打开 \"%1\"。")
                        .arg(url));
}

void AppController::revealInFileManager(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        reportError(QStringLiteral("No path was given. / 未提供路径。"));
        return;
    }

    const QFileInfo info(path.trimmed());
    if (!info.exists()) {
        reportError(QStringLiteral("\"%1\" does not exist. / \"%1\" 不存在。").arg(path));
        return;
    }

    //  All platforms are served by asking the desktop to open the containing
    //  folder; on Linux this depends on the desktop environment providing a
    //  handler for directories (xdg-open).
    //  所有平台都通过请桌面环境打开所在文件夹来实现；在 Linux 上这取决于桌面环境
    //  是否为目录提供了处理程序（xdg-open）。
    const QString parentDir = info.absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(parentDir)))
        reportError(QStringLiteral("The file manager could not be opened for \"%1\". / "
                                   "无法为 \"%1\" 打开文件管理器。")
                        .arg(parentDir));
}

void AppController::copyToClipboard(const QString &text)
{
    if (!QCoreApplication::instance())
        return;
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

QString AppController::readFromClipboard() const
{
    if (!QCoreApplication::instance())
        return QString();
    if (const QClipboard *clipboard = QGuiApplication::clipboard())
        return clipboard->text();
    return QString();
}

QString AppController::platformName() const
{
    return QSysInfo::prettyProductName();
}

QString AppController::architecture() const
{
    return QSysInfo::currentCpuArchitecture();
}

QString AppController::projectJson() const
{
    if (!m_document || !m_io)
        return QString();
    return QString::fromUtf8(m_io->toJsonBytes(*m_document));
}

// ---------------------------------------------------------------------------
//  Private slots
//  私有槽
// ---------------------------------------------------------------------------
void AppController::onDocumentStructureChanged()
{
    if (m_document) {
        //  The page list is rebuilt from scratch by the QML layer, so it is only
        //  announced when the pages (or even just their order) really changed.
        //  QML 层会完全重建页面列表，因此只有当页面（甚至只是顺序）确实变化时才通告。
        const QStringList ids = pageIdList(m_document.get());
        if (property(kPageIdsProperty).toStringList() != ids) {
            setProperty(kPageIdsProperty, ids);
            emit pagesChanged();
        }

        //  A structural change may have removed the page the editor was on.
        //  结构变化可能删除了编辑器当前所在的页面。
        if (!m_document->pageById(m_document->activePageId)) {
            const QString fallback = pickCurrentPageId(m_document.get());
            if (fallback != m_document->activePageId) {
                m_document->activePageId = fallback;
                emit currentPageChanged(fallback);
            }
        }
    }
    emit projectStructureChanged();
}

void AppController::onUndoStackChanged()
{
    emit undoStackChanged();
    if (m_document && m_commands)
        m_document->setModified(!m_commands->isClean());
}

// ---------------------------------------------------------------------------
//  Wiring
//  信号接线
// ---------------------------------------------------------------------------
void AppController::wireDocument()
{
    if (!m_document)
        return;

    //  Structural edits rebuild the trees; fine-grained edits only repaint.
    //  结构性编辑会重建各棵树；细粒度编辑只需重绘。
    connect(m_document.get(), &ProjectDocument::structureChanged, this,
            &AppController::onDocumentStructureChanged);
    connect(m_document.get(), &ProjectDocument::nodeChanged, this,
            [this](const QString &) { emit projectStructureChanged(); });
    connect(m_document.get(), &ProjectDocument::pageChanged, this,
            [this](const QString &) { emit projectStructureChanged(); });
    connect(m_document.get(), &ProjectDocument::modifiedChanged, this,
            [this](bool value) { emit modifiedChanged(value); });

    //  The undo stack of the new document is wired here as well, so that
    //  installing a document is a single call for every caller.
    //  新文档的撤销栈也在这里接线，使"安装文档"对每个调用方都只需一次调用。
    if (m_commands) {
        connect(m_commands.get(), &QUndoStack::indexChanged, this,
                &AppController::onUndoStackChanged);
        connect(m_commands.get(), &QUndoStack::cleanChanged, this,
                &AppController::onUndoStackChanged);
    }
}

} // namespace tcbw
