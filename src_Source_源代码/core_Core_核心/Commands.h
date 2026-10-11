// ============================================================================
//  Commands.h -- undoable editing operations
//  Commands.h —— 可撤销的编辑操作
//
//  Every structural edit in the application is a QUndoCommand. This gives the
//  user unlimited undo/redo for free, gives the AI layer a transactional
//  boundary (one tool call == one command), and makes the document model
//  testable without a UI.
//  应用程序中的每一次结构性编辑都是一个 QUndoCommand。这免费带来无限次撤销/重做，
//  为 AI 层提供事务边界（一次工具调用等于一个命令），并让文档模型无需界面即可测试。
// ============================================================================
#pragma once

#include "BlockDef.h"
#include "DocumentNode.h"
#include "Page.h"
#include "ProjectDocument.h"
#include "StyleSheet.h"
#include "Types.h"

#include <QString>
#include <QUndoCommand>
#include <QUndoStack>

#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  DocumentCommand
//  Base class carrying the target document and the user-visible action label.
//  基类，携带目标文档与用户可见的操作名称。
// ---------------------------------------------------------------------------
class DocumentCommand : public QUndoCommand
{
public:
    DocumentCommand(ProjectDocument *document, const QString &textEn,
                    const QString &textZh);
    ~DocumentCommand() override;

    //  Localised label for the Edit menu / 编辑菜单使用的本地化名称。
    QString labelZh() const { return m_textZh; }

    ProjectDocument *document() const { return m_document; }

protected:
    //  Resolves a node by id across the active page (or a named page).
    //  在活动页面（或指定页面）中按 id 解析节点。
    Page *pageById(const QString &pageId) const;
    DocumentNode *nodeById(const QString &pageId, const QString &nodeId) const;

    ProjectDocument *m_document = nullptr;
    QString m_textZh;
};

// ---------------------------------------------------------------------------
//  Node commands
//  节点命令
// ---------------------------------------------------------------------------
class AddNodeCommand : public DocumentCommand
{
public:
    AddNodeCommand(ProjectDocument *document, const QString &pageId,
                   const QString &parentNodeId, int index,
                   std::unique_ptr<DocumentNode> node);
    ~AddNodeCommand() override;

    void undo() override;
    void redo() override;

    QString createdNodeId() const;

private:
    QString m_pageId;
    QString m_parentNodeId;
    int m_index;
    std::unique_ptr<DocumentNode> m_node;
    QString m_createdId;
    bool m_firstRun = true;
};

class RemoveNodeCommand : public DocumentCommand
{
public:
    RemoveNodeCommand(ProjectDocument *document, const QString &pageId,
                      const QString &nodeId);
    ~RemoveNodeCommand() override;

    void undo() override;
    void redo() override;

    bool isValid() const { return m_valid; }
    QString removedParentId() const { return m_parentNodeId; }
    int removedIndex() const { return m_index; }
    QString removedNodeId() const { return m_nodeId; }

private:
    QString m_pageId;
    QString m_nodeId;
    QString m_parentNodeId;
    int m_index = -1;
    std::unique_ptr<DocumentNode> m_owned;
    bool m_valid = false;
};

class MoveNodeCommand : public DocumentCommand
{
public:
    MoveNodeCommand(ProjectDocument *document, const QString &pageId,
                    const QString &nodeId, int newIndex);
    void undo() override;
    void redo() override;

private:
    QString m_pageId;
    QString m_nodeId;
    int m_newIndex;
    int m_oldIndex = -1;
};

class ReparentNodeCommand : public DocumentCommand
{
public:
    ReparentNodeCommand(ProjectDocument *document, const QString &pageId,
                        const QString &nodeId, const QString &newParentId, int index);
    void undo() override;
    void redo() override;

private:
    QString m_pageId;
    QString m_nodeId;
    QString m_newParentId;
    QString m_oldParentId;
    int m_newIndex;
    int m_oldIndex = -1;
};

class DuplicateNodeCommand : public DocumentCommand
{
public:
    DuplicateNodeCommand(ProjectDocument *document, const QString &pageId,
                         const QString &nodeId);
    ~DuplicateNodeCommand() override;
    void undo() override;
    void redo() override;
    QString duplicateId() const { return m_duplicateId; }

private:
    QString m_pageId;
    QString m_nodeId;
    std::unique_ptr<DocumentNode> m_owned;
    QString m_duplicateId;
    bool m_firstRun = true;
};

// ---------------------------------------------------------------------------
//  Property / style commands
//  属性 / 样式命令
// ---------------------------------------------------------------------------
class SetTextCommand : public DocumentCommand
{
public:
    SetTextCommand(ProjectDocument *document, const QString &pageId,
                   const QString &nodeId, const QString &newText);
    void undo() override;
    void redo() override;
    bool isNoop() const { return m_newText == m_oldText; }

private:
    QString m_pageId;
    QString m_nodeId;
    QString m_newText;
    QString m_oldText;
};

class SetAttributeCommand : public DocumentCommand
{
public:
    SetAttributeCommand(ProjectDocument *document, const QString &pageId,
                        const QString &nodeId, const QString &key,
                        const QString &newValue);
    void undo() override;
    void redo() override;
    bool isNoop() const { return m_newValue == m_oldValue; }

private:
    QString m_pageId;
    QString m_nodeId;
    QString m_key;
    QString m_newValue;
    QString m_oldValue;
    bool m_hadOld = false;
};

class SetStyleCommand : public DocumentCommand
{
public:
    SetStyleCommand(ProjectDocument *document, const QString &pageId,
                    const QString &nodeId, Breakpoint breakpoint,
                    const QString &property, const QString &newValue);
    void undo() override;
    void redo() override;
    bool isNoop() const { return m_newValue == m_oldValue; }

private:
    QString m_pageId;
    QString m_nodeId;
    Breakpoint m_breakpoint;
    QString m_property;
    QString m_newValue;
    QString m_oldValue;
    bool m_hadOld = false;
};

class RenameNodeCommand : public DocumentCommand
{
public:
    RenameNodeCommand(ProjectDocument *document, const QString &pageId,
                      const QString &nodeId, const QString &newName);
    void undo() override;
    void redo() override;
    bool isNoop() const { return m_newName == m_oldName; }

private:
    QString m_pageId;
    QString m_nodeId;
    QString m_newName;
    QString m_oldName;
};

// ---------------------------------------------------------------------------
//  Page commands
//  页面命令
// ---------------------------------------------------------------------------
class AddPageCommand : public DocumentCommand
{
public:
    AddPageCommand(ProjectDocument *document, std::unique_ptr<Page> page, int index);
    ~AddPageCommand() override;
    void undo() override;
    void redo() override;
    QString pageId() const;

private:
    std::unique_ptr<Page> m_page;
    int m_index;
    QString m_pageId;
    bool m_firstRun = true;
};

class RemovePageCommand : public DocumentCommand
{
public:
    RemovePageCommand(ProjectDocument *document, int index);
    ~RemovePageCommand() override;
    void undo() override;
    void redo() override;
    bool isValid() const { return m_index >= 0; }

private:
    std::unique_ptr<Page> m_owned;
    int m_index = -1;
};

class SetPageMetaCommand : public DocumentCommand
{
public:
    enum Field { Name, FileName, Title, Description, Language, Author, IsHome };
    SetPageMetaCommand(ProjectDocument *document, const QString &pageId,
                       Field field, const QString &newValue);
    void undo() override;
    void redo() override;

private:
    QString m_pageId;
    Field m_field;
    QString m_newValue;
    QString m_oldValue;
};

// ---------------------------------------------------------------------------
//  Block commands
//  积木命令
// ---------------------------------------------------------------------------
class AddScriptCommand : public DocumentCommand
{
public:
    AddScriptCommand(ProjectDocument *document, std::unique_ptr<BlockScript> script,
                     int index);
    ~AddScriptCommand() override;
    void undo() override;
    void redo() override;
    QString scriptId() const;

private:
    std::unique_ptr<BlockScript> m_script;
    int m_index;
    QString m_scriptId;
    bool m_firstRun = true;
};

class RemoveScriptCommand : public DocumentCommand
{
public:
    RemoveScriptCommand(ProjectDocument *document, const QString &scriptId);
    ~RemoveScriptCommand() override;
    void undo() override;
    void redo() override;
    bool isValid() const { return m_index >= 0; }

private:
    std::unique_ptr<BlockScript> m_owned;
    QString m_scriptId;
    int m_index = -1;
};

class AddBlockCommand : public DocumentCommand
{
public:
    //  Insert into an input socket or a statement branch, or append to a
    //  script body when `socketName` is empty.
    //  插入到输入槽、语句分支；当 `socketName` 为空时追加到脚本主体。
    AddBlockCommand(ProjectDocument *document, const QString &scriptId,
                    const QString &parentBlockId, const QString &socketName,
                    bool isStatement, int index, std::unique_ptr<Block> block);
    ~AddBlockCommand() override;
    void undo() override;
    void redo() override;
    QString blockId() const;

private:
    QString m_scriptId;
    QString m_parentBlockId;
    QString m_socketName;
    bool m_isStatement;
    int m_index;
    std::unique_ptr<Block> m_block;
    QString m_blockId;
    bool m_firstRun = true;
};

class RemoveBlockCommand : public DocumentCommand
{
public:
    RemoveBlockCommand(ProjectDocument *document, const QString &scriptId,
                       const QString &blockId);
    ~RemoveBlockCommand() override;
    void undo() override;
    void redo() override;
    bool isValid() const { return m_valid; }

private:
    QString m_scriptId;
    QString m_blockId;
    QString m_parentBlockId;
    QString m_socketName;
    bool m_parentIsStatement = false;
    int m_index = -1;
    std::unique_ptr<Block> m_owned;
    bool m_valid = false;
};

class SetBlockFieldCommand : public DocumentCommand
{
public:
    SetBlockFieldCommand(ProjectDocument *document, const QString &scriptId,
                         const QString &blockId, const QString &fieldName,
                         const QString &newValue);
    void undo() override;
    void redo() override;
    bool isNoop() const { return m_newValue == m_oldValue; }

private:
    QString m_scriptId;
    QString m_blockId;
    QString m_fieldName;
    QString m_newValue;
    QString m_oldValue;
};

// ---------------------------------------------------------------------------
//  MacroCommand -- groups several edits into one undo step
//  MacroCommand —— 将多个编辑合并为一个撤销步骤
// ---------------------------------------------------------------------------
class MacroCommand : public DocumentCommand
{
public:
    MacroCommand(ProjectDocument *document, const QString &textEn, const QString &textZh);
    ~MacroCommand() override;
    void undo() override;
    void redo() override;
    void add(std::unique_ptr<QUndoCommand> command);

private:
    QVector<QUndoCommand *> m_children;
};

// ---------------------------------------------------------------------------
//  CommandStack
//  A QUndoStack with a friendlier API and a modification-tracking flag.
//  带更友好 API 与修改标记的 QUndoStack。
// ---------------------------------------------------------------------------
class CommandStack : public QUndoStack
{
    Q_OBJECT

public:
    explicit CommandStack(ProjectDocument *document, QObject *parent = nullptr);
    ~CommandStack() override;

    ProjectDocument *document() const { return m_document; }

    //  Pushes a command, ignoring it when it would be a no-op.
    //  压入命令；若为空操作则忽略。
    void pushIfEffective(QUndoCommand *command);

    //  Begins/ends a macro so several edits collapse into one undo step.
    //  开始/结束宏，使多个编辑折叠为一个撤销步骤。
    void beginMacro(const QString &textEn, const QString &textZh);
    void endMacro();

    bool isInMacro() const { return m_macroDepth > 0; }

    //  Convenience wrappers used by the controllers.
    //  控制器使用的便捷封装。
    QString addNode(const QString &pageId, const QString &parentNodeId, int index,
                    std::unique_ptr<DocumentNode> node, const QString &labelEn,
                    const QString &labelZh);
    bool removeNode(const QString &pageId, const QString &nodeId,
                    const QString &labelEn, const QString &labelZh);
    bool duplicateNode(const QString &pageId, const QString &nodeId,
                       const QString &labelEn, const QString &labelZh);
    bool setText(const QString &pageId, const QString &nodeId, const QString &text,
                 const QString &labelEn, const QString &labelZh);
    bool setAttribute(const QString &pageId, const QString &nodeId, const QString &key,
                      const QString &value, const QString &labelEn,
                      const QString &labelZh);
    bool setStyle(const QString &pageId, const QString &nodeId, Breakpoint bp,
                  const QString &property, const QString &value,
                  const QString &labelEn, const QString &labelZh);

signals:
    void effectiveChangePushed();

private:
    ProjectDocument *m_document = nullptr;
    int m_macroDepth = 0;
    QString m_macroTextZh;
};

} // namespace tcbw
