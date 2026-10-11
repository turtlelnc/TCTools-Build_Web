// ============================================================================
//  ProjectDocument.h -- the whole project: pages, scripts, settings
//  ProjectDocument.h —— 整个工程：页面、脚本、设置
// ============================================================================
#pragma once

#include "Page.h"
#include "Types.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace tcbw {

class BlockScript;

// ---------------------------------------------------------------------------
//  ProjectDocument
//  The in-memory model of a `.tcbw` project file. It is a QObject so that the
//  UI can react to structural changes; fine-grained edits are routed through
//  QUndoStack commands in Commands.h.
//  `.tcbw` 工程文件的内存模型。它是 QObject，便于界面响应结构变化；细粒度编辑
//  通过 Commands.h 中的 QUndoStack 命令完成。
// ---------------------------------------------------------------------------
class ProjectDocument : public QObject
{
    Q_OBJECT

public:
    explicit ProjectDocument(QObject *parent = nullptr);
    ~ProjectDocument() override;

    // --- identity ----------------------------------------------------------
    QString id;
    QString name = QStringLiteral("Untitled Project");
    QString description;
    QString author;
    QString version = QStringLiteral("0.1.0");

    // --- pages -------------------------------------------------------------
    int pageCount() const { return static_cast<int>(m_pages.size()); }
    Page *pageAt(int index) const;
    Page *pageById(const QString &pageId) const;
    Page *pageByName(const QString &name) const;
    int indexOfPage(const Page *page) const;
    Page *homePage() const;

    Page *addPage(std::unique_ptr<Page> page, int index = -1);
    Page *addPage(const QString &name, bool chineseDefaults = false);
    std::unique_ptr<Page> takePage(int index);
    bool removePage(int index);
    bool movePage(int from, int to);

    // --- block programs ----------------------------------------------------
    //  Every page owns one program; a program holds a list of scripts.
    //  每个页面拥有一个程序；一个程序包含一组脚本。
    QVector<BlockScript *> &program();
    const QVector<BlockScript *> &program() const;
    void clearProgram();
    BlockScript *scriptById(const QString &scriptId) const;
    void addScript(std::unique_ptr<BlockScript> script, int index = -1);

    // --- global settings ---------------------------------------------------
    QString defaultLanguage = QStringLiteral("en");
    QString themeCss;                        // shared CSS / 共享 CSS
    QMap<QString, QString> sharedCssVariables;
    QJsonObject backendConfig;               // see backend_Backend_后端/Endpoint.h
    QJsonObject aiConfig;                    // see ai_AI_智能/AiTypes.h

    // Indices / asset registry
    QStringList assetPaths;                  // project-relative / 工程相对路径

    // --- editor state ------------------------------------------------------
    QString activePageId;
    Breakpoint activeBreakpoint = Breakpoint::Desktop;
    QString filePath;                        // absolute .tcbw path / 绝对路径
    bool modified = false;

    // --- housekeeping ------------------------------------------------------
    void setModified(bool value = true);
    void touch();                            // marks modified + emits changed / 标记修改并发出信号

    std::unique_ptr<ProjectDocument> clone(bool keepIds = false) const;
    void clear();

    // --- serialisation -----------------------------------------------------
    QJsonObject toJson() const;
    static std::unique_ptr<ProjectDocument> fromJson(const QJsonObject &obj,
                                                    QString *error = nullptr);
    static std::unique_ptr<ProjectDocument> makeStarter(bool chineseDefaults = false);

    //  JSON schema version written into the file.
    //  写入文件的 JSON 模式版本。
    static constexpr int kSchemaVersion = 1;

signals:
    //  Emitted whenever the structure changes in a way that requires a full
    //  rebuild of the UI trees.
    //  当结构发生需要完全重建界面树的变化时发出。
    void structureChanged();
    //  Emitted for cheap, localised changes (a property or style value).
    //  用于代价很小的局部变化（属性或样式值）。
    void nodeChanged(const QString &nodeId);
    void pageChanged(const QString &pageId);
    void modifiedChanged(bool modified);

private:
    //  NOTE: this is a *length carrier*, not the owner. A Qt container cannot
    //  store a move-only type -- QList/QVector detach reallocates through the
    //  copy constructor, which std::unique_ptr deletes -- so the real owners live
    //  in ProjectDocument.cpp's address-keyed registries of heap-allocated
    //  std::vectors. This member is kept in step with that registry purely so
    //  that pageCount() stays correct, and it is released in the destructor.
    //  注意：本成员只是*长度载体*，并非持有者。Qt 容器无法存放仅可移动类型 ——
    //  QList/QVector 的 detach 会通过拷贝构造函数重新分配，而 std::unique_ptr
    //  已删除该函数 —— 因此真正的持有者位于 ProjectDocument.cpp 中以对象地址为键、
    //  值为堆上 std::vector 的登记表中。本成员仅与登记表保持同步，以使 pageCount()
    //  保持正确，并在析构函数中释放。
    QVector<Page *> m_pages;
    QVector<BlockScript *> m_program;
    int m_pageCounter = 0;
};

} // namespace tcbw
