// ============================================================================
//  ProjectIO.h -- reading, writing, autosaving and recent-file tracking
//  ProjectIO.h —— 读取、写入、自动保存与最近文件记录
// ============================================================================
#pragma once

#include "ProjectDocument.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  ProjectIO
//  Every filesystem interaction for `.tcbw` project files. It is deliberately
//  strict: it refuses to load a project that fails validation, refuses to
//  overwrite an unrelated file, and writes atomically through a temp file plus
//  rename so a crash can never truncate the user's work.
//  `.tcbw` 工程文件的全部文件系统交互。它刻意保持严格：拒绝加载未通过校验的
//  工程，拒绝覆盖无关文件，并通过临时文件加改名的方式原子写入，使崩溃永远不会
//  截断用户的工作成果。
// ---------------------------------------------------------------------------
class ProjectIO : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIO(QObject *parent = nullptr);
    ~ProjectIO() override;

    // --- constants ---------------------------------------------------------
    static QString fileExtension() { return QStringLiteral("tcbw"); }
    static QString fileFilter(bool chinese);
    static QByteArray fileMagic();       // leading bytes of a valid file / 有效文件的前导字节
    static constexpr int kMaxProjectBytes = 64 * 1024 * 1024;

    // --- loading -----------------------------------------------------------
    //  Reads and validates a project. `error` receives a localised message.
    //  读取并校验工程。`error` 接收本地化消息。
    std::unique_ptr<ProjectDocument> load(const QString &filePath, QString *error = nullptr);

    //  Parses a JSON document (used by tests and by "paste JSON" import).
    //  解析 JSON 文档（供测试与"粘贴 JSON"导入使用）。
    static std::unique_ptr<ProjectDocument> fromJson(const QByteArray &json,
                                                    QString *error = nullptr);

    // --- saving ------------------------------------------------------------
    bool save(const ProjectDocument &project, const QString &filePath,
              QString *error = nullptr);
    bool saveAs(const ProjectDocument &project, const QString &directory,
                const QString &baseName, QString *error = nullptr);

    QByteArray toJsonBytes(const ProjectDocument &project) const;

    // --- autosave / crash recovery ----------------------------------------
    void enableAutosave(bool enabled, int intervalSeconds = 120);
    bool isAutosaveEnabled() const { return m_autosaveEnabled; }
    QString autosavePathFor(const QString &filePath) const;
    bool writeAutosave(const ProjectDocument &project, QString *error = nullptr);
    bool hasRecoverableAutosave(const QString &filePath) const;
    std::unique_ptr<ProjectDocument> recoverAutosave(const QString &filePath,
                                                    QString *error = nullptr);
    void discardAutosave(const QString &filePath);

    // --- recent files ------------------------------------------------------
    QStringList recentFiles() const;
    void addRecentFile(const QString &filePath);
    void removeRecentFile(const QString &filePath);
    void clearRecentFiles();
    static constexpr int kMaxRecentFiles = 12;

    // --- safety helpers (unit-testable) ------------------------------------
    //  True when `path` has the `.tcbw` extension (case-insensitive).
    //  当 `path` 具有 `.tcbw` 扩展名（不区分大小写）时返回 true。
    static bool hasProjectExtension(const QString &path);

    //  Validates a JSON payload against the project schema version.
    //  按工程模式版本校验 JSON 载荷。
    static bool validateSchema(const QJsonObject &root, QString *error);

    //  Checks that the magic/version header of a raw file is acceptable.
    //  检查原始文件的魔数/版本头是否可接受。
    static bool validateHeader(const QByteArray &raw, QString *error);

signals:
    void loadStarted(const QString &filePath);
    void loadFinished(const QString &filePath, bool ok, const QString &error);
    void saveFinished(const QString &filePath, bool ok, const QString &error);
    void autosaveWritten(const QString &path);

private:
    static QJsonObject wrapForDisk(const ProjectDocument &project);
    QString tempPathFor(const QString &filePath) const;
    bool atomicWrite(const QString &filePath, const QByteArray &data, QString *error);

    bool m_autosaveEnabled = false;
    int m_autosaveIntervalSeconds = 120;
};

} // namespace tcbw
