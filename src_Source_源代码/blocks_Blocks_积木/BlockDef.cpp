// ============================================================================
//  BlockDef.cpp -- block shapes, definitions and block instances
//  BlockDef.cpp —— 积木形状、定义与实例
// ============================================================================
#include "BlockDef.h"

namespace tcbw {

namespace {

// Stable, lowercase, dash separated keys. They end up in the project JSON, so
// they must never change once released.
// 稳定的小写短横线分隔键。它们会写入工程 JSON，发布后绝不能更改。
struct ShapeName { BlockShape shape; const char *key; };

const ShapeName kShapes[] = {
    { BlockShape::Hat,      "hat" },
    { BlockShape::Stack,    "stack" },
    { BlockShape::CShape,   "c-shape" },
    { BlockShape::EShape,   "e-shape" },
    { BlockShape::Cap,      "cap" },
    { BlockShape::Reporter, "reporter" },
    { BlockShape::Boolean,  "boolean" },
    { BlockShape::Round,    "round" }
};

struct KindName { ValueKind kind; const char *key; };

const KindName kKinds[] = {
    { ValueKind::Any,      "any" },
    { ValueKind::Number,   "number" },
    { ValueKind::String,   "string" },
    { ValueKind::Boolean,  "boolean" },
    { ValueKind::Colour,   "colour" },
    { ValueKind::Element,  "element" },
    { ValueKind::List,     "list" },
    { ValueKind::Object,   "object" },
    { ValueKind::Function, "function" }
};

} // namespace

// ---------------------------------------------------------------------------
//  BlockShape <-> string
//  积木形状 <-> 字符串
// ---------------------------------------------------------------------------
QString toString(BlockShape v)
{
    for (const ShapeName &entry : kShapes) {
        if (entry.shape == v)
            return QString::fromLatin1(entry.key);
    }
    return QStringLiteral("stack");
}

BlockShape blockShapeFromString(const QString &s, BlockShape fallback)
{
    //  Tolerate aliases used by older project files and by hand written JSON.
    //  兼容旧工程文件与手写 JSON 中使用的别名。
    const QString needle = s.trimmed().toLower();
    for (const ShapeName &entry : kShapes) {
        if (needle == QString::fromLatin1(entry.key))
            return entry.shape;
    }
    if (needle == QStringLiteral("cshape") || needle == QStringLiteral("c"))
        return BlockShape::CShape;
    if (needle == QStringLiteral("eshape") || needle == QStringLiteral("e")
        || needle == QStringLiteral("if"))
        return BlockShape::EShape;
    if (needle == QStringLiteral("value") || needle == QStringLiteral("oval"))
        return BlockShape::Reporter;
    return fallback;
}

// ---------------------------------------------------------------------------
//  ValueKind <-> string
//  值种类 <-> 字符串
// ---------------------------------------------------------------------------
QString toString(ValueKind v)
{
    for (const KindName &entry : kKinds) {
        if (entry.kind == v)
            return QString::fromLatin1(entry.key);
    }
    return QStringLiteral("any");
}

ValueKind valueKindFromString(const QString &s, ValueKind fallback)
{
    const QString needle = s.trimmed().toLower();
    for (const KindName &entry : kKinds) {
        if (needle == QString::fromLatin1(entry.key))
            return entry.kind;
    }
    //  American spelling and a couple of common synonyms.
    //  美式拼写以及若干常见同义词。
    if (needle == QStringLiteral("color"))
        return ValueKind::Colour;
    if (needle == QStringLiteral("bool") || needle == QStringLiteral("truthy"))
        return ValueKind::Boolean;
    if (needle == QStringLiteral("num") || needle == QStringLiteral("int")
        || needle == QStringLiteral("float"))
        return ValueKind::Number;
    if (needle == QStringLiteral("str") || needle == QStringLiteral("text"))
        return ValueKind::String;
    if (needle == QStringLiteral("array") || needle == QStringLiteral("json"))
        return ValueKind::List;
    return fallback;
}

// ---------------------------------------------------------------------------
//  Only these three shapes may sit inside an input socket.
//  只有这三种形状可以放进输入槽中。
// ---------------------------------------------------------------------------
bool shapeReturnsValue(BlockShape shape)
{
    return shape == BlockShape::Reporter
        || shape == BlockShape::Boolean
        || shape == BlockShape::Round;
}

} // namespace tcbw
