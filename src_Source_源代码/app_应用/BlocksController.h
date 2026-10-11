// ============================================================================
//  BlocksController.h -- the Scratch-style block editor behind QML
//  BlocksController.h —— 类 Scratch 积木编辑器背后的控制器
// ============================================================================
#pragma once

#include "BlockCompiler.h"
#include "BlockDef.h"
#include "BlockRegistry.h"
#include "ProjectDocument.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace tcbw {

class AppController;

// ---------------------------------------------------------------------------
//  BlocksController
//  Exposes the toolbox, the script list, the editable block tree and the
//  compiler to QML. Block edits are undoable through CommandStack.
//  向 QML 暴露工具箱、脚本列表、可编辑的积木树与编译器。积木编辑可通过
//  CommandStack 撤销。
// ---------------------------------------------------------------------------
class BlocksController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList categories READ categories NOTIFY toolboxChanged)
    Q_PROPERTY(QVariantList toolbox READ toolbox NOTIFY toolboxChanged)
    Q_PROPERTY(QVariantList scripts READ scripts NOTIFY scriptsChanged)
    Q_PROPERTY(QString selectedScriptId READ selectedScriptId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedBlockId READ selectedBlockId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedBlock READ selectedBlock NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedScriptBlocks READ selectedScriptBlocks NOTIFY scriptsChanged)
    Q_PROPERTY(QString generatedCode READ generatedCode NOTIFY codeChanged)
    Q_PROPERTY(QStringList compileErrors READ compileErrors NOTIFY codeChanged)
    Q_PROPERTY(QStringList compileWarnings READ compileWarnings NOTIFY codeChanged)
    Q_PROPERTY(bool showAdvanced READ showAdvanced WRITE setShowAdvanced NOTIFY toolboxChanged)

public:
    explicit BlocksController(AppController *app, QObject *parent = nullptr);
    ~BlocksController() override;

    void setAppController(AppController *app);

    QVariantList categories() const;
    QVariantList toolbox() const;
    QVariantList scripts() const;
    QString selectedScriptId() const { return m_selectedScriptId; }
    QString selectedBlockId() const { return m_selectedBlockId; }
    QVariantMap selectedBlock() const;
    QVariantList selectedScriptBlocks() const;
    QString generatedCode() const { return m_generatedCode; }
    QStringList compileErrors() const { return m_errors; }
    QStringList compileWarnings() const { return m_warnings; }

    bool showAdvanced() const { return m_showAdvanced; }
    void setShowAdvanced(bool value);

    // -----------------------------------------------------------------------
    //  Invokable API
    //  可调用接口
    // -----------------------------------------------------------------------
    Q_INVOKABLE QVariantList blocksInCategory(const QString &categoryKey) const;
    Q_INVOKABLE QVariantList searchBlocks(const QString &needle) const;
    Q_INVOKABLE QVariantMap blockDefinition(const QString &type) const;

    Q_INVOKABLE QString addScript(const QString &eventType = QStringLiteral("events.start"));
    Q_INVOKABLE bool removeScript(const QString &scriptId);
    Q_INVOKABLE bool renameScript(const QString &scriptId, const QString &name);
    Q_INVOKABLE bool setScriptEnabled(const QString &scriptId, bool enabled);
    Q_INVOKABLE bool setScriptPosition(const QString &scriptId, double x, double y);
    Q_INVOKABLE void selectScript(const QString &scriptId);

    //  Inserts a block. `parentBlockId` empty means "top of the script body".
    //  `socketName` selects an input socket or a statement branch.
    //  插入积木。`parentBlockId` 为空表示"脚本主体顶部"。
    //  `socketName` 选择输入槽或语句分支。
    Q_INVOKABLE QString insertBlock(const QString &type, const QString &scriptId = QString(),
                                    const QString &parentBlockId = QString(),
                                    const QString &socketName = QString(),
                                    bool isStatement = true, int index = -1);
    Q_INVOKABLE bool removeBlock(const QString &blockId);
    Q_INVOKABLE bool moveBlock(const QString &blockId, const QString &newParentBlockId,
                               const QString &socketName, bool isStatement, int index);
    Q_INVOKABLE bool setBlockField(const QString &blockId, const QString &fieldName,
                                   const QString &value);
    Q_INVOKABLE bool setBlockPosition(const QString &blockId, double x, double y);
    Q_INVOKABLE void selectBlock(const QString &blockId);
    Q_INVOKABLE QVariantMap blockById(const QString &blockId) const;
    Q_INVOKABLE QVariantList compatibleBlocks(const QString &inputName,
                                              const QString &valueKind) const;

    //  True when dropping `blockType` into (parentBlockId, socketName) is legal.
    //  当把 `blockType` 放入 (parentBlockId, socketName) 合法时返回 true。
    Q_INVOKABLE bool canConnect(const QString &blockType, const QString &parentBlockId,
                                const QString &socketName, bool isStatement) const;

    Q_INVOKABLE void recompile();
    Q_INVOKABLE QString codeForScript(const QString &scriptId) const;
    Q_INVOKABLE QVariantList validateScript(const QString &scriptId) const;
    Q_INVOKABLE QString exportProgramJavaScript() const;
    Q_INVOKABLE bool copyCodeToClipboard() const;

    //  Optional preview convenience: blocks that can be simulated without a
    //  browser report their first observable effect here.
    //  可选预览便利：无需浏览器即可模拟的积木在此报告其首个可观察效果。
    Q_INVOKABLE QVariantList dryRun(const QString &scriptId) const;

signals:
    void toolboxChanged();
    void scriptsChanged();
    void selectionChanged();
    void codeChanged();

private slots:
    void onStructureChanged();

private:
    BlockScript *script(const QString &scriptId) const;
    Block *block(const QString &blockId) const;
    QVariantMap describeBlockDef(const BlockDef &def) const;
    QVariantMap blockToVariant(const Block &b, int depth) const;
    void scheduleRecompile();

    AppController *m_app = nullptr;
    QString m_selectedScriptId;
    QString m_selectedBlockId;
    QString m_generatedCode;
    QStringList m_errors;
    QStringList m_warnings;
    bool m_showAdvanced = false;
};

} // namespace tcbw
