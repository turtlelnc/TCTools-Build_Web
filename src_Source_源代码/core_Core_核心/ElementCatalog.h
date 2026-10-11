// ============================================================================
//  ElementCatalog.h -- the palette of HTML elements and ready-made widgets
//  ElementCatalog.h —— HTML 元素与预制组件（控件）面板目录
// ============================================================================
#pragma once

#include "DocumentNode.h"
#include "Types.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  ElementDef
//  Metadata describing one draggable entry in the element palette.
//  描述素材面板中一个可拖拽条目的元数据。
// ---------------------------------------------------------------------------
struct ElementDef
{
    QString tag;                          // "div", "h1", ...
    QString labelEn;
    QString labelZh;
    QString groupEn;                      // sub-group inside the category
    QString groupZh;
    QString descriptionEn;
    QString descriptionZh;
    QString icon;                         // icon key resolved by the UI
    ElementCategory category = ElementCategory::Layout;
    QString defaultTextEn;
    QString defaultTextZh;
    QMap<QString, QString> defaultAttributes;
    QMap<QString, QString> defaultStyleCss;
    bool allowsChildren = true;
    bool isVoid = false;
    int order = 0;

    bool isContainer() const { return allowsChildren && !isVoid; }
};

// ---------------------------------------------------------------------------
//  CompositeDef
//  A ready-made, multi-element widget (navbar, hero, pricing card, ...).
//  一个预制多元素组件（导航栏、主视觉、价格卡片……）。
// ---------------------------------------------------------------------------
struct CompositeDef
{
    QString id;                           // stable key, e.g. "hero-center"
    QString labelEn;
    QString labelZh;
    QString groupEn;
    QString groupZh;
    QString descriptionEn;
    QString descriptionZh;
    QString icon;
    int order = 0;
};

// ---------------------------------------------------------------------------
//  ElementCatalog
//  Process-wide singleton describing everything that can be dragged onto the
//  canvas. Adding an element to the product means adding one row to
//  ElementCatalog.cpp -- no other code needs to change.
//  进程级单例，描述所有可拖拽到画布上的内容。向产品中新增元素只需要在
//  ElementCatalog.cpp 中增加一行，其他代码无需改动。
// ---------------------------------------------------------------------------
class ElementCatalog
{
public:
    static const ElementCatalog &instance();

    // --- elements ----------------------------------------------------------
    const QVector<ElementDef> &elements() const { return m_elements; }
    const ElementDef *find(const QString &tag) const;
    bool contains(const QString &tag) const { return find(tag) != nullptr; }

    QVector<const ElementDef *> byCategory(ElementCategory category) const;
    QVector<const ElementDef *> byGroup(ElementCategory category, const QString &groupEn) const;

    static QVector<ElementCategory> categories();
    QVector<QString> groupsIn(ElementCategory category) const;

    //  Creates a fresh node for `tag`, applying default text, attributes and
    //  styles from the catalog. Returns nullptr for unknown tags.
    //  依据目录中的默认文本、属性和样式为 `tag` 创建新节点；未知标签返回 nullptr。
    std::unique_ptr<DocumentNode> create(const QString &tag, bool chineseDefaults = false) const;

    // --- composites --------------------------------------------------------
    const QVector<CompositeDef> &composites() const { return m_composites; }
    const CompositeDef *findComposite(const QString &id) const;
    QVector<const CompositeDef *> compositesByGroup(const QString &groupEn) const;
    QVector<QString> compositeGroups() const;

    //  Builds the full node subtree for a ready-made widget.
    //  为预制组件构建完整的节点子树。
    std::unique_ptr<DocumentNode> createComposite(const QString &id,
                                                  bool chineseDefaults = false) const;

    // --- tag classification ------------------------------------------------
    static bool isVoidTag(const QString &tag);
    static bool isTextTag(const QString &tag);
    static bool isInlineTag(const QString &tag);
    static bool isHeadingTag(const QString &tag);
    static bool isFormControl(const QString &tag);
    static bool isMediaTag(const QString &tag);

    //  Tags that may legally appear inside <head>, used by the code generator.
    //  可以合法出现在 <head> 中的标签，供代码生成器使用。
    static bool isHeadTag(const QString &tag);

private:
    ElementCatalog();
    void registerElements();
    void registerComposites();

    QVector<ElementDef> m_elements;
    QVector<CompositeDef> m_composites;
    QHash<QString, int> m_byTag;          // tag -> index into m_elements
    QHash<QString, int> m_compositeById;  // id  -> index into m_composites
};

} // namespace tcbw
