// ============================================================================
//  HtmlEmitter.h -- renders the page tree as HTML
//  HtmlEmitter.h —— 将页面树渲染为 HTML
// ============================================================================
#pragma once

#include "DocumentNode.h"
#include "EmitOptions.h"
#include "Page.h"

#include <QString>

namespace tcbw {

// ---------------------------------------------------------------------------
//  HtmlEmitter
//  Pure, side-effect free HTML serialisation. Every string that reaches the
//  output is escaped according to its syntactic position, so a page built by a
//  beginner can never smuggle raw script into the document by accident.
//  纯函数式、无副作用的 HTML 序列化。进入输出的每个字符串都按其语法位置转义，
//  因此新手构建的页面不会意外地把原始脚本夹带进文档。
// ---------------------------------------------------------------------------
class HtmlEmitter
{
public:
    explicit HtmlEmitter(const EmitOptions &options = EmitOptions());

    // --- fragments ---------------------------------------------------------
    //  Renders only the children of the page root (i.e. the <body> content).
    //  仅渲染页面根节点的子节点（即 <body> 内容）。
    QString emitBody(const Page &page) const;

    //  Renders a single node and its subtree.
    //  渲染单个节点及其子树。
    QString emitNode(const DocumentNode &node, int depth) const;

    // --- documents ---------------------------------------------------------
    //  Renders a complete `<!DOCTYPE html>` document for one page.
    //  为单个页面渲染完整的 `<!DOCTYPE html>` 文档。
    QString emitDocument(const Page &page) const;

    //  Renders a complete document that references external .css/.js files.
    //  渲染引用外部 .css/.js 文件的完整文档。
    QString emitDocumentWithAssets(const Page &page) const;

    //  Renders a minimal host page used by the .js-only export mode.
    //  渲染 `.js` 导出模式使用的极简宿主页面。
    QString emitScriptHost(const Page &page) const;

    //  Renders an `index.html` listing when a multi-page site is emitted.
    //  多页面站点导出时渲染 `index.html` 目录页。
    QString emitSiteIndex(const QVector<Page *> &pages) const;

    // --- helpers -----------------------------------------------------------
    static QString voidTag(const QString &tag,
                           const QMap<QString, QString> &attributes);
    static QString openTag(const QString &tag,
                           const QMap<QString, QString> &attributes,
                           const QString &classAttribute,
                           const QString &styleAttribute);
    static QString styleAttributeFor(const DocumentNode &node, Breakpoint bp,
                                     const EmitOptions &options);

private:
    QString emitChildren(const DocumentNode &node, int depth) const;
    QString emitAttributes(const DocumentNode &node) const;
    QString emitInlineStyle(const DocumentNode &node) const;
    QString elementClass(const DocumentNode &node) const;

    EmitOptions m_options;
};

} // namespace tcbw
