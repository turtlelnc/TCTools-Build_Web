// ============================================================================
//  BlockDef.h -- block shapes, definitions and block instances
//  BlockDef.h —— 积木形状、定义与实例
// ============================================================================
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  BlockShape -- the visual grammar of the Scratch-like editor
//  积木形状 —— 类 Scratch 编辑器的视觉语法
// ---------------------------------------------------------------------------
enum class BlockShape {
    Hat = 0,      // starts a script / 脚本起始（帽子块）
    Stack,        // statement, connects below / 语句块，向下连接
    CShape,       // wraps a statement list / 包裹语句列表（C 形块）
    EShape,       // C-shape with an else branch / 带 else 分支的 C 形块
    Cap,          // ends a script / 结束块
    Reporter,     // returns a value, oval / 返回值（椭圆形）
    Boolean,      // returns true/false, hexagon / 返回布尔值（六边形）
    Round         // numeric literal slot / 数值槽
};

// ---------------------------------------------------------------------------
//  Value kinds understood by the type checker
//  类型检查器可识别的值种类
// ---------------------------------------------------------------------------
enum class ValueKind {
    Any = 0, Number, String, Boolean, Colour, Element, List, Object, Function
};

QString toString(BlockShape v);
QString toString(ValueKind v);
BlockShape blockShapeFromString(const QString &s, BlockShape fallback = BlockShape::Stack);
ValueKind valueKindFromString(const QString &s, ValueKind fallback = ValueKind::Any);
bool shapeReturnsValue(BlockShape shape);

// ---------------------------------------------------------------------------
//  FieldDef -- an inline, non-connectable slot (dropdown, number, text)
//  字段定义 —— 内联的、不可连接的槽位（下拉、数字、文本）
// ---------------------------------------------------------------------------
struct BlockFieldDef
{
    QString name;
    QString labelEn;
    QString labelZh;
    ValueKind kind = ValueKind::String;
    QString defaultValue;
    QStringList options;        // non-empty => dropdown / 非空表示下拉框
    QStringList optionLabelsZh; // parallel Chinese labels / 对应的中文标签
    double minValue = 0.0;
    double maxValue = 0.0;
    bool hasRange = false;
    QString prefix;             // e.g. "#" for colours / 例如颜色的 "#"
};

// ---------------------------------------------------------------------------
//  InputDef -- a connectable socket
//  输入定义 —— 可连接的槽
// ---------------------------------------------------------------------------
struct BlockInputDef
{
    QString name;
    QString labelEn;
    QString labelZh;
    ValueKind kind = ValueKind::Any;
    QString shadowType;         // default inline block, e.g. "math.number"
    QString shadowValue;        // default literal / 默认字面量
    bool acceptsBlocks = true;
};

// ---------------------------------------------------------------------------
//  StatementDef -- a nested sequence slot for C/E shaped blocks
//  语句定义 —— C/E 形块内部的嵌套语句序列槽
// ---------------------------------------------------------------------------
struct BlockStatementDef
{
    QString name;
    QString labelEn;
    QString labelZh;
    int minBlocks = 0;
    int maxBlocks = -1;         // -1 == unlimited / -1 表示不限制
};

// ---------------------------------------------------------------------------
//  BlockDef -- the full definition of one block type
//  积木定义 —— 一种积木类型的完整定义
// ---------------------------------------------------------------------------
struct BlockDef
{
    QString type;               // "events.start", "control.if", "http.get"
    QString category;           // key into BlockRegistry categories
    QString labelEn;            // "%1" placeholders, in declaration order
    QString labelZh;
    QString tooltipEn;
    QString tooltipZh;
    BlockShape shape = BlockShape::Stack;
    QString colour;             // hex, from the category by default
    QVector<BlockFieldDef> fields;
    QVector<BlockInputDef> inputs;
    QVector<BlockStatementDef> statements;
    bool terminal = false;      // code generation stops after this block
    bool experimental = false;
    QStringList keywords;       // free-text search terms / 全文搜索关键词

    bool returnsValue() const { return shapeReturnsValue(shape); }
    bool hasNestedStatements() const { return !statements.isEmpty(); }
};

// ---------------------------------------------------------------------------
//  Block -- one instance in a script
//  积木 —— 脚本中的一个实例
// ---------------------------------------------------------------------------
class Block
{
public:
    Block();
    explicit Block(const QString &type);
    ~Block();

    Block(const Block &) = delete;
    Block &operator=(const Block &) = delete;

    std::unique_ptr<Block> clone(bool keepIds = false) const;

    QString id;
    QString type;

    // --- slots -------------------------------------------------------------
    QMap<QString, QString> fieldValues;
    //  input name -> connected blocks (a reporter chain; empty => shadow)
    //  输入名 -> 已连接的积木（返回值链；为空则使用影子值）
    QMap<QString, QVector<Block *>> inputs;
    //  statement name -> nested statement sequence
    //  语句名 -> 嵌套语句序列
    QMap<QString, QVector<Block *>> statements;

    //  For reporter/boolean blocks nested inside an input, only one parent link
    //  exists; statement blocks additionally form a vertical chain.
    //  嵌套在输入中的返回值块只有一个父链接；语句块还会形成纵向链条。
    Block *parent = nullptr;
    QString parentInput;        // set when parent is an input socket / 位于输入槽时设置
    bool parentIsStatement = false;

    //  UI state (persisted so the editor reopens exactly as it was left).
    //  界面状态（持久化，使编辑器恢复到上次离开时的样子）。
    qreal uiX = 0.0;
    qreal uiY = 0.0;
    bool uiCollapsed = false;
    QString comment;

    QString fieldValue(const QString &name) const;
    void setFieldValue(const QString &name, const QString &value);

    const QVector<Block *> &inputBlocks(const QString &name) const;
    const QVector<Block *> &statementBlocks(const QString &name) const;

    //  Returns true when the socket already holds at least one block.
    //  当槽中已至少有一个积木时返回 true。
    bool hasInput(const QString &name) const;

    void forEach(const std::function<void(Block *)> &fn);
    void forEach(const std::function<void(const Block *)> &fn) const;
    int depth() const;

    QJsonObject toJson() const;
    static std::unique_ptr<Block> fromJson(const QJsonObject &obj, QString *error = nullptr);

    //  Regenerates ids for this block and all descendants.
    //  为本积木及其所有后代重新生成 id。
    void assignIds();
    void ensureIds();

    //  Detaches from the current parent without destroying the block.
    //  从当前父节点分离，但不销毁积木。
    void detach();

private:
    QVector<Block *> m_empty;
};

// ---------------------------------------------------------------------------
//  BlockScript -- one top-level stack (a hat block plus its body)
//  积木脚本 —— 一个顶层堆栈（一个帽子块及其主体）
// ---------------------------------------------------------------------------
class BlockScript
{
public:
    BlockScript();
    explicit BlockScript(const QString &eventType);
    ~BlockScript();

    BlockScript(const BlockScript &) = delete;
    BlockScript &operator=(const BlockScript &) = delete;

    std::unique_ptr<BlockScript> clone(bool keepIds = false) const;

    QString id;
    QString name;               // user label, e.g. "on page load" / 用户标签
    QString eventType;          // block type of the hat, e.g. "events.start"
    QString pageTarget;         // page id, or empty for all pages / 页面 id
    bool enabled = true;

    //  The hat block itself (never null after construction).
    //  帽子块本身（构造后永不为空）。
    std::unique_ptr<Block> hat;

    //  Statements that hang below the hat block.
    //  挂在帽子块下方的语句。
    QVector<Block *> body;

    qreal uiX = 0.0;
    qreal uiY = 0.0;

    void addBlock(std::unique_ptr<Block> block, int index = -1);
    std::unique_ptr<Block> takeBlock(int index);
    std::unique_ptr<Block> takeBlock(Block *block);
    int indexOf(const Block *block) const;
    int blockCount() const;

    void forEach(const std::function<void(Block *)> &fn);
    void forEach(const std::function<void(const Block *)> &fn) const;

    QJsonObject toJson() const;
    static std::unique_ptr<BlockScript> fromJson(const QJsonObject &obj, QString *error = nullptr);
};

} // namespace tcbw
