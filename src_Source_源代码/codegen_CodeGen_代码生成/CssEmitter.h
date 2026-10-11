// ============================================================================
//  CssEmitter.h -- renders styles as stylesheets
//  CssEmitter.h —— 将样式渲染为样式表
// ============================================================================
#pragma once

#include "DocumentNode.h"
#include "EmitOptions.h"
#include "Page.h"
#include "StyleSheet.h"

#include <QString>
#include <QStringList>

namespace tcbw {

// ---------------------------------------------------------------------------
//  CssEmitter
//  Emits one rule per node that carries styles, plus the responsive media
//  queries, the design-token custom properties and an optional reset.
//  为每个带样式的节点输出一条规则，并输出响应式媒体查询、设计令牌自定义属性
//  以及可选的 reset。
// ---------------------------------------------------------------------------
class CssEmitter
{
public:
    explicit CssEmitter(const EmitOptions &options = EmitOptions());

    //  Full page stylesheet: reset + tokens + node rules + media queries.
    //  完整页面样式表：reset + 令牌 + 节点规则 + 媒体查询。
    QString emitPage(const Page &page) const;

    //  Only the rules derived from the element tree.
    //  仅由元素树推导出的规则。
    QString emitTree(const Page &page) const;

    //  Design tokens emitted as `:root { --token: value; }`.
    //  以 `:root { --token: value; }` 形式输出的设计令牌。
    QString emitTokens(const Page &page) const;

    //  Opinionated, modern reset shared by every exported page.
    //  所有导出页面共用的现代 reset。
    QString emitReset() const;

    //  Accessibility helper classes referenced by generated markup.
    //  生成标记所引用的无障碍辅助类。
    QString emitAccessibilityHelpers() const;

    //  CSS for a single node and its subtree.
    //  单个节点及其子树的 CSS。
    QString emitNodeRules(const DocumentNode &node) const;

    //  Minifies CSS text (comment and whitespace removal, safe collapsing).
    //  压缩 CSS 文本（去除注释与空白，并做安全的折叠）。
    static QString minify(const QString &css);

    //  Builds the selector used for a node: `.tcbw-<slug>-<id>`.
    //  构建节点使用的选择器：`.tcbw-<slug>-<id>`。
    static QString selectorFor(const DocumentNode &node);

private:
    EmitOptions m_options;
};

} // namespace tcbw
