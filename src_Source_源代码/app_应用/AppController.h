// ============================================================================
//  AppController.h -- the QML-facing application facade
//  AppController.h —— 面向 QML 的应用程序门面
// ============================================================================
#pragma once

#include "AppSettings.h"
#include "Commands.h"
#include "ProjectDocument.h"
#include "ProjectIO.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace tcbw {

class BuilderController;
class BlocksController;
class AiController;
class BackendController;

// ---------------------------------------------------------------------------
//  AppController
//  Owns the document lifecycle: new / open / save / export, the undo stack,
//  the active page and the global preferences. Everything the QML layer needs
//  to render the shell lives here; heavy editing lives in BuilderController.
//  负责文档生命周期：新建 / 打开 / 保存 / 导出、撤销栈、活动页面与全局偏好。
//  QML 层渲染外壳所需的一切都在这里；繁重的编辑工作位于 BuilderController。
// ---------------------------------------------------------------------------
class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString versionFull READ versionFull CONSTANT)
    Q_PROPERTY(QString versionChannel READ versionChannel CONSTANT)
    Q_PROPERTY(QString buildStamp READ buildStamp CONSTANT)
    Q_PROPERTY(QString generatorAttribution READ generatorAttribution CONSTANT)

    Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QString projectDisplayPath READ projectDisplayPath NOTIFY projectChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)

    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId NOTIFY currentPageChanged)
    Q_PROPERTY(QString currentPageTitle READ currentPageTitle NOTIFY currentPageChanged)
    Q_PROPERTY(QStringList pageNames READ pageNames NOTIFY pagesChanged)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)

    Q_PROPERTY(QString breakpoint READ breakpoint WRITE setBreakpoint NOTIFY breakpointChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY themeModeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool chinese READ chinese NOTIFY languageChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStackChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStackChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStackChanged)

    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)

    Q_PROPERTY(QString exportMode READ exportMode WRITE setExportMode NOTIFY exportModeChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    // --- constants ---------------------------------------------------------
    QString version() const;
    QString versionFull() const;
    QString versionChannel() const;
    QString buildStamp() const;
    QString generatorAttribution() const;

    // --- document ----------------------------------------------------------
    ProjectDocument *document() const { return m_document.get(); }
    CommandStack *commands() const { return m_commands.get(); }

    QString projectName() const;
    void setProjectName(const QString &name);
    QString projectPath() const;
    QString projectDisplayPath() const;
    bool hasProject() const { return m_document != nullptr; }
    bool modified() const;

    // --- pages -------------------------------------------------------------
    QString currentPageId() const;
    void setCurrentPageId(const QString &pageId);
    Page *currentPage() const;
    QString currentPageTitle() const;
    QStringList pageNames() const;
    QVariantList pages() const;

    // --- view state --------------------------------------------------------
    QString breakpoint() const;
    void setBreakpoint(const QString &key);
    Breakpoint activeBreakpoint() const { return m_breakpoint; }

    QString themeMode() const;
    void setThemeMode(const QString &key);
    bool darkTheme() const;

    QString language() const;
    void setLanguage(const QString &code);
    bool chinese() const;

    // --- undo --------------------------------------------------------------
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;

    // --- status ------------------------------------------------------------
    QString statusMessage() const { return m_statusMessage; }
    QString lastError() const { return m_lastError; }
    void setStatusMessage(const QString &message);
    void reportError(const QString &message, bool alsoLog = true);
    bool busy() const { return m_busy; }
    void setBusy(bool value);

    // --- recent files ------------------------------------------------------
    QStringList recentFiles() const;

    // --- export ------------------------------------------------------------
    QString exportMode() const;
    void setExportMode(const QString &key);
    ExportMode activeExportMode() const { return m_exportMode; }

    // -----------------------------------------------------------------------
    //  Invokable API used directly from QML
    //  QML 直接调用的可调用接口
    // -----------------------------------------------------------------------
    Q_INVOKABLE bool newProject(bool chineseDefaults = false);
    Q_INVOKABLE bool openProjectDialog();
    Q_INVOKABLE bool openProject(const QString &filePath);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAsDialog();
    Q_INVOKABLE bool saveProjectTo(const QString &filePath);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE bool hasRecoverableAutosave() const;
    Q_INVOKABLE bool recoverAutosave();
    Q_INVOKABLE void discardAutosave();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearUndoHistory();

    Q_INVOKABLE QString addPage(const QString &name = QString());
    Q_INVOKABLE bool removePage(const QString &pageId);
    Q_INVOKABLE bool renamePage(const QString &pageId, const QString &newName);
    Q_INVOKABLE bool setPageTitle(const QString &pageId, const QString &title);
    Q_INVOKABLE bool setPageMeta(const QString &pageId, const QString &field,
                                 const QString &value);
    Q_INVOKABLE QVariantMap pageMeta(const QString &pageId) const;

    //  Opens a native folder picker and exports the site there.
    //  打开原生文件夹选择器并把站点导出到该目录。
    Q_INVOKABLE QString exportToDirectory(const QString &directory,
                                          const QString &mode = QString());
    Q_INVOKABLE QString exportToDialog(const QString &mode = QString());
    Q_INVOKABLE QVariantMap previewExport(const QString &mode = QString());
    Q_INVOKABLE QString suggestExportFileName() const;

    //  Where crash-recovery and autosave files live.
    //  崩溃恢复与自动保存文件所在位置。
    Q_INVOKABLE QString appDataPath() const;
    Q_INVOKABLE QString documentsPath() const;
    Q_INVOKABLE QString tempPath() const;

    Q_INVOKABLE void clearRecentFiles();
    Q_INVOKABLE bool removeRecentFile(const QString &filePath);

    //  Persisted user preferences exposed as a plain map for the settings UI.
    //  以普通映射形式暴露给设置界面的持久化用户偏好。
    Q_INVOKABLE QVariantMap editorSettings() const;
    Q_INVOKABLE void updateEditorSetting(const QString &key, const QVariant &value);

    Q_INVOKABLE void openExternalUrl(const QString &url);
    Q_INVOKABLE void revealInFileManager(const QString &path);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE QString readFromClipboard() const;

    Q_INVOKABLE QString platformName() const;
    Q_INVOKABLE QString architecture() const;

    //  Serialised project, used by the AI tools and by "copy project JSON".
    //  序列化后的工程，供 AI 工具与"复制工程 JSON"使用。
    Q_INVOKABLE QString projectJson() const;

signals:
    void projectChanged();
    void pagesChanged();
    void currentPageChanged(const QString &pageId);
    void modifiedChanged(bool modified);
    void breakpointChanged(const QString &key);
    void themeModeChanged();
    void languageChanged();
    void undoStackChanged();
    void statusMessageChanged();
    void lastErrorChanged();
    void busyChanged();
    void recentFilesChanged();
    void exportModeChanged();
    void projectStructureChanged();

private slots:
    void onDocumentStructureChanged();
    void onUndoStackChanged();

private:
    void wireDocument();
    void applyExportModeToGenerator();
    void refreshRecentFiles();

    std::unique_ptr<ProjectDocument> m_document;
    std::unique_ptr<CommandStack> m_commands;
    std::unique_ptr<ProjectIO> m_io;
    AppSettings &m_settings;

    Breakpoint m_breakpoint = Breakpoint::Desktop;
    ExportMode m_exportMode = ExportMode::SingleHtml;
    QString m_statusMessage;
    QString m_lastError;
    QStringList m_recentFiles;
    bool m_busy = false;
};

} // namespace tcbw
