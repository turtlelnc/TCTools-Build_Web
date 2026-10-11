// ============================================================================
//  BlockRegistry.h -- every block type the toolbox can offer
//  BlockRegistry.h —— 工具箱可提供的全部积木类型
// ============================================================================
#pragma once

#include "BlockDef.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  BlockCategory
//  One colour-coded section of the toolbox.
//  工具箱中一个带颜色的分类。
// ---------------------------------------------------------------------------
struct BlockCategory
{
    QString key;          // "control", "http", "ai", ...
    QString labelEn;
    QString labelZh;
    QString colour;       // hex, used by both toolbox and canvas / 十六进制，工具箱与画布共用
    QString icon;
    QString descriptionEn;
    QString descriptionZh;
    bool advanced = false; // hidden behind "show advanced" / 需展开高级才显示
    int order = 0;
};

// ---------------------------------------------------------------------------
//  BlockRegistry
//  Process-wide singleton. Adding a programming capability to the product
//  means adding one BlockDef and (optionally) one code-generation case.
//  进程级单例。为产品新增一种编程能力，只需增加一个 BlockDef，并（可选）
//  在代码生成器中增加一个分支。
// ---------------------------------------------------------------------------
class BlockRegistry
{
public:
    static const BlockRegistry &instance();

    // --- categories --------------------------------------------------------
    const QVector<BlockCategory> &categories() const { return m_categories; }
    const BlockCategory *category(const QString &key) const;
    QVector<const BlockCategory *> visibleCategories(bool showAdvanced) const;

    // --- definitions -------------------------------------------------------
    const QVector<BlockDef> &definitions() const { return m_defs; }
    const BlockDef *find(const QString &type) const;
    bool contains(const QString &type) const { return find(type) != nullptr; }

    QVector<const BlockDef *> byCategory(const QString &categoryKey) const;
    QVector<const BlockDef *> search(const QString &needle) const;
    QVector<const BlockDef *> hatBlocks() const;
    QVector<const BlockDef *> shadowBlocks() const;

    //  Default colour for a category (falls back to a neutral grey).
    //  分类的默认颜色（回退为中性灰）。
    QString colourForCategory(const QString &key) const;

    //  Creates a block instance pre-populated with default field values.
    //  创建已填充默认字段值的积木实例。
    std::unique_ptr<Block> createBlock(const QString &type) const;

    //  Creates the shadow value block used when an input socket is empty.
    //  创建输入槽为空时使用的影子值积木。
    std::unique_ptr<Block> createShadow(const BlockInputDef &input) const;

    // --- validation --------------------------------------------------------
    //  Checks that a block's sockets are filled with compatible blocks.
    //  校验积木的槽是否已填充且类型兼容。
    struct Diagnostic
    {
        QString blockId;
        QString messageEn;
        QString messageZh;
        bool fatal = false;
    };
    QVector<Diagnostic> validate(const Block &block) const;

private:
    BlockRegistry();
    void registerCategories();
    void registerBlocks();
    void add(BlockDef def);

    //  Convenience builders that keep the table below readable.
    //  让下方表格保持可读的便捷构造辅助。
    static BlockFieldDef textField(const QString &name, const QString &labelEn,
                                   const QString &labelZh, const QString &def = QString());
    static BlockFieldDef numberField(const QString &name, const QString &labelEn,
                                     const QString &labelZh, double def,
                                     double min = 0.0, double max = 0.0);
    static BlockFieldDef dropdownField(const QString &name, const QString &labelEn,
                                       const QString &labelZh, const QStringList &options,
                                       const QStringList &optionsZh = {},
                                       const QString &def = QString());
    static BlockFieldDef colourField(const QString &name, const QString &labelEn,
                                     const QString &labelZh, const QString &def);
    static BlockInputDef input(const QString &name, const QString &labelEn,
                               const QString &labelZh, ValueKind kind,
                               const QString &shadowType = QString(),
                               const QString &shadowValue = QString());
    static BlockStatementDef branch(const QString &name, const QString &labelEn,
                                    const QString &labelZh);

    QVector<BlockCategory> m_categories;
    QVector<BlockDef> m_defs;
    QHash<QString, int> m_byType;
    QHash<QString, int> m_categoryByKey;
};

} // namespace tcbw
