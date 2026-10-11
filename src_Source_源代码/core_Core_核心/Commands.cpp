// ============================================================================
//  Commands.cpp -- undoable editing operations
//  Commands.cpp —— 可撤销的编辑操作
//
//  Design notes a reviewer should keep in mind:
//  评审时需要留意的设计要点：
//
//  1. Every command resolves its targets *by id* on each redo()/undo(). A
//     competing edit (another command, an AI tool call, a page reload) may have
//     rebuilt the tree in between, so a cached DocumentNode * would dangle.
//     每个命令在每次 redo()/undo() 时都*按 id* 解析目标。其间可能有竞争编辑
//     （另一个命令、一次 AI 工具调用、一次页面重载）重建了树，因此缓存
//     DocumentNode * 会变成悬垂指针。
//  2. Commands that insert data are idempotent under repeated redo(): the header's
//     `m_firstRun` flag records that the entity has already been created.
//     插入数据的命令在重复 redo 时是幂等的：头文件的 `m_firstRun` 标记记录实体
//     已创建。
//  3. When a target cannot be resolved a command degrades to a no-op: nothing
//     crashes and nothing is silently corrupted.
//     当目标无法解析时，命令降级为空操作：既不崩溃，也不会静默损坏数据。
//  4. `ProjectDocument` has no public removeScript(). AddScriptCommand::undo()
//     therefore leaves the script in the document and records that fact, so a
//     following redo() is still idempotent instead of duplicating it. See the
//     note on AddScriptCommand::undo() below.
//     `ProjectDocument` 没有公开的 removeScript()。因此 AddScriptCommand::undo()
//     会让脚本留在文档中并记录该事实，使随后的 redo() 仍然幂等，而不会产生副本。
//     详见下方 AddScriptCommand::undo() 的说明。
// ============================================================================
#include "Commands.h"

#include "AppSettings.h"
#include "IdGenerator.h"

#include <QJsonObject>
#include <QStringList>

#include <algorithm>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Localisation helper
//  本地化辅助
// ---------------------------------------------------------------------------

//  Picks the label the user should see right now.
//  选择当前应显示给用户的名称。
QString localise(const QString &textEn, const QString &textZh)
{
    return AppSettings::instance().isChinese() ? textZh : textEn;
}

// ---------------------------------------------------------------------------
//  Block lookup helpers
//  积木查找辅助
// ---------------------------------------------------------------------------

Block *findBlockById(BlockScript *script, const QString &blockId)
{
    if (!script || blockId.isEmpty())
        return nullptr;
    if (script->hat && script->hat->id == blockId)
        return script->hat.get();
    Block *hit = nullptr;
    script->forEach([&](Block *block) {
        if (!hit && block && block != script->hat.get() && block->id == blockId)
            hit = block;
    });
    return hit;
}

//  Locates the block that currently holds `blockId` in one of its sockets.
//  定位当前在某个槽中持有 `blockId` 的积木。
Block *findOwningBlock(BlockScript *script, const QString &blockId)
{
    if (!script || blockId.isEmpty())
        return nullptr;
    Block *owner = nullptr;
    script->forEach([&](Block *block) {
        if (owner || !block)
            return;
        for (auto it = block->inputs.constBegin(); it != block->inputs.constEnd(); ++it) {
            for (Block *child : it.value()) {
                if (child && child->id == blockId) {
                    owner = block;
                    return;
                }
            }
        }
        for (auto it = block->statements.constBegin(); it != block->statements.constEnd();
             ++it) {
            for (Block *child : it.value()) {
                if (child && child->id == blockId) {
                    owner = block;
                    return;
                }
            }
        }
    });
    return owner;
}

//  Recorded location of a block inside its script, used by the block commands.
//  积木在脚本中的记录位置，供积木命令使用。
struct BlockLocation
{
    bool valid = false;
    bool inStatement = false;
    QString parentBlockId;     // empty when the block sits at the script top level
                               // 积木位于脚本顶层时为空
    QString socketName;
    int index = -1;
};

BlockLocation locateBlock(BlockScript *script, const QString &blockId)
{
    BlockLocation location;
    if (!script || blockId.isEmpty())
        return location;

    for (int i = 0; i < script->body.size(); ++i) {
        Block *candidate = script->body.at(i);
        if (candidate && candidate->id == blockId) {
            location.valid = true;
            location.inStatement = true;
            location.index = i;
            return location;
        }
    }

    Block *owner = findOwningBlock(script, blockId);
    if (!owner)
        return location;

    for (auto it = owner->inputs.constBegin(); it != owner->inputs.constEnd(); ++it) {
        const QVector<Block *> &list = it.value();
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i) && list.at(i)->id == blockId) {
                location.valid = true;
                location.inStatement = false;
                location.parentBlockId = owner->id;
                location.socketName = it.key();
                location.index = i;
                return location;
            }
        }
    }
    for (auto it = owner->statements.constBegin(); it != owner->statements.constEnd();
         ++it) {
        const QVector<Block *> &list = it.value();
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i) && list.at(i)->id == blockId) {
                location.valid = true;
                location.inStatement = true;
                location.parentBlockId = owner->id;
                location.socketName = it.key();
                location.index = i;
                return location;
            }
        }
    }
    return location;
}

//  Detaches the block from its current holder and hands ownership to the caller.
//  把积木从当前持有者上摘下，并把所有权交给调用方。
std::unique_ptr<Block> detachBlock(BlockScript *script, const BlockLocation &location,
                                   const QString &blockId)
{
    if (!script || !location.valid)
        return nullptr;

    if (location.parentBlockId.isEmpty()) {
        //  Top-level statement of the script body.
        //  脚本主体中的顶层语句。
        for (int i = 0; i < script->body.size(); ++i) {
            Block *candidate = script->body.at(i);
            if (!candidate || candidate->id != blockId)
                continue;
            std::unique_ptr<Block> owned = script->takeBlock(i);
            if (owned) {
                owned->parent = nullptr;
                owned->parentInput.clear();
                owned->parentIsStatement = false;
            }
            return owned;
        }
        return nullptr;
    }

    Block *owner = findBlockById(script, location.parentBlockId);
    if (!owner)
        return nullptr;

    QVector<Block *> &list = location.inStatement ? owner->statements[location.socketName]
                                                  : owner->inputs[location.socketName];
    for (int i = 0; i < list.size(); ++i) {
        Block *candidate = list.at(i);
        if (!candidate || candidate->id != blockId)
            continue;
        list.removeAt(i);
        std::unique_ptr<Block> owned(candidate);
        owned->parent = nullptr;
        owned->parentInput.clear();
        owned->parentIsStatement = false;
        return owned;
    }
    return nullptr;
}

QString pageIdOrActive(const ProjectDocument *document, const QString &pageId)
{
    if (!pageId.isEmpty())
        return pageId;
    if (!document)
        return QString();
    return document->activePageId;
}

} // namespace

// ===========================================================================
//  DocumentCommand
//  文档命令基类
// ===========================================================================
DocumentCommand::DocumentCommand(ProjectDocument *document, const QString &textEn,
                                 const QString &textZh)
    : QUndoCommand(localise(textEn, textZh))
    , m_document(document)
    , m_textZh(textZh)
{
}

DocumentCommand::~DocumentCommand() = default;

Page *DocumentCommand::pageById(const QString &pageId) const
{
    if (!m_document)
        return nullptr;
    const QString id = pageIdOrActive(m_document, pageId);
    if (id.isEmpty())
        return nullptr;
    return m_document->pageById(id);
}

DocumentNode *DocumentCommand::nodeById(const QString &pageId, const QString &nodeId) const
{
    Page *page = pageById(pageId);
    if (!page || nodeId.isEmpty())
        return nullptr;
    return page->findNode(nodeId);
}

// ===========================================================================
//  AddNodeCommand
//  添加节点命令
// ===========================================================================
AddNodeCommand::AddNodeCommand(ProjectDocument *document, const QString &pageId,
                               const QString &parentNodeId, int index,
                               std::unique_ptr<DocumentNode> node)
    : DocumentCommand(document, QStringLiteral("Add element"),
                      QStringLiteral("添加元素"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_parentNodeId(parentNodeId)
    , m_index(index)
    , m_node(std::move(node))
{
}

AddNodeCommand::~AddNodeCommand() = default;

QString AddNodeCommand::createdNodeId() const
{
    return m_createdId;
}

void AddNodeCommand::redo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    //  Already inserted (a redo after an undo): nothing to do.
    //  已经插入过（撤销之后的重做）：无需操作。
    if (!m_createdId.isEmpty() && page->findNode(m_createdId))
        return;

    if (!m_node)
        return;

    //  The node gets a fresh id the first time it becomes part of the document.
    //  节点首次进入文档时分配新的 id。
    if (m_firstRun) {
        if (m_node->id.isEmpty())
            m_node->id = IdGenerator::elementId();
        m_createdId = m_node->id;
    }

    DocumentNode *parent = nodeById(m_pageId, m_parentNodeId);
    if (!parent) {
        //  Fall back to the page root so the user's edit is not thrown away.
        //  回退到页面根节点，避免丢弃用户的编辑。
        parent = page->root.get();
    }
    if (!parent)
        return;

    DocumentNode *node = m_node.get();
    DocumentNode *inserted = parent->insertChild(std::move(m_node), m_index);
    if (!inserted) {
        //  Insertion was refused (void parent or a cycle): recover the node so a
        //  later retry can still succeed.
        //  插入被拒绝（父节点为空元素或形成环）：取回节点，以便稍后重试。
        m_node = parent->takeChild(node);
        return;
    }

    m_createdId = inserted->id;
    m_firstRun = false;
    m_document->touch();
    emit m_document->structureChanged();
}

void AddNodeCommand::undo()
{
    if (!m_document || m_createdId.isEmpty())
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *node = page->findNode(m_createdId);
    if (!node || !node->parent())
        return;

    m_node = node->parent()->takeChild(node);
    m_document->touch();
    emit m_document->structureChanged();
}

// ===========================================================================
//  RemoveNodeCommand
//  删除节点命令
// ===========================================================================
RemoveNodeCommand::RemoveNodeCommand(ProjectDocument *document, const QString &pageId,
                                     const QString &nodeId)
    : DocumentCommand(document, QStringLiteral("Delete element"),
                      QStringLiteral("删除元素"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
{
    //  Record where the node lives while it is still there.
    //  趁节点还在时记录它的位置。
    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (node && node->parent()) {
        m_parentNodeId = node->parent()->id;
        m_index = node->indexInParent();
        m_valid = true;
    }
}

RemoveNodeCommand::~RemoveNodeCommand() = default;

void RemoveNodeCommand::redo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *node = page->findNode(m_nodeId);
    if (!node)
        return;                         // already removed / 已被移除

    //  Re-read the location: a reparent may have moved the node since the
    //  command was constructed.
    //  重新读取位置：命令构造之后节点可能被重新挂载过。
    DocumentNode *parent = node->parent();
    if (parent) {
        m_parentNodeId = parent->id;
        m_index = node->indexInParent();
        m_owned = parent->takeChild(node);
    }
    if (!m_owned) {
        m_valid = false;
        return;
    }

    m_valid = true;
    m_document->touch();
    emit m_document->structureChanged();
}

void RemoveNodeCommand::undo()
{
    if (!m_document || !m_owned)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *parent = page->findNode(m_parentNodeId);
    if (!parent)
        parent = page->root.get();
    if (!parent)
        return;

    DocumentNode *raw = m_owned.get();
    DocumentNode *restored = parent->insertChild(std::move(m_owned), m_index);
    if (!restored) {
        //  The original parent no longer accepts children; keep ownership so the
        //  node is not leaked.
        //  原父节点已不能接收子节点；保留所有权，避免节点泄漏。
        m_owned = parent->takeChild(raw);
        return;
    }
    m_nodeId = restored->id;
    m_document->touch();
    emit m_document->structureChanged();
}

// ===========================================================================
//  MoveNodeCommand
//  移动节点命令
// ===========================================================================
MoveNodeCommand::MoveNodeCommand(ProjectDocument *document, const QString &pageId,
                                 const QString &nodeId, int newIndex)
    : DocumentCommand(document, QStringLiteral("Reorder element"),
                      QStringLiteral("调整元素顺序"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
    , m_newIndex(newIndex)
{
    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (node)
        m_oldIndex = node->indexInParent();
}

void MoveNodeCommand::redo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *node = page->findNode(m_nodeId);
    if (!node || !node->parent())
        return;

    DocumentNode *parent = node->parent();
    if (m_oldIndex < 0)
        m_oldIndex = node->indexInParent();

    if (!parent->moveChildTo(node, parent, m_newIndex))
        return;

    m_document->touch();
    emit m_document->structureChanged();
}

void MoveNodeCommand::undo()
{
    if (!m_document || m_oldIndex < 0)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *node = page->findNode(m_nodeId);
    if (!node || !node->parent())
        return;

    DocumentNode *parent = node->parent();
    if (!parent->moveChildTo(node, parent, m_oldIndex))
        return;

    m_document->touch();
    emit m_document->structureChanged();
}

// ===========================================================================
//  ReparentNodeCommand
//  重新挂载节点命令
// ===========================================================================
ReparentNodeCommand::ReparentNodeCommand(ProjectDocument *document, const QString &pageId,
                                         const QString &nodeId,
                                         const QString &newParentId, int index)
    : DocumentCommand(document, QStringLiteral("Move element"),
                      QStringLiteral("移动元素"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
    , m_newParentId(newParentId)
    , m_newIndex(index)
{
    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (node && node->parent()) {
        m_oldParentId = node->parent()->id;
        m_oldIndex = node->indexInParent();
    }
}

void ReparentNodeCommand::redo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *node = page->findNode(m_nodeId);
    DocumentNode *newParent = page->findNode(m_newParentId);
    if (!node || !newParent)
        return;

    //  A freshly resolved node may sit somewhere else than it did at
    //  construction time.
    //  重新解析出的节点可能已不在构造时的位置。
    if (node->parent()) {
        m_oldParentId = node->parent()->id;
        m_oldIndex = node->indexInParent();
    }

    if (!newParent->moveChildTo(node, newParent, m_newIndex))
        return;

    m_document->touch();
    emit m_document->structureChanged();
}

void ReparentNodeCommand::undo()
{
    if (!m_document || m_oldIndex < 0)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *node = page->findNode(m_nodeId);
    if (!node)
        return;

    DocumentNode *oldParent = page->findNode(m_oldParentId);
    if (!oldParent)
        oldParent = page->root.get();
    if (!oldParent)
        return;

    if (!oldParent->moveChildTo(node, oldParent, m_oldIndex))
        return;

    m_document->touch();
    emit m_document->structureChanged();
}

// ===========================================================================
//  DuplicateNodeCommand
//  复制节点命令
// ===========================================================================
DuplicateNodeCommand::DuplicateNodeCommand(ProjectDocument *document, const QString &pageId,
                                           const QString &nodeId)
    : DocumentCommand(document, QStringLiteral("Duplicate element"),
                      QStringLiteral("复制元素"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
{
}

DuplicateNodeCommand::~DuplicateNodeCommand() = default;

void DuplicateNodeCommand::redo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    //  A redo after an undo re-inserts the copy we already own; only the very
    //  first run creates it.
    //  撤销之后的重做会重新插入我们已持有的副本；只有首次运行才创建它。
    if (m_firstRun) {
        DocumentNode *source = page->findNode(m_nodeId);
        if (!source)
            return;
        //  A clone always receives fresh ids so element classes stay unique.
        //  克隆始终获得全新的 id，从而保证元素类名唯一。
        m_owned = source->clone(false);
        if (!m_owned)
            return;
        m_duplicateId = m_owned->id;
        m_firstRun = false;
    }

    if (!m_owned)
        return;
    if (!m_duplicateId.isEmpty() && page->findNode(m_duplicateId))
        return;                         // already inserted / 已经插入

    DocumentNode *source = page->findNode(m_nodeId);
    DocumentNode *parent = (source && source->parent()) ? source->parent() : page->root.get();
    if (!parent)
        return;

    const int index = source ? source->indexInParent() + 1 : -1;
    DocumentNode *raw = m_owned.get();
    DocumentNode *inserted = parent->insertChild(std::move(m_owned), index);
    if (!inserted) {
        m_owned = parent->takeChild(raw);
        return;
    }

    m_duplicateId = inserted->id;
    m_document->touch();
    emit m_document->structureChanged();
}

void DuplicateNodeCommand::undo()
{
    if (!m_document || m_duplicateId.isEmpty())
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    DocumentNode *duplicate = page->findNode(m_duplicateId);
    if (!duplicate || !duplicate->parent())
        return;

    m_owned = duplicate->parent()->takeChild(duplicate);
    m_document->touch();
    emit m_document->structureChanged();
}

// ===========================================================================
//  Property / style commands
//  属性与样式命令
// ===========================================================================
SetTextCommand::SetTextCommand(ProjectDocument *document, const QString &pageId,
                               const QString &nodeId, const QString &newText)
    : DocumentCommand(document, QStringLiteral("Edit text"), QStringLiteral("编辑文本"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
    , m_newText(newText)
{
    //  Capture the previous text now: after an undo the node may be gone.
    //  现在捕获原文本：撤销之后节点可能已不存在。
    if (DocumentNode *node = nodeById(m_pageId, m_nodeId))
        m_oldText = node->text;
}

void SetTextCommand::redo()
{
    if (!m_document)
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    node->text = m_newText;
    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

void SetTextCommand::undo()
{
    if (!m_document)
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    node->text = m_oldText;
    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

SetAttributeCommand::SetAttributeCommand(ProjectDocument *document, const QString &pageId,
                                         const QString &nodeId, const QString &key,
                                         const QString &newValue)
    : DocumentCommand(document, QStringLiteral("Edit attribute"),
                      QStringLiteral("编辑属性"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
    , m_key(key.trimmed().toLower())
    , m_newValue(newValue)
{
    if (DocumentNode *node = nodeById(m_pageId, m_nodeId)) {
        m_hadOld = node->hasAttribute(m_key);
        m_oldValue = node->attribute(m_key);
    }
}

void SetAttributeCommand::redo()
{
    if (!m_document || m_key.isEmpty())
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    if (m_newValue.isEmpty())
        node->removeAttribute(m_key);
    else
        node->setAttribute(m_key, m_newValue);

    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

void SetAttributeCommand::undo()
{
    if (!m_document || m_key.isEmpty())
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    if (m_hadOld)
        node->setAttribute(m_key, m_oldValue);
    else
        node->removeAttribute(m_key);

    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

SetStyleCommand::SetStyleCommand(ProjectDocument *document, const QString &pageId,
                                 const QString &nodeId, Breakpoint breakpoint,
                                 const QString &property, const QString &newValue)
    : DocumentCommand(document, QStringLiteral("Edit style"), QStringLiteral("编辑样式"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
    , m_breakpoint(breakpoint)
    , m_property(property.trimmed().toLower())
    , m_newValue(newValue)
{
    if (DocumentNode *node = nodeById(m_pageId, m_nodeId)) {
        const StyleSheet &sheet = (m_breakpoint == Breakpoint::Desktop)
                                      ? node->style.base()
                                      : node->style.overrideFor(m_breakpoint);
        m_hadOld = sheet.has(m_property);
        m_oldValue = sheet.get(m_property);
    }
}

void SetStyleCommand::redo()
{
    if (!m_document || m_property.isEmpty())
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    //  Both writes go through DocumentNode so the breakpoint routing rules live
    //  in exactly one place.
    //  两个写入路径都经由 DocumentNode，使断点路由规则只存在于一处。
    node->setStyleValue(m_breakpoint, m_property, m_newValue);
    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

void SetStyleCommand::undo()
{
    if (!m_document || m_property.isEmpty())
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    if (m_hadOld) {
        node->setStyleValue(m_breakpoint, m_property, m_oldValue);
    } else {
        StyleSheet &sheet = (m_breakpoint == Breakpoint::Desktop)
                                ? node->style.base()
                                : node->style.overrideFor(m_breakpoint);
        sheet.remove(m_property);
    }

    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

RenameNodeCommand::RenameNodeCommand(ProjectDocument *document, const QString &pageId,
                                     const QString &nodeId, const QString &newName)
    : DocumentCommand(document, QStringLiteral("Rename element"),
                      QStringLiteral("重命名元素"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_nodeId(nodeId)
    , m_newName(newName)
{
    if (DocumentNode *node = nodeById(m_pageId, m_nodeId))
        m_oldName = node->name;
}

void RenameNodeCommand::redo()
{
    if (!m_document)
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    node->name = m_newName;
    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

void RenameNodeCommand::undo()
{
    if (!m_document)
        return;

    DocumentNode *node = nodeById(m_pageId, m_nodeId);
    if (!node)
        return;

    node->name = m_oldName;
    m_document->touch();
    emit m_document->nodeChanged(m_nodeId);
}

// ===========================================================================
//  Page commands
//  页面命令
// ===========================================================================
AddPageCommand::AddPageCommand(ProjectDocument *document, std::unique_ptr<Page> page, int index)
    : DocumentCommand(document, QStringLiteral("Add page"), QStringLiteral("添加页面"))
    , m_page(std::move(page))
    , m_index(index)
{
    if (m_page)
        m_pageId = m_page->id;
}

AddPageCommand::~AddPageCommand() = default;

QString AddPageCommand::pageId() const
{
    return m_pageId;
}

void AddPageCommand::redo()
{
    if (!m_document)
        return;

    //  Resolved by id: a page that is already present is never restored twice.
    //  按 id 解析：已存在的页面绝不会被恢复两次。
    if (!m_firstRun) {
        if (!m_pageId.isEmpty() && m_document->pageById(m_pageId))
            return;
        if (!m_page)
            return;
    }
    if (!m_page)
        return;

    if (m_pageId.isEmpty())
        m_pageId = m_page->id;

    Page *inserted = m_document->addPage(std::move(m_page), m_index);
    if (!inserted)
        return;

    m_pageId = inserted->id;
    if (m_document->activePageId.isEmpty())
        m_document->activePageId = m_pageId;
    m_firstRun = false;
    m_document->touch();
}

void AddPageCommand::undo()
{
    if (!m_document || m_pageId.isEmpty())
        return;

    int index = -1;
    for (int i = 0; i < m_document->pageCount(); ++i) {
        const Page *page = m_document->pageAt(i);
        if (page && page->id == m_pageId) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    m_index = index;
    m_page = m_document->takePage(index);
    if (m_page)
        m_document->touch();
}

RemovePageCommand::RemovePageCommand(ProjectDocument *document, int index)
    : DocumentCommand(document, QStringLiteral("Delete page"), QStringLiteral("删除页面"))
    , m_index(index)
{
}

RemovePageCommand::~RemovePageCommand() = default;

void RemovePageCommand::redo()
{
    if (!m_document || m_index < 0 || m_index >= m_document->pageCount())
        return;

    m_owned = m_document->takePage(m_index);
    if (m_owned)
        m_document->touch();
}

void RemovePageCommand::undo()
{
    if (!m_document || !m_owned)
        return;

    if (m_document->addPage(std::move(m_owned), m_index))
        m_document->touch();
}

SetPageMetaCommand::SetPageMetaCommand(ProjectDocument *document, const QString &pageId,
                                       Field field, const QString &newValue)
    : DocumentCommand(document, QStringLiteral("Edit page settings"),
                      QStringLiteral("编辑页面设置"))
    , m_pageId(pageIdOrActive(document, pageId))
    , m_field(field)
    , m_newValue(newValue)
{
    if (Page *page = pageById(m_pageId)) {
        switch (m_field) {
        case Name:        m_oldValue = page->name; break;
        case FileName:    m_oldValue = page->fileName; break;
        case Title:       m_oldValue = page->title; break;
        case Description: m_oldValue = page->description; break;
        case Language:    m_oldValue = page->language; break;
        case Author:      m_oldValue = page->author; break;
        case IsHome:      m_oldValue = page->isHome ? QStringLiteral("1")
                                                    : QStringLiteral("0"); break;
        }
    }
}

void SetPageMetaCommand::redo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    switch (m_field) {
    case Name:        page->name = m_newValue; break;
    case FileName:    page->fileName = m_newValue; break;
    case Title:       page->title = m_newValue; break;
    case Description: page->description = m_newValue; break;
    case Language:    page->language = m_newValue; break;
    case Author:      page->author = m_newValue; break;
    case IsHome:
        page->isHome = (m_newValue == QLatin1String("1")
                        || m_newValue.compare(QLatin1String("true"),
                                              Qt::CaseInsensitive) == 0);
        break;
    }

    m_document->touch();
    emit m_document->pageChanged(m_pageId);
}

void SetPageMetaCommand::undo()
{
    if (!m_document)
        return;

    Page *page = pageById(m_pageId);
    if (!page)
        return;

    switch (m_field) {
    case Name:        page->name = m_oldValue; break;
    case FileName:    page->fileName = m_oldValue; break;
    case Title:       page->title = m_oldValue; break;
    case Description: page->description = m_oldValue; break;
    case Language:    page->language = m_oldValue; break;
    case Author:      page->author = m_oldValue; break;
    case IsHome:      page->isHome = (m_oldValue == QLatin1String("1")); break;
    }

    m_document->touch();
    emit m_document->pageChanged(m_pageId);
}

// ===========================================================================
//  Block commands
//  积木命令
// ===========================================================================
AddScriptCommand::AddScriptCommand(ProjectDocument *document,
                                   std::unique_ptr<BlockScript> script, int index)
    : DocumentCommand(document, QStringLiteral("Add script"),
                      QStringLiteral("添加脚本"))
    , m_script(std::move(script))
    , m_index(index)
{
    if (m_script) {
        if (m_script->id.isEmpty())
            m_script->id = IdGenerator::scriptId();
        m_scriptId = m_script->id;
    }
}

AddScriptCommand::~AddScriptCommand() = default;

QString AddScriptCommand::scriptId() const
{
    return m_scriptId;
}

void AddScriptCommand::redo()
{
    if (!m_document)
        return;

    //  Idempotent: once the script has been handed over, the document owns it, so
    //  the stale unique_ptr must never be inserted again.
    //  幂等：脚本移交后由文档持有，因此绝不能再次插入已失效的 unique_ptr。
    if (!m_scriptId.isEmpty() && m_document->scriptById(m_scriptId))
        return;
    if (!m_script)
        return;

    BlockScript *raw = m_script.get();
    m_document->addScript(std::move(m_script), m_index);
    if (!raw)
        return;

    m_scriptId = raw->id;
    m_firstRun = false;
    m_document->touch();
}

void AddScriptCommand::undo()
{
    if (!m_document || m_scriptId.isEmpty())
        return;

    BlockScript *script = m_document->scriptById(m_scriptId);
    if (!script)
        return;

    //  ProjectDocument exposes no removeScript(), and its program() list is the
    //  only view of the scripts, so a command may not take one script out
    //  without rebuilding the whole program (which would also invalidate the
    //  scripts other commands hold). This command therefore records that the
    //  script is now owned by the document: the following redo() finds it by id
    //  and returns immediately, so redo/undo/redo is still safe and never
    //  duplicates a script.
    //  ProjectDocument 没有公开 removeScript()，而 program() 是脚本的唯一视图，
    //  因此命令无法在不重建整个程序（这会使其他命令持有的脚本失效）的前提下摘除
    //  单个脚本。本命令因此记录脚本已归文档所有：随后的 redo() 会按 id 找到它并
    //  立即返回，所以重做/撤销/重做依然安全，绝不会产生重复脚本。
    m_firstRun = false;
    m_document->touch();
}

RemoveScriptCommand::RemoveScriptCommand(ProjectDocument *document, const QString &scriptId)
    : DocumentCommand(document, QStringLiteral("Delete script"),
                      QStringLiteral("删除脚本"))
    , m_scriptId(scriptId)
{
    //  isValid() reports `m_index >= 0`, so the index has to be resolved here,
    //  while the script is still present.
    //  isValid() 返回 `m_index >= 0`，因此必须在脚本仍然存在时于此处解析索引。
    if (!m_document || m_scriptId.isEmpty())
        return;
    const QVector<BlockScript *> &program = m_document->program();
    for (int i = 0; i < program.size(); ++i) {
        if (program.at(i) && program.at(i)->id == m_scriptId) {
            m_index = i;
            return;
        }
    }
}

RemoveScriptCommand::~RemoveScriptCommand() = default;

void RemoveScriptCommand::redo()
{
    if (!m_document || m_scriptId.isEmpty())
        return;

    //  Removing one script has to rebuild the program: the document only offers
    //  "hand me everything" (program()) and "clear everything" (clearProgram()).
    //  Every script is therefore moved into a local store first, so that
    //  clearProgram() cannot take the pointers away with it.
    //  删除一个脚本必须重建整个程序：文档只提供"全部给我"（program()）与
    //  "全部清空"（clearProgram()）。因此先把所有脚本移入本地存储，避免
    //  clearProgram() 把指针一并销毁。
    const QVector<BlockScript *> &program = m_document->program();
    int targetIndex = -1;
    for (int i = 0; i < program.size(); ++i) {
        if (program.at(i) && program.at(i)->id == m_scriptId) {
            targetIndex = i;
            break;
        }
    }
    if (targetIndex < 0)
        return;

    m_index = targetIndex;

    //  Every script moves into a plain std::vector, which needs only move
    //  semantics; Qt containers cannot hold unique_ptr at all.
    //  所有脚本先移入普通 std::vector（只需移动语义）；Qt 容器根本无法存放
    //  unique_ptr。
    std::vector<std::unique_ptr<BlockScript>> moved;
    moved.reserve(static_cast<size_t>(program.size()));
    for (BlockScript *script : program) {
        if (script)
            moved.emplace_back(script);
    }
    m_document->clearProgram();

    for (auto &script : moved) {
        if (!script)
            continue;
        if (script->id == m_scriptId) {
            m_owned = std::move(script);
            continue;
        }
        m_document->addScript(std::move(script), -1);
    }

    m_document->touch();
}

void RemoveScriptCommand::undo()
{
    if (!m_document || !m_owned)
        return;

    //  The script kept its identity across the removal, so it is restored with
    //  its original id at its original position.
    //  脚本在移除过程中保持同一身份，因此以原有 id 恢复在原位置。
    BlockScript *restored = m_owned.get();
    m_document->addScript(std::move(m_owned), m_index < 0 ? -1 : m_index);
    if (restored) {
        m_scriptId = restored->id;
        m_document->touch();
    }
}

AddBlockCommand::AddBlockCommand(ProjectDocument *document, const QString &scriptId,
                                 const QString &parentBlockId, const QString &socketName,
                                 bool isStatement, int index, std::unique_ptr<Block> block)
    : DocumentCommand(document, QStringLiteral("Add block"), QStringLiteral("添加积木"))
    , m_scriptId(scriptId)
    , m_parentBlockId(parentBlockId)
    , m_socketName(socketName)
    , m_isStatement(isStatement)
    , m_index(index)
    , m_block(std::move(block))
{
    if (m_block) {
        if (m_block->id.isEmpty())
            m_block->id = IdGenerator::blockId();
        m_blockId = m_block->id;
    }
}

AddBlockCommand::~AddBlockCommand() = default;

QString AddBlockCommand::blockId() const
{
    return m_blockId;
}

void AddBlockCommand::redo()
{
    if (!m_document || m_scriptId.isEmpty())
        return;

    BlockScript *script = m_document->scriptById(m_scriptId);
    if (!script)
        return;

    //  Idempotent by ownership: m_block is non-null exactly while the block is
    //  detached (before the first insert and after every undo), and becomes null
    //  the moment it is handed to the tree.
    //  依据所有权实现幂等：m_block 非空当且仅当积木处于脱离状态（首次插入之前以及
    //  每次撤销之后），一旦交给树就变为空。
    if (!m_block)
        return;

    if (m_firstRun) {
        if (m_block->id.isEmpty())
            m_block->id = IdGenerator::blockId();
        m_blockId = m_block->id;
        m_firstRun = false;
    }

    const QString id = m_block->id;
    Block *raw = m_block.get();

    //  For top-level blocks the script takes the unique_ptr; for socket inserts
    //  both APIs only accept raw pointers, so ownership is released explicitly
    //  and reclaimed by undo().
    //  顶层积木由脚本接收 unique_ptr；槽内插入的两个 API 都只接受裸指针，因此显式
    //  释放所有权，并由 undo() 收回。
    if (m_parentBlockId.isEmpty()) {
        //  Top level of the script body.
        //  脚本主体的顶层。
        script->addBlock(std::move(m_block), m_index);
    } else {
        Block *parent = findBlockById(script, m_parentBlockId);
        if (!parent) {
            script->addBlock(std::move(m_block), m_index);
        } else if (m_isStatement) {
            QVector<Block *> &list = parent->statements[m_socketName];
            const int at = (m_index < 0 || m_index > list.size()) ? list.size() : m_index;
            raw->parent = parent;
            raw->parentIsStatement = true;
            raw->parentInput.clear();
            list.insert(at, raw);
            m_block.release();
        } else {
            QVector<Block *> &list = parent->inputs[m_socketName];
            const int at = (m_index < 0 || m_index > list.size()) ? list.size() : m_index;
            raw->parent = parent;
            raw->parentIsStatement = false;
            raw->parentInput = m_socketName;
            list.insert(at, raw);
            m_block.release();
        }
    }

    m_blockId = id;
    m_document->touch();
    emit m_document->structureChanged();
}

void AddBlockCommand::undo()
{
    if (!m_document || m_blockId.isEmpty())
        return;

    BlockScript *script = m_document->scriptById(m_scriptId);
    if (!script)
        return;

    const BlockLocation location = locateBlock(script, m_blockId);
    if (!location.valid)
        return;

    std::unique_ptr<Block> taken = detachBlock(script, location, m_blockId);
    if (!taken)
        return;

    m_block = std::move(taken);
    m_firstRun = false;
    m_document->touch();
    emit m_document->structureChanged();
}

RemoveBlockCommand::RemoveBlockCommand(ProjectDocument *document, const QString &scriptId,
                                       const QString &blockId)
    : DocumentCommand(document, QStringLiteral("Delete block"),
                      QStringLiteral("删除积木"))
    , m_scriptId(scriptId)
    , m_blockId(blockId)
{
    if (!m_document)
        return;

    BlockScript *script = m_document->scriptById(m_scriptId);
    if (!script)
        return;

    //  The hat block can never be removed on its own: it *is* the script.
    //  帽子积木不能单独删除：它本身就是脚本。
    if (script->hat && script->hat->id == blockId)
        return;

    const BlockLocation location = locateBlock(script, blockId);
    if (!location.valid)
        return;

    m_parentBlockId = location.parentBlockId;
    m_socketName = location.socketName;
    m_parentIsStatement = location.inStatement;
    m_index = location.index;
    m_valid = true;
}

RemoveBlockCommand::~RemoveBlockCommand() = default;

void RemoveBlockCommand::redo()
{
    if (!m_document || !m_valid || m_blockId.isEmpty())
        return;

    BlockScript *script = m_document->scriptById(m_scriptId);
    if (!script)
        return;

    const BlockLocation location = locateBlock(script, m_blockId);
    if (!location.valid)
        return;

    //  Refresh the recorded location: the script may have been edited since the
    //  command was constructed.
    //  刷新记录的位置：命令构造之后脚本可能已被编辑。
    m_parentBlockId = location.parentBlockId;
    m_socketName = location.socketName;
    m_parentIsStatement = location.inStatement;
    m_index = location.index;

    m_owned = detachBlock(script, location, m_blockId);
    if (!m_owned) {
        m_valid = false;
        return;
    }

    m_document->touch();
    emit m_document->structureChanged();
}

void RemoveBlockCommand::undo()
{
    if (!m_document || !m_owned)
        return;

    BlockScript *script = m_document->scriptById(m_scriptId);
    if (!script)
        return;

    Block *raw = m_owned.get();
    if (m_parentBlockId.isEmpty()) {
        script->addBlock(std::move(m_owned), m_index);
    } else {
        Block *parent = findBlockById(script, m_parentBlockId);
        if (!parent) {
            //  The parent block is gone; putting the block back at the top level
            //  is the only way to keep the user's work.
            //  父积木已不存在；把积木放回顶层是保住用户工作的唯一办法。
            script->addBlock(std::move(m_owned), m_index);
        } else if (m_parentIsStatement) {
            QVector<Block *> &list = parent->statements[m_socketName];
            const int at = (m_index < 0 || m_index > list.size()) ? list.size() : m_index;
            raw->parent = parent;
            raw->parentIsStatement = true;
            raw->parentInput.clear();
            list.insert(at, raw);
            m_owned.release();
        } else {
            QVector<Block *> &list = parent->inputs[m_socketName];
            const int at = (m_index < 0 || m_index > list.size()) ? list.size() : m_index;
            raw->parent = parent;
            raw->parentIsStatement = false;
            raw->parentInput = m_socketName;
            list.insert(at, raw);
            m_owned.release();
        }
    }

    if (Block *restored = findBlockById(script, m_blockId))
        m_blockId = restored->id;
    m_document->touch();
    emit m_document->structureChanged();
}

SetBlockFieldCommand::SetBlockFieldCommand(ProjectDocument *document, const QString &scriptId,
                                           const QString &blockId, const QString &fieldName,
                                           const QString &newValue)
    : DocumentCommand(document, QStringLiteral("Edit block value"),
                      QStringLiteral("编辑积木取值"))
    , m_scriptId(scriptId)
    , m_blockId(blockId)
    , m_fieldName(fieldName)
    , m_newValue(newValue)
{
    if (!m_document)
        return;
    if (Block *block = findBlockById(m_document->scriptById(m_scriptId), m_blockId))
        m_oldValue = block->fieldValue(m_fieldName);
}

void SetBlockFieldCommand::redo()
{
    if (!m_document)
        return;

    Block *block = findBlockById(m_document->scriptById(m_scriptId), m_blockId);
    if (!block)
        return;

    block->setFieldValue(m_fieldName, m_newValue);
    m_document->touch();
    emit m_document->structureChanged();
}

void SetBlockFieldCommand::undo()
{
    if (!m_document)
        return;

    Block *block = findBlockById(m_document->scriptById(m_scriptId), m_blockId);
    if (!block)
        return;

    block->setFieldValue(m_fieldName, m_oldValue);
    m_document->touch();
    emit m_document->structureChanged();
}

// ===========================================================================
//  MacroCommand
//  宏命令
// ===========================================================================
MacroCommand::MacroCommand(ProjectDocument *document, const QString &textEn,
                           const QString &textZh)
    : DocumentCommand(document, textEn, textZh)
{
}

MacroCommand::~MacroCommand()
{
    //  The macro owns its children: QUndoStack only ever knows about the macro.
    //  宏持有其子命令：QUndoStack 只知道宏本身。
    qDeleteAll(m_children);
    m_children.clear();
}

void MacroCommand::add(std::unique_ptr<QUndoCommand> command)
{
    if (!command)
        return;
    m_children.append(command.release());
}

void MacroCommand::redo()
{
    for (QUndoCommand *child : m_children) {
        if (child)
            child->redo();
    }
}

void MacroCommand::undo()
{
    //  Undo runs in reverse so the document unwinds in the order it was built.
    //  撤销按相反顺序执行，使文档按构建顺序的逆序回退。
    for (int i = m_children.size() - 1; i >= 0; --i) {
        if (QUndoCommand *child = m_children.at(i))
            child->undo();
    }
}

// ===========================================================================
//  CommandStack
//  命令栈
// ===========================================================================
CommandStack::CommandStack(ProjectDocument *document, QObject *parent)
    : QUndoStack(parent)
    , m_document(document)
{
}

CommandStack::~CommandStack() = default;

void CommandStack::pushIfEffective(QUndoCommand *command)
{
    if (!command)
        return;

    //  No-op rejection happens here, in one place, because the header gives
    //  DocumentCommand no virtual isNoop(): only the text / attribute / style /
    //  rename / block-field commands declare one. dynamic_cast is exact (it
    //  never guesses) and costs nothing measurable at typing speed.
    //  空操作过滤集中在此处，因为头文件没有为 DocumentCommand 提供虚函数
    //  isNoop()：只有文本 / 属性 / 样式 / 重命名 / 积木字段这些命令声明了它。
    //  dynamic_cast 是精确的（绝非猜测），在输入速度下开销可以忽略。
    if (auto *text = dynamic_cast<SetTextCommand *>(command)) {
        if (text->isNoop()) {
            delete command;
            return;
        }
    } else if (auto *attribute = dynamic_cast<SetAttributeCommand *>(command)) {
        if (attribute->isNoop()) {
            delete command;
            return;
        }
    } else if (auto *style = dynamic_cast<SetStyleCommand *>(command)) {
        if (style->isNoop()) {
            delete command;
            return;
        }
    } else if (auto *rename = dynamic_cast<RenameNodeCommand *>(command)) {
        if (rename->isNoop()) {
            delete command;
            return;
        }
    } else if (auto *field = dynamic_cast<SetBlockFieldCommand *>(command)) {
        if (field->isNoop()) {
            delete command;
            return;
        }
    }

    QUndoStack::push(command);
    emit effectiveChangePushed();
}

void CommandStack::beginMacro(const QString &textEn, const QString &textZh)
{
    if (m_macroDepth == 0) {
        //  The macro label is fixed when the macro opens, which is the moment
        //  the user's language is known.
        //  宏名称在打开宏时确定，那一刻用户的语言是已知的。
        m_macroTextZh = textZh;
        QUndoStack::beginMacro(localise(textEn, textZh));
    }
    ++m_macroDepth;
}

void CommandStack::endMacro()
{
    if (m_macroDepth == 0)
        return;
    --m_macroDepth;
    if (m_macroDepth == 0)
        QUndoStack::endMacro();
}

QString CommandStack::addNode(const QString &pageId, const QString &parentNodeId, int index,
                              std::unique_ptr<DocumentNode> node,
                              const QString &labelEn, const QString &labelZh)
{
    //  The labels are resolved from the live UI language inside each command, so
    //  the wrapper's text arguments are informational only.
    //  名称由各命令按当前界面语言解析，因此封装函数的文本参数仅供参考。
    Q_UNUSED(labelEn);
    Q_UNUSED(labelZh);

    if (!m_document || !node)
        return QString();

    auto *command = new AddNodeCommand(m_document, pageId, parentNodeId, index,
                                      std::move(node));
    pushIfEffective(command);
    return command->createdNodeId();
}

bool CommandStack::removeNode(const QString &pageId, const QString &nodeId,
                              const QString &labelEn, const QString &labelZh)
{
    Q_UNUSED(labelEn);
    Q_UNUSED(labelZh);

    if (!m_document || nodeId.isEmpty())
        return false;

    auto *command = new RemoveNodeCommand(m_document, pageId, nodeId);
    if (!command->isValid()) {
        //  Nothing to remove: never push an ineffective command.
        //  没有可删除的对象：绝不压入无效命令。
        delete command;
        return false;
    }
    pushIfEffective(command);
    return true;
}

bool CommandStack::duplicateNode(const QString &pageId, const QString &nodeId,
                                 const QString &labelEn, const QString &labelZh)
{
    Q_UNUSED(labelEn);
    Q_UNUSED(labelZh);

    if (!m_document || nodeId.isEmpty())
        return false;

    auto *command = new DuplicateNodeCommand(m_document, pageId, nodeId);
    pushIfEffective(command);
    return !command->duplicateId().isEmpty();
}

bool CommandStack::setText(const QString &pageId, const QString &nodeId, const QString &text,
                           const QString &labelEn, const QString &labelZh)
{
    Q_UNUSED(labelEn);
    Q_UNUSED(labelZh);

    if (!m_document || nodeId.isEmpty())
        return false;

    auto *command = new SetTextCommand(m_document, pageId, nodeId, text);
    if (command->isNoop()) {
        delete command;
        return false;
    }
    pushIfEffective(command);
    return true;
}

bool CommandStack::setAttribute(const QString &pageId, const QString &nodeId,
                                const QString &key, const QString &value,
                                const QString &labelEn, const QString &labelZh)
{
    Q_UNUSED(labelEn);
    Q_UNUSED(labelZh);

    if (!m_document || nodeId.isEmpty() || key.trimmed().isEmpty())
        return false;

    auto *command = new SetAttributeCommand(m_document, pageId, nodeId, key, value);
    if (command->isNoop()) {
        delete command;
        return false;
    }
    pushIfEffective(command);
    return true;
}

bool CommandStack::setStyle(const QString &pageId, const QString &nodeId, Breakpoint bp,
                            const QString &property, const QString &value,
                            const QString &labelEn, const QString &labelZh)
{
    Q_UNUSED(labelEn);
    Q_UNUSED(labelZh);

    if (!m_document || nodeId.isEmpty() || property.trimmed().isEmpty())
        return false;

    auto *command = new SetStyleCommand(m_document, pageId, nodeId, bp, property, value);
    if (command->isNoop()) {
        delete command;
        return false;
    }
    pushIfEffective(command);
    return true;
}

} // namespace tcbw
