// ============================================================================
//  JsEmitter.h -- renders block programs and interactions as JavaScript
//  JsEmitter.h —— 将积木程序与交互渲染为 JavaScript
// ============================================================================
#pragma once

#include "BlockDef.h"
#include "BlockCompiler.h"
#include "EmitOptions.h"
#include "Page.h"
#include "ProjectDocument.h"

#include <QString>

namespace tcbw {

// ---------------------------------------------------------------------------
//  JsEmitter
//  Produces the runtime that the exported page needs: a tiny DOM helper
//  library, the compiled block scripts, and the declarative interaction
//  bindings collected from node `data` fields.
//  生成导出页面所需的运行时代码：一个小型 DOM 辅助库、编译后的积木脚本，以及
//  从节点 `data` 字段收集到的声明式交互绑定。
// ---------------------------------------------------------------------------
class JsEmitter
{
public:
    explicit JsEmitter(const EmitOptions &options = EmitOptions());

    //  Complete script for one page (runtime + blocks + interactions).
    //  单个页面的完整脚本（运行时 + 积木 + 交互）。
    QString emitPage(const Page &page, const QVector<BlockScript *> &program) const;

    //  The reusable runtime helper library (`TCBW` global).
    //  可复用的运行时辅助库（`TCBW` 全局对象）。
    QString emitRuntime() const;

    //  Only the compiled block scripts.
    //  仅编译后的积木脚本。
    QString emitProgram(const QVector<BlockScript *> &program) const;

    //  Declarative event bindings stored in node data (onClick, onSubmit, ...).
    //  存储在节点 data 中的声明式事件绑定（onClick、onSubmit 等）。
    QString emitInteractions(const Page &page) const;

    //  A small, dependency-free DOM builder API used by the .js export mode.
    //  `.js` 导出模式使用的小型、无依赖 DOM 构建 API。
    QString emitDomBuilder(const Page &page) const;

    //  Embeds page data as a JSON island for the .js export mode.
    //  以 JSON 数据岛形式嵌入页面数据，供 `.js` 导出模式使用。
    QString emitPageData(const Page &page) const;

    //  Conservative JS minifier: strips comments and collapses blank lines
    //  while preserving string and template-literal contents.
    //  保守的 JS 压缩：去除注释并折叠空行，同时保留字符串与模板字面量内容。
    static QString minify(const QString &js);

    //  Wraps code in an IIFE with optional strict mode.
    //  使用 IIFE 包装代码，可选严格模式。
    static QString wrapIife(const QString &body, const QString &moduleName,
                            bool useStrict, bool esm);

private:
    EmitOptions m_options;
};

} // namespace tcbw
