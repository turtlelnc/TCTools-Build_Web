// ============================================================================
//  DocumentNode.h -- one node of the visual page tree
//  DocumentNode.h —— 可视化页面树中的一个节点
// ============================================================================
#pragma once

#include "StyleSheet.h"
#include "Types.h"

#include <QJsonObject>
#include <QMap>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  DocumentNode
//  A node is either an HTML element, a text run, a component instance or a
//  template slot. Children are owned by the parent.
//  节点可以是 HTML 元素、文本片段、组件实例或模板占位符。子节点由父节点持有。
// ---------------------------------------------------------------------------
class DocumentNode
{
public:
    DocumentNode();
    explicit DocumentNode(const QString &tag, NodeKind kind = NodeKind::Element);
    ~DocumentNode();

    DocumentNode(const DocumentNode &) = delete;
    DocumentNode &operator=(const DocumentNode &) = delete;

    //  Deep copy including children (ids are regenerated unless keepIds).
    //  深拷贝（含子节点）；除非 keepIds 为真，否则重新生成 id。
    std::unique_ptr<DocumentNode> clone(bool keepIds = false) const;

    // --- identity / kind ---------------------------------------------------
    QString id;
    NodeKind kind = NodeKind::Element;
    QString tag = QStringLiteral("div");
    QString name;                 // layer-panel label / 图层面板标签
    QString componentId;          // when kind == Component / kind 为组件时使用

    // --- content -----------------------------------------------------------
    QString text;
    bool textIsHtml = false;
    QMap<QString, QString> attributes;

    // --- presentation ------------------------------------------------------
    ResponsiveStyle style;
    LayoutMode layoutMode = LayoutMode::Flow;
    QRectF freeRect;              // used when layoutMode == Free / 自由布局时使用
    int rotation = 0;
    QStringList classes;

    // --- editor state ------------------------------------------------------
    bool locked = false;
    bool hidden = false;
    bool expanded = true;

    // --- extension bag (bindings, AI notes, component props) ---------------
    QJsonObject data;

    // --- tree --------------------------------------------------------------
    DocumentNode *parent() const { return m_parent; }
    const std::vector<std::unique_ptr<DocumentNode>> &children() const { return m_children; }
    int childCount() const { return static_cast<int>(m_children.size()); }
    bool isLeaf() const { return m_children.empty(); }

    DocumentNode *childAt(int index) const;
    int indexInParent() const;

    //  Inserts and takes ownership. index < 0 or > count appends.
    //  插入并接管所有权；index 小于 0 或大于子节点数时追加到末尾。
    DocumentNode *insertChild(std::unique_ptr<DocumentNode> node, int index = -1);
    DocumentNode *appendChild(std::unique_ptr<DocumentNode> node);
    std::unique_ptr<DocumentNode> takeChild(int index);
    std::unique_ptr<DocumentNode> takeChild(DocumentNode *node);
    bool removeChild(DocumentNode *node);
    void clearChildren();

    //  Moves node to a new parent/index. Refuses to create a cycle.
    //  将节点移动到新的父节点/位置；拒绝形成环。
    bool moveChildTo(DocumentNode *node, DocumentNode *newParent, int index);

    // --- traversal ---------------------------------------------------------
    DocumentNode *findById(const QString &id) const;
    void forEach(const std::function<void(DocumentNode *)> &fn);
    void forEach(const std::function<void(const DocumentNode *)> &fn) const;
    int depth() const;
    bool isAncestorOf(const DocumentNode *other) const;
    QString pathToRoot() const;   // "body > section > h1" / 从根到本节点的路径

    // --- attributes --------------------------------------------------------
    QString attribute(const QString &key) const;
    void setAttribute(const QString &key, const QString &value);
    void removeAttribute(const QString &key);
    bool hasAttribute(const QString &key) const;

    //  Merged attribute map including src/href/alt surfaced from data.
    //  合并后的属性表，包含从 data 中暴露出的 src/href/alt。
    QMap<QString, QString> effectiveAttributes() const;

    // --- style convenience -------------------------------------------------
    QString styleValue(Breakpoint bp, const QString &property) const;
    void setStyleValue(const QString &property, const QString &value);
    void setStyleValue(Breakpoint bp, const QString &property, const QString &value);

    //  True for elements that may not legally contain children.
    //  对于法律上不允许包含子元素的元素返回 true。
    bool isVoidElement() const;

    //  True when the node participates in layout as a text run.
    //  当节点作为文本片段参与排版时返回 true。
    bool isTextual() const;

    // --- serialisation -----------------------------------------------------
    QJsonObject toJson() const;
    static std::unique_ptr<DocumentNode> fromJson(const QJsonObject &obj, QString *error = nullptr);

    // --- factory helpers ---------------------------------------------------
    static std::unique_ptr<DocumentNode> makeElement(const QString &tag,
                                                     const QString &id = QString());
    static std::unique_ptr<DocumentNode> makeText(const QString &text);
    static std::unique_ptr<DocumentNode> makeRoot();

    //  Assigns fresh ids to this node and every descendant that lacks one.
    //  为本节点及所有缺少 id 的后代分配新的 id。
    void ensureIds();

    //  Verifies the subtree is internally consistent.
    //  校验子树内部一致性。
    bool validate(QStringList *problems = nullptr) const;

private:
    DocumentNode *m_parent = nullptr;
    std::vector<std::unique_ptr<DocumentNode>> m_children;
};

//  Set of HTML void elements (cannot have children).
//  HTML 空元素集合（不能包含子节点）。
bool isVoidTag(const QString &tag);

//  True when the tag is allowed to carry user-visible text.
//  当标签可承载用户可见文本时返回 true。
bool isTextTag(const QString &tag);

} // namespace tcbw
