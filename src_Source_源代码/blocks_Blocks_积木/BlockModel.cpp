// ============================================================================
//  BlockModel.cpp -- Block and BlockScript: ownership, cloning and JSON
//  BlockModel.cpp —— 积木与积木脚本：所有权、克隆与 JSON
// ============================================================================
#include "BlockDef.h"

#include "IdGenerator.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace tcbw {

namespace {

//  Hard cap on nesting. It bounds recursion for hand written or corrupted
//  project files and turns a runaway structure into a clean error.
//  嵌套硬上限。用于约束手写或损坏工程文件的递归深度，
//  把失控结构变成一条清晰的错误。
constexpr int kMaxNestingDepth = 256;

//  Removes `block` from the container that currently owns it and clears the
//  three back links. Never deletes anything.
//  把 `block` 从当前拥有它的容器中移除，并清空三个反向链接。不销毁任何对象。
void unlinkFromParent(Block *block)
{
    if (!block)
        return;

    Block *p = block->parent;
    if (p) {
        if (block->parentIsStatement) {
            auto it = p->statements.find(block->parentInput);
            if (it != p->statements.end())
                it.value().removeAll(block);
        } else {
            auto it = p->inputs.find(block->parentInput);
            if (it != p->inputs.end())
                it.value().removeAll(block);
        }
    }
    block->parent = nullptr;
    block->parentInput.clear();
    block->parentIsStatement = false;
}

//  Depth limited recursive reader. Returns nullptr and fills `error` on any
//  structural problem (missing type, non-object child, nesting too deep).
//  带深度限制的递归读取器。遇到任何结构问题（缺少 type、子项非对象、
//  嵌套过深）返回 nullptr 并写入 `error`。
std::unique_ptr<Block> readBlock(const QJsonObject &obj, QString *error, int depth)
{
    if (depth > kMaxNestingDepth) {
        if (error)
            *error = QStringLiteral("Block nesting too deep");
        return nullptr;
    }

    const QString type = obj.value(QStringLiteral("type")).toString().trimmed();
    if (type.isEmpty()) {
        if (error)
            *error = QStringLiteral("Block is missing its type");
        return nullptr;
    }

    auto block = std::make_unique<Block>(type);

    const QString id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty())
        block->id = id;

    const QJsonObject fields = obj.value(QStringLiteral("fieldValues")).toObject();
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        block->fieldValues.insert(it.key(), it.value().toString());

    //  Reads one slot map ("inputs" or "statements") into the block.
    //  把一个槽位映射（"inputs" 或 "statements"）读入积木。
    const auto readSlots = [&](const char *key, bool isStatement) -> bool {
        //  NB: `slots` is a Qt keyword macro, so the local must be named
        //  differently. / 注意：`slots` 是 Qt 的宏，局部变量必须改名。
        const QJsonObject slotMap =
            obj.value(QLatin1String(key)).toObject();
        for (auto it = slotMap.constBegin(); it != slotMap.constEnd(); ++it) {
            if (!it.value().isArray())
                continue;
            const QJsonArray children = it.value().toArray();
            for (const QJsonValue &value : children) {
                if (!value.isObject()) {
                    if (error)
                        *error = QStringLiteral("Block child is not a JSON object");
                    return false;
                }
                std::unique_ptr<Block> child = readBlock(value.toObject(), error, depth + 1);
                if (!child)
                    return false;
                child->parent = block.get();
                child->parentInput = it.key();
                child->parentIsStatement = isStatement;
                if (isStatement)
                    block->statements[it.key()].append(child.release());
                else
                    block->inputs[it.key()].append(child.release());
            }
        }
        return true;
    };

    if (!readSlots("inputs", false))
        return nullptr;
    if (!readSlots("statements", true))
        return nullptr;

    const QJsonObject ui = obj.value(QStringLiteral("ui")).toObject();
    if (!ui.isEmpty()) {
        block->uiX = ui.value(QStringLiteral("x")).toDouble(0.0);
        block->uiY = ui.value(QStringLiteral("y")).toDouble(0.0);
        block->uiCollapsed = ui.value(QStringLiteral("collapsed")).toBool(false);
        block->comment = ui.value(QStringLiteral("comment")).toString();
    }
    return block;
}

//  Serialises one slot map.
//  序列化一个槽位映射。
QJsonObject slotsToJson(const QMap<QString, QVector<Block *>> &slotMap)
{
    QJsonObject out;
    for (auto it = slotMap.constBegin(); it != slotMap.constEnd(); ++it) {
        QJsonArray children;
        for (const Block *child : it.value()) {
            if (child)
                children.append(child->toJson());
        }
        out.insert(it.key(), children);
    }
    return out;
}

} // namespace

// ===========================================================================
//  Block
//  积木
// ===========================================================================
Block::Block()
    : id(IdGenerator::blockId())
{
}

Block::Block(const QString &type)
    : id(IdGenerator::blockId()),
      type(type)
{
}

Block::~Block()
{
    //  Remove ourselves from the parent container first, so a manual `delete`
    //  never leaves a dangling pointer behind.
    //  先把自己从父容器中摘除，这样手动 `delete` 也不会留下悬空指针。
    unlinkFromParent(this);

    //  Ownership: everything reachable through `inputs` and `statements`.
    //  De-duplicate defensively, in case a corrupt file repeats one pointer.
    //  所有权：`inputs` 与 `statements` 中可达的一切。
    //  防御性地去重，以防损坏的文件重复引用同一指针。
    QSet<Block *> owned;
    for (auto it = inputs.begin(); it != inputs.end(); ++it) {
        for (Block *child : it.value()) {
            if (child)
                owned.insert(child);
        }
    }
    for (auto it = statements.begin(); it != statements.end(); ++it) {
        for (Block *child : it.value()) {
            if (child)
                owned.insert(child);
        }
    }

    for (Block *child : owned) {
        //  Clear the back link first: the child must not touch our containers
        //  while they are being destroyed.
        //  先清空反向链接：子积木在我们容器析构期间不得再访问它们。
        child->parent = nullptr;
        child->parentInput.clear();
        child->parentIsStatement = false;
        delete child;
    }
    inputs.clear();
    statements.clear();
}

std::unique_ptr<Block> Block::clone(bool keepIds) const
{
    auto copy = std::make_unique<Block>(type);
    copy->id = keepIds ? id : IdGenerator::blockId();
    copy->fieldValues = fieldValues;
    copy->uiX = uiX;
    copy->uiY = uiY;
    copy->uiCollapsed = uiCollapsed;
    copy->comment = comment;

    for (auto it = inputs.constBegin(); it != inputs.constEnd(); ++it) {
        for (const Block *child : it.value()) {
            if (!child)
                continue;
            std::unique_ptr<Block> childCopy = child->clone(keepIds);
            childCopy->parent = copy.get();
            childCopy->parentInput = it.key();
            childCopy->parentIsStatement = false;
            copy->inputs[it.key()].append(childCopy.release());
        }
    }
    for (auto it = statements.constBegin(); it != statements.constEnd(); ++it) {
        for (const Block *child : it.value()) {
            if (!child)
                continue;
            std::unique_ptr<Block> childCopy = child->clone(keepIds);
            childCopy->parent = copy.get();
            childCopy->parentInput = it.key();
            childCopy->parentIsStatement = true;
            copy->statements[it.key()].append(childCopy.release());
        }
    }
    return copy;
}

QString Block::fieldValue(const QString &name) const
{
    return fieldValues.value(name);
}

void Block::setFieldValue(const QString &name, const QString &value)
{
    if (name.isEmpty())
        return;
    fieldValues.insert(name, value);
}

const QVector<Block *> &Block::inputBlocks(const QString &name) const
{
    auto it = inputs.constFind(name);
    if (it != inputs.constEnd())
        return it.value();
    return m_empty;
}

const QVector<Block *> &Block::statementBlocks(const QString &name) const
{
    auto it = statements.constFind(name);
    if (it != statements.constEnd())
        return it.value();
    return m_empty;
}

bool Block::hasInput(const QString &name) const
{
    auto it = inputs.constFind(name);
    if (it == inputs.constEnd())
        return false;
    for (const Block *child : it.value()) {
        if (child)
            return true;
    }
    return false;
}

void Block::forEach(const std::function<void(Block *)> &fn)
{
    if (!fn)
        return;
    QSet<Block *> seen;
    std::function<void(Block *, int)> walk = [&](Block *node, int depth) {
        if (!node || depth > kMaxNestingDepth || seen.contains(node))
            return;
        seen.insert(node);
        fn(node);
        //  Snapshot the children: `fn` is allowed to restructure the tree.
        //  先快照子节点：`fn` 允许改动树结构。
        QVector<Block *> children;
        for (auto it = node->inputs.constBegin(); it != node->inputs.constEnd(); ++it) {
            for (Block *child : it.value())
                children.append(child);
        }
        for (auto it = node->statements.constBegin(); it != node->statements.constEnd(); ++it) {
            for (Block *child : it.value())
                children.append(child);
        }
        for (Block *child : children)
            walk(child, depth + 1);
    };
    walk(this, 0);
}

void Block::forEach(const std::function<void(const Block *)> &fn) const
{
    if (!fn)
        return;
    QSet<const Block *> seen;
    std::function<void(const Block *, int)> walk = [&](const Block *node, int depth) {
        if (!node || depth > kMaxNestingDepth || seen.contains(node))
            return;
        seen.insert(node);
        fn(node);
        QVector<const Block *> children;
        for (auto it = node->inputs.constBegin(); it != node->inputs.constEnd(); ++it) {
            for (const Block *child : it.value())
                children.append(child);
        }
        for (auto it = node->statements.constBegin(); it != node->statements.constEnd(); ++it) {
            for (const Block *child : it.value())
                children.append(child);
        }
        for (const Block *child : children)
            walk(child, depth + 1);
    };
    walk(this, 0);
}

int Block::depth() const
{
    int level = 0;
    const Block *p = parent;
    while (p && level < kMaxNestingDepth) {
        ++level;
        p = p->parent;
    }
    return level;
}

QJsonObject Block::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("type"), type);
    QJsonObject fields;
    for (auto it = fieldValues.constBegin(); it != fieldValues.constEnd(); ++it)
        fields.insert(it.key(), it.value());
    obj.insert(QStringLiteral("fieldValues"), fields);
    obj.insert(QStringLiteral("inputs"), slotsToJson(inputs));
    obj.insert(QStringLiteral("statements"), slotsToJson(statements));

    QJsonObject ui;
    ui.insert(QStringLiteral("x"), uiX);
    ui.insert(QStringLiteral("y"), uiY);
    ui.insert(QStringLiteral("collapsed"), uiCollapsed);
    ui.insert(QStringLiteral("comment"), comment);
    obj.insert(QStringLiteral("ui"), ui);
    return obj;
}

std::unique_ptr<Block> Block::fromJson(const QJsonObject &obj, QString *error)
{
    if (error)
        error->clear();
    QString localError;
    std::unique_ptr<Block> block = readBlock(obj, &localError, 0);
    if (!block && error)
        *error = localError;
    return block;
}

void Block::assignIds()
{
    forEach([](Block *block) {
        if (block)
            block->id = IdGenerator::blockId();
    });
}

void Block::ensureIds()
{
    forEach([](Block *block) {
        if (block && block->id.trimmed().isEmpty())
            block->id = IdGenerator::blockId();
    });
}

void Block::detach()
{
    unlinkFromParent(this);
}

// ===========================================================================
//  BlockScript
//  积木脚本
// ===========================================================================
BlockScript::BlockScript()
    : BlockScript(QStringLiteral("events.start"))
{
}

BlockScript::BlockScript(const QString &eventType)
    : id(IdGenerator::scriptId()),
      eventType(eventType.trimmed().isEmpty() ? QStringLiteral("events.start")
                                              : eventType.trimmed()),
      hat(std::make_unique<Block>(eventType.trimmed().isEmpty()
                                      ? QStringLiteral("events.start")
                                      : eventType.trimmed()))
{
}

BlockScript::~BlockScript()
{
    //  `hat` is a unique_ptr and destroys itself. The body blocks are owned by
    //  this script; clear their back links so they do not touch a half
    //  destroyed parent.
    //  `hat` 是 unique_ptr，会自行销毁。主体积木由本脚本拥有；
    //  先清空反向链接，避免它们访问正在析构的父对象。
    for (Block *block : body) {
        if (!block)
            continue;
        block->parent = nullptr;
        block->parentInput.clear();
        block->parentIsStatement = false;
        delete block;
    }
    body.clear();
}

std::unique_ptr<BlockScript> BlockScript::clone(bool keepIds) const
{
    auto copy = std::make_unique<BlockScript>(eventType);
    copy->id = keepIds ? id : IdGenerator::scriptId();
    copy->name = name;
    copy->pageTarget = pageTarget;
    copy->enabled = enabled;
    copy->uiX = uiX;
    copy->uiY = uiY;

    if (hat)
        copy->hat = hat->clone(keepIds);

    for (const Block *block : body) {
        if (!block)
            continue;
        std::unique_ptr<Block> child = block->clone(keepIds);
        child->parent = copy->hat.get();
        child->parentInput = QStringLiteral("body");
        child->parentIsStatement = true;
        copy->body.append(child.release());
    }
    return copy;
}

void BlockScript::addBlock(std::unique_ptr<Block> block, int index)
{
    if (!block)
        return;

    Block *raw = block.release();
    //  Moving a block that is still attached elsewhere must not leave a
    //  dangling pointer in the old parent.
    //  若积木仍挂在别处，必须先摘除，否则旧父节点会留下悬空指针。
    unlinkFromParent(raw);

    if (index < 0 || index > body.size())
        index = body.size();
    body.insert(index, raw);

    //  Blocks hanging below a hat are statement children of the hat block.
    //  挂在帽子块下方的积木是帽子块的语句子节点。
    raw->parent = hat.get();
    raw->parentInput = QStringLiteral("body");
    raw->parentIsStatement = true;
}

std::unique_ptr<Block> BlockScript::takeBlock(int index)
{
    if (index < 0 || index >= body.size())
        return nullptr;

    Block *raw = body.takeAt(index);
    if (!raw)
        return nullptr;

    raw->parent = nullptr;
    raw->parentInput.clear();
    raw->parentIsStatement = false;

    //  Ownership moved to the caller: make sure the destructor does not free it
    //  a second time and that the block no longer points at a stale parent.
    //  所有权已转移给调用方：确保析构函数不会二次释放，
    //  且该积木不再指向已失效的父对象。
    return std::unique_ptr<Block>(raw);
}

std::unique_ptr<Block> BlockScript::takeBlock(Block *block)
{
    if (!block)
        return nullptr;
    const int index = indexOf(block);
    if (index < 0)
        return nullptr;
    return takeBlock(index);
}

int BlockScript::indexOf(const Block *block) const
{
    if (!block)
        return -1;
    for (int i = 0; i < body.size(); ++i) {
        if (body.at(i) == block)
            return i;
    }
    return -1;
}

int BlockScript::blockCount() const
{
    return body.size();
}

void BlockScript::forEach(const std::function<void(Block *)> &fn)
{
    if (!fn)
        return;
    if (hat)
        hat->forEach(fn);
    QSet<Block *> seen;
    for (Block *block : body) {
        if (!block || seen.contains(block))
            continue;
        seen.insert(block);
        block->forEach(fn);
    }
}

void BlockScript::forEach(const std::function<void(const Block *)> &fn) const
{
    if (!fn)
        return;
    //  Bind through a const pointer so the const overload is chosen.
    //  通过 const 指针调用，以选中 const 重载。
    const Block *hatBlock = hat.get();
    if (hatBlock)
        hatBlock->forEach(fn);
    QSet<const Block *> seen;
    for (const Block *block : body) {
        if (!block || seen.contains(block))
            continue;
        seen.insert(block);
        block->forEach(fn);
    }
}

QJsonObject BlockScript::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("eventType"), eventType);
    obj.insert(QStringLiteral("pageTarget"), pageTarget);
    obj.insert(QStringLiteral("enabled"), enabled);
    obj.insert(QStringLiteral("ui"),
               QJsonObject{ { QStringLiteral("x"), uiX }, { QStringLiteral("y"), uiY } });

    if (hat)
        obj.insert(QStringLiteral("hat"), hat->toJson());

    QJsonArray bodyArray;
    for (const Block *block : body) {
        if (block)
            bodyArray.append(block->toJson());
    }
    obj.insert(QStringLiteral("body"), bodyArray);
    return obj;
}

std::unique_ptr<BlockScript> BlockScript::fromJson(const QJsonObject &obj, QString *error)
{
    if (error)
        error->clear();

    const QString eventType = obj.value(QStringLiteral("eventType")).toString().trimmed();
    auto script = std::make_unique<BlockScript>(eventType);

    const QString id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty())
        script->id = id;
    script->name = obj.value(QStringLiteral("name")).toString();
    script->pageTarget = obj.value(QStringLiteral("pageTarget")).toString();
    script->enabled = obj.value(QStringLiteral("enabled")).toBool(true);

    const QJsonObject ui = obj.value(QStringLiteral("ui")).toObject();
    script->uiX = ui.value(QStringLiteral("x")).toDouble(0.0);
    script->uiY = ui.value(QStringLiteral("y")).toDouble(0.0);

    const QJsonObject hatObj = obj.value(QStringLiteral("hat")).toObject();
    if (!hatObj.isEmpty()) {
        QString hatError;
        std::unique_ptr<Block> hat = Block::fromJson(hatObj, &hatError);
        if (!hat) {
            if (error)
                *error = hatError;
            return nullptr;
        }
        script->hat = std::move(hat);
        if (!script->hat->type.isEmpty())
            script->eventType = script->hat->type;
        else
            script->hat->type = script->eventType;
    }

    if (!script->hat)
        script->hat = std::make_unique<Block>(script->eventType);

    const QJsonArray bodyArray = obj.value(QStringLiteral("body")).toArray();
    for (const QJsonValue &value : bodyArray) {
        if (!value.isObject())
            continue;
        QString childError;
        std::unique_ptr<Block> child = Block::fromJson(value.toObject(), &childError);
        if (!child) {
            if (error)
                *error = childError;
            return nullptr;
        }
        script->addBlock(std::move(child));
    }
    return script;
}

} // namespace tcbw
