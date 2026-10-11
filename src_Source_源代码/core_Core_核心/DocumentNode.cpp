// ============================================================================
//  DocumentNode.cpp -- one node of the visual page tree
//  DocumentNode.cpp —— 可视化页面树中的一个节点
// ============================================================================
#include "DocumentNode.h"
#include "IdGenerator.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace tcbw {

namespace {

const QSet<QString> &voidTags()
{
    static const QSet<QString> s = {
        QStringLiteral("area"), QStringLiteral("base"), QStringLiteral("br"),
        QStringLiteral("col"), QStringLiteral("embed"), QStringLiteral("hr"),
        QStringLiteral("img"), QStringLiteral("input"), QStringLiteral("link"),
        QStringLiteral("meta"), QStringLiteral("param"), QStringLiteral("source"),
        QStringLiteral("track"), QStringLiteral("wbr")
    };
    return s;
}

const QSet<QString> &textTags()
{
    static const QSet<QString> s = {
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("p"), QStringLiteral("span"), QStringLiteral("a"),
        QStringLiteral("label"), QStringLiteral("button"), QStringLiteral("li"),
        QStringLiteral("blockquote"), QStringLiteral("figcaption"),
        QStringLiteral("strong"), QStringLiteral("em"), QStringLiteral("small"),
        QStringLiteral("code"), QStringLiteral("pre"), QStringLiteral("td"),
        QStringLiteral("th"), QStringLiteral("option"), QStringLiteral("title"),
        QStringLiteral("summary"), QStringLiteral("legend"), QStringLiteral("caption")
    };
    return s;
}

//  Strip control characters that would corrupt the generated markup.
//  去除会破坏生成标记的控制字符。
QString sanitiseText(const QString &in)
{
    QString out;
    out.reserve(in.size());
    for (const QChar c : in) {
        const ushort u = c.unicode();
        if (u == 0x09 || u == 0x0A || u == 0x0D || u >= 0x20)
            out.append(c);
    }
    return out;
}

} // namespace

bool isVoidTag(const QString &tag)
{
    return voidTags().contains(tag.trimmed().toLower());
}

bool isTextTag(const QString &tag)
{
    return textTags().contains(tag.trimmed().toLower());
}

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
DocumentNode::DocumentNode() = default;

DocumentNode::DocumentNode(const QString &tag_, NodeKind kind_)
    : id(IdGenerator::elementId())
    , kind(kind_)
    , tag(tag_.toLower())
{
    if (isVoidTag(tag))
        layoutMode = LayoutMode::Flow;
}

DocumentNode::~DocumentNode() = default;

std::unique_ptr<DocumentNode> DocumentNode::clone(bool keepIds) const
{
    auto copy = std::make_unique<DocumentNode>();
    copy->id = keepIds ? id : IdGenerator::elementId();
    copy->kind = kind;
    copy->tag = tag;
    copy->name = name;
    copy->componentId = componentId;
    copy->text = text;
    copy->textIsHtml = textIsHtml;
    copy->attributes = attributes;
    copy->style = style;
    copy->layoutMode = layoutMode;
    copy->freeRect = freeRect;
    copy->rotation = rotation;
    copy->classes = classes;
    copy->locked = locked;
    copy->hidden = hidden;
    copy->expanded = expanded;
    copy->data = data;

    for (const auto &child : m_children) {
        auto childCopy = child->clone(keepIds);
        copy->appendChild(std::move(childCopy));
    }
    return copy;
}

// ---------------------------------------------------------------------------
//  Tree manipulation
//  树操作
// ---------------------------------------------------------------------------
DocumentNode *DocumentNode::childAt(int index) const
{
    if (index < 0 || index >= childCount())
        return nullptr;
    return m_children[static_cast<size_t>(index)].get();
}

int DocumentNode::indexInParent() const
{
    if (!m_parent)
        return -1;
    const auto &siblings = m_parent->m_children;
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i].get() == this)
            return static_cast<int>(i);
    }
    return -1;
}

DocumentNode *DocumentNode::insertChild(std::unique_ptr<DocumentNode> node, int index)
{
    if (!node)
        return nullptr;

    //  A void element can never own children.
    //  空元素永远不能拥有子节点。
    if (isVoidTag(tag))
        return nullptr;

    //  Refuse to create a cycle.
    //  拒绝形成环。
    if (node.get() == this || node->isAncestorOf(this))
        return nullptr;

    DocumentNode *raw = node.get();

    //  Detach from a previous parent first so m_parent stays truthful.
    //  先从原父节点分离，保证 m_parent 始终正确。
    if (raw->m_parent) {
        auto detached = raw->m_parent->takeChild(raw);
        if (!detached)
            return nullptr;
        node = std::move(detached);
        raw = node.get();
    }

    if (index < 0 || index > childCount())
        index = childCount();

    raw->m_parent = this;
    m_children.insert(m_children.begin() + index, std::move(node));
    return raw;
}

DocumentNode *DocumentNode::appendChild(std::unique_ptr<DocumentNode> node)
{
    return insertChild(std::move(node), -1);
}

std::unique_ptr<DocumentNode> DocumentNode::takeChild(int index)
{
    if (index < 0 || index >= childCount())
        return nullptr;
    auto node = std::move(m_children[static_cast<size_t>(index)]);
    m_children.erase(m_children.begin() + index);
    if (node)
        node->m_parent = nullptr;
    return node;
}

std::unique_ptr<DocumentNode> DocumentNode::takeChild(DocumentNode *node)
{
    if (!node)
        return nullptr;
    const int index = node->indexInParent();
    if (index < 0 || m_children[static_cast<size_t>(index)].get() != node)
        return nullptr;
    return takeChild(index);
}

bool DocumentNode::removeChild(DocumentNode *node)
{
    return takeChild(node) != nullptr;
}

void DocumentNode::clearChildren()
{
    for (auto &child : m_children) {
        if (child)
            child->m_parent = nullptr;
    }
    m_children.clear();
}

bool DocumentNode::moveChildTo(DocumentNode *node, DocumentNode *newParent, int index)
{
    if (!node || !newParent)
        return false;
    if (node == newParent || node->isAncestorOf(newParent))
        return false;

    auto owned = takeChild(node);
    if (!owned)
        return false;

    //  insertChild may refuse (void parent); restore the node in that case.
    //  当父节点是空元素时 insertChild 会拒绝，此时需要还原节点。
    if (!newParent->insertChild(std::move(owned), index)) {
        insertChild(std::move(owned), -1);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Traversal
//  遍历
// ---------------------------------------------------------------------------
DocumentNode *DocumentNode::findById(const QString &needle) const
{
    if (id == needle)
        return const_cast<DocumentNode *>(this);
    for (const auto &child : m_children) {
        if (DocumentNode *hit = child->findById(needle))
            return hit;
    }
    return nullptr;
}

void DocumentNode::forEach(const std::function<void(DocumentNode *)> &fn)
{
    fn(this);
    //  Recursing through a mutable pointer selects this overload exactly.
    //  `std::function<void(DocumentNode*)>` cannot be adapted to
    //  `std::function<void(const DocumentNode*)>`, so the const overload is not
    //  even a candidate here and the call is unambiguous.
    //  通过可变指针递归会精确选中本重载。`std::function<void(DocumentNode*)>` 无法
    //  适配为 `std::function<void(const DocumentNode*)>`，因此 const 重载在此甚至不是
    //  候选项，调用没有二义性。
    for (const auto &child : m_children) {
        DocumentNode *mutableChild = child.get();
        mutableChild->forEach(fn);
    }
}

void DocumentNode::forEach(const std::function<void(const DocumentNode *)> &fn) const
{
    fn(this);
    //  `std::unique_ptr::operator->` is const-qualified but still yields a
    //  *mutable* `DocumentNode *`, which would make both overloads viable and
    //  therefore ambiguous. Binding to a `const DocumentNode *` first leaves
    //  only the const overload callable.
    //  `std::unique_ptr::operator->` 虽是 const 成员，但仍返回*可变*的
    //  `DocumentNode *`，这会使两个重载都可匹配，从而产生二义性。先绑定到
    //  `const DocumentNode *`，就只剩 const 重载可调用。
    for (const auto &child : m_children) {
        const DocumentNode *constChild = child.get();
        constChild->forEach(fn);
    }
}

int DocumentNode::depth() const
{
    int d = 0;
    const DocumentNode *p = m_parent;
    while (p) {
        ++d;
        p = p->m_parent;
    }
    return d;
}

bool DocumentNode::isAncestorOf(const DocumentNode *other) const
{
    if (!other)
        return false;
    const DocumentNode *p = other->m_parent;
    while (p) {
        if (p == this)
            return true;
        p = p->m_parent;
    }
    return false;
}

QString DocumentNode::pathToRoot() const
{
    QStringList parts;
    const DocumentNode *p = this;
    while (p) {
        QString label = p->tag;
        if (!p->classes.isEmpty())
            label += QLatin1Char('.') + p->classes.first();
        parts.prepend(label);
        p = p->m_parent;
    }
    return parts.join(QStringLiteral(" > "));
}

// ---------------------------------------------------------------------------
//  Attributes
//  属性
// ---------------------------------------------------------------------------
QString DocumentNode::attribute(const QString &key) const
{
    return attributes.value(key.trimmed().toLower());
}

void DocumentNode::setAttribute(const QString &key, const QString &value)
{
    const QString k = key.trimmed().toLower();
    if (k.isEmpty())
        return;
    if (value.isEmpty()) {
        attributes.remove(k);
        return;
    }
    attributes.insert(k, sanitiseText(value));
}

void DocumentNode::removeAttribute(const QString &key)
{
    attributes.remove(key.trimmed().toLower());
}

bool DocumentNode::hasAttribute(const QString &key) const
{
    return attributes.contains(key.trimmed().toLower());
}

QMap<QString, QString> DocumentNode::effectiveAttributes() const
{
    QMap<QString, QString> out = attributes;

    //  Surface common data-backed fields as real attributes.
    //  将常见的数据字段映射为真实属性。
    static const QVector<QPair<QString, QString>> bridged = {
        {QStringLiteral("src"), QStringLiteral("src")},
        {QStringLiteral("href"), QStringLiteral("href")},
        {QStringLiteral("alt"), QStringLiteral("alt")},
        {QStringLiteral("title"), QStringLiteral("title")},
        {QStringLiteral("placeholder"), QStringLiteral("placeholder")},
        {QStringLiteral("value"), QStringLiteral("value")},
        {QStringLiteral("type"), QStringLiteral("type")},
        {QStringLiteral("id"), QStringLiteral("id")},
        {QStringLiteral("name"), QStringLiteral("name")},
        {QStringLiteral("target"), QStringLiteral("target")},
        {QStringLiteral("action"), QStringLiteral("action")},
        {QStringLiteral("method"), QStringLiteral("method")},
        {QStringLiteral("poster"), QStringLiteral("poster")},
        {QStringLiteral("controls"), QStringLiteral("controls")},
        {QStringLiteral("autoplay"), QStringLiteral("autoplay")},
        {QStringLiteral("loop"), QStringLiteral("loop")},
        {QStringLiteral("muted"), QStringLiteral("muted")},
        {QStringLiteral("disabled"), QStringLiteral("disabled")},
        {QStringLiteral("required"), QStringLiteral("required")},
        {QStringLiteral("rows"), QStringLiteral("rows")},
        {QStringLiteral("cols"), QStringLiteral("cols")}
    };
    for (const auto &pair : bridged) {
        if (out.contains(pair.first))
            continue;
        const QJsonValue v = data.value(pair.second);
        if (v.isString() && !v.toString().isEmpty())
            out.insert(pair.first, sanitiseText(v.toString()));
        else if (v.isBool() && v.toBool())
            out.insert(pair.first, QString());
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Style
//  样式
// ---------------------------------------------------------------------------
QString DocumentNode::styleValue(Breakpoint bp, const QString &property) const
{
    return style.resolve(bp, property);
}

void DocumentNode::setStyleValue(const QString &property, const QString &value)
{
    style.base().set(property, value);
}

void DocumentNode::setStyleValue(Breakpoint bp, const QString &property, const QString &value)
{
    if (bp == Breakpoint::Desktop)
        style.base().set(property, value);
    else
        style.overrideFor(bp).set(property, value);
}

bool DocumentNode::isVoidElement() const
{
    return isVoidTag(tag);
}

bool DocumentNode::isTextual() const
{
    return kind == NodeKind::Text || isTextTag(tag);
}

// ---------------------------------------------------------------------------
//  Serialisation
//  序列化
// ---------------------------------------------------------------------------
QJsonObject DocumentNode::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("kind"), toString(kind));
    obj.insert(QStringLiteral("tag"), tag);
    if (!name.isEmpty())
        obj.insert(QStringLiteral("name"), name);
    if (!componentId.isEmpty())
        obj.insert(QStringLiteral("componentId"), componentId);
    if (!text.isEmpty())
        obj.insert(QStringLiteral("text"), text);
    if (textIsHtml)
        obj.insert(QStringLiteral("textIsHtml"), true);

    if (!attributes.isEmpty()) {
        QJsonObject attrs;
        for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it)
            attrs.insert(it.key(), it.value());
        obj.insert(QStringLiteral("attributes"), attrs);
    }

    if (!style.isEmpty())
        obj.insert(QStringLiteral("style"), style.toJson());

    obj.insert(QStringLiteral("layoutMode"), toString(layoutMode));

    if (layoutMode == LayoutMode::Free || !freeRect.isNull()) {
        QJsonObject rect;
        rect.insert(QStringLiteral("x"), freeRect.x());
        rect.insert(QStringLiteral("y"), freeRect.y());
        rect.insert(QStringLiteral("w"), freeRect.width());
        rect.insert(QStringLiteral("h"), freeRect.height());
        obj.insert(QStringLiteral("freeRect"), rect);
    }

    if (rotation != 0)
        obj.insert(QStringLiteral("rotation"), rotation);
    if (!classes.isEmpty())
        obj.insert(QStringLiteral("classes"), QJsonArray::fromStringList(classes));
    if (locked)
        obj.insert(QStringLiteral("locked"), true);
    if (hidden)
        obj.insert(QStringLiteral("hidden"), true);
    if (!expanded)
        obj.insert(QStringLiteral("expanded"), false);
    if (!data.isEmpty())
        obj.insert(QStringLiteral("data"), data);

    if (!m_children.empty()) {
        QJsonArray arr;
        for (const auto &child : m_children)
            arr.append(child->toJson());
        obj.insert(QStringLiteral("children"), arr);
    }
    return obj;
}

std::unique_ptr<DocumentNode> DocumentNode::fromJson(const QJsonObject &obj, QString *error)
{
    if (obj.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty node object / 节点对象为空");
        return nullptr;
    }

    auto node = std::make_unique<DocumentNode>();
    node->id = obj.value(QStringLiteral("id")).toString();
    if (node->id.isEmpty())
        node->id = IdGenerator::elementId();

    node->kind = nodeKindFromString(obj.value(QStringLiteral("kind")).toString());
    node->tag = obj.value(QStringLiteral("tag")).toString(QStringLiteral("div")).toLower();
    node->name = obj.value(QStringLiteral("name")).toString();
    node->componentId = obj.value(QStringLiteral("componentId")).toString();
    node->text = obj.value(QStringLiteral("text")).toString();
    node->textIsHtml = obj.value(QStringLiteral("textIsHtml")).toBool(false);

    const QJsonObject attrs = obj.value(QStringLiteral("attributes")).toObject();
    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
        node->setAttribute(it.key(), it.value().toString());

    node->style = ResponsiveStyle::fromJson(obj.value(QStringLiteral("style")).toObject());
    node->layoutMode = layoutModeFromString(obj.value(QStringLiteral("layoutMode")).toString());

    if (obj.contains(QStringLiteral("freeRect"))) {
        const QJsonObject r = obj.value(QStringLiteral("freeRect")).toObject();
        node->freeRect = QRectF(r.value(QStringLiteral("x")).toDouble(),
                                r.value(QStringLiteral("y")).toDouble(),
                                r.value(QStringLiteral("w")).toDouble(),
                                r.value(QStringLiteral("h")).toDouble());
    }

    node->rotation = obj.value(QStringLiteral("rotation")).toInt(0);
    node->locked = obj.value(QStringLiteral("locked")).toBool(false);
    node->hidden = obj.value(QStringLiteral("hidden")).toBool(false);
    node->expanded = obj.value(QStringLiteral("expanded")).toBool(true);
    node->data = obj.value(QStringLiteral("data")).toObject();

    const QJsonArray classes = obj.value(QStringLiteral("classes")).toArray();
    for (const QJsonValue &v : classes) {
        if (v.isString())
            node->classes.append(v.toString());
    }

    const QJsonArray children = obj.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &v : children) {
        QString childError;
        auto child = DocumentNode::fromJson(v.toObject(), &childError);
        if (!child) {
            if (error)
                *error = childError;
            return nullptr;
        }
        node->appendChild(std::move(child));
    }
    return node;
}

// ---------------------------------------------------------------------------
//  Factory helpers
//  工厂辅助函数
// ---------------------------------------------------------------------------
std::unique_ptr<DocumentNode> DocumentNode::makeElement(const QString &tag, const QString &id)
{
    auto node = std::make_unique<DocumentNode>(tag, NodeKind::Element);
    if (!id.isEmpty())
        node->id = id;
    return node;
}

std::unique_ptr<DocumentNode> DocumentNode::makeText(const QString &text)
{
    auto node = std::make_unique<DocumentNode>(QStringLiteral("#text"), NodeKind::Text);
    node->text = sanitiseText(text);
    return node;
}

std::unique_ptr<DocumentNode> DocumentNode::makeRoot()
{
    auto node = std::make_unique<DocumentNode>(QStringLiteral("body"), NodeKind::Root);
    node->name = QStringLiteral("Page root / 页面根");
    node->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    node->style.base().set(QStringLiteral("min-height"), QStringLiteral("100vh"));
    return node;
}

void DocumentNode::ensureIds()
{
    if (id.isEmpty())
        id = IdGenerator::elementId();
    for (const auto &child : m_children)
        child->ensureIds();
}

bool DocumentNode::validate(QStringList *problems) const
{
    QStringList local;
    QSet<QString> seen;

    std::function<void(const DocumentNode *)> walk = [&](const DocumentNode *n) {
        if (!IdGenerator::isValid(n->id))
            local << QStringLiteral("Invalid id '%1' at %2").arg(n->id, n->pathToRoot());
        if (seen.contains(n->id))
            local << QStringLiteral("Duplicate id '%1'").arg(n->id);
        seen.insert(n->id);

        if (n->tag.isEmpty())
            local << QStringLiteral("Empty tag at %1").arg(n->pathToRoot());
        if (isVoidTag(n->tag) && !n->children().empty())
            local << QStringLiteral("Void element <%1> has children").arg(n->tag);
        if (n->kind == NodeKind::Root && n->tag != QStringLiteral("body"))
            local << QStringLiteral("Root node must be <body>, found <%1>").arg(n->tag);

        for (const auto &child : n->children())
            walk(child.get());
    };
    walk(this);

    if (problems)
        *problems = local;
    return local.isEmpty();
}

} // namespace tcbw
