// ============================================================================
//  AppSettings.h -- user preferences (never credentials)
//  AppSettings.h —— 用户偏好设置（绝不保存凭据）
// ============================================================================
#pragma once

#include "AiTypes.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  AppSettings
//  A typed, change-notifying view over QSettings. Credentials are excluded by
//  design: they live in SecretStore.
//  基于 QSettings 的类型化、可发出变更通知的视图。凭据被刻意排除在外：
//  它们存放在 SecretStore 中。
// ---------------------------------------------------------------------------
class AppSettings : public QObject
{
    Q_OBJECT

public:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings() override;

    static AppSettings &instance();

    // --- appearance --------------------------------------------------------
    ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);

    QString language() const;                  // "en" | "zh" / 语言
    void setLanguage(const QString &code);
    bool isChinese() const { return language().startsWith(QLatin1String("zh")); }

    double uiScale() const;
    void setUiScale(double scale);

    bool reduceMotion() const;
    void setReduceMotion(bool value);

    // --- editor ------------------------------------------------------------
    bool showGrid() const;
    void setShowGrid(bool value);
    int gridSize() const;
    void setGridSize(int size);
    bool snapToGrid() const;
    void setSnapToGrid(bool value);
    bool snapToGuides() const;
    void setSnapToGuides(bool value);
    bool showRulers() const;
    void setShowRulers(bool value);
    bool autoSave() const;
    void setAutoSave(bool value);
    int autoSaveIntervalSeconds() const;
    void setAutoSaveIntervalSeconds(int seconds);
    Breakpoint defaultBreakpoint() const;
    void setDefaultBreakpoint(Breakpoint bp);

    // --- code generation ---------------------------------------------------
    ExportMode defaultExportMode() const;
    void setDefaultExportMode(ExportMode mode);
    bool prettyPrint() const;
    void setPrettyPrint(bool value);
    int indentWidth() const;
    void setIndentWidth(int width);
    bool includeResetCss() const;
    void setIncludeResetCss(bool value);
    bool openAfterExport() const;
    void setOpenAfterExport(bool value);
    QString lastExportDirectory() const;
    void setLastExportDirectory(const QString &directory);
    QString lastProjectDirectory() const;
    void setLastProjectDirectory(const QString &directory);

    // --- AI / TI-UseAgent --------------------------------------------------
    ProviderConfig providerConfig(const QString &vendorId) const;
    void setProviderConfig(const ProviderConfig &config);
    QString lastUsedVendor() const;
    void setLastUsedVendor(const QString &vendorId);
    bool aiEnabled() const;
    void setAiEnabled(bool value);
    bool aiAutoApproveSafeTools() const;
    void setAiAutoApproveSafeTools(bool value);
    QString aiApprovalPolicy() const;          // serialised ApprovalPolicy / 序列化策略
    void setAiApprovalPolicy(const QString &policy);
    int aiMaxToolIterations() const;
    void setAiMaxToolIterations(int iterations);
    int aiMaxTranscriptMessages() const;
    void setAiMaxTranscriptMessages(int messages);
    bool aiIncludeProjectOutline() const;
    void setAiIncludeProjectOutline(bool value);

    QVector<McpServerConfig> mcpServers() const;
    void setMcpServers(const QVector<McpServerConfig> &servers);
    void addMcpServer(const McpServerConfig &server);
    void removeMcpServer(const QString &id);

    // --- privacy -----------------------------------------------------------
    bool telemetryEnabled() const;             // always false in this project
    void setTelemetryEnabled(bool value);
    bool sendProjectOutlineToModel() const;
    void setSendProjectOutlineToModel(bool value);
    bool redactSecretsInTranscripts() const;
    void setRedactSecretsInTranscripts(bool value);

    // --- preview -----------------------------------------------------------
    bool previewServerEnabled() const;
    void setPreviewServerEnabled(bool value);
    bool previewReloadOnChange() const;
    void setPreviewReloadOnChange(bool value);
    QString externalBrowserCommand() const;
    void setExternalBrowserCommand(const QString &command);

    // --- maintenance -------------------------------------------------------
    void sync();
    void resetToDefaults();
    QString settingsFilePath() const;

signals:
    void themeChanged(ThemeMode mode);
    void languageChanged(const QString &code);
    void editorSettingsChanged();
    void exportSettingsChanged();
    void aiSettingsChanged();
    void mcpServersChanged();
    void previewSettingsChanged();

private:
    QString key(const QString &suffix) const;
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tcbw
