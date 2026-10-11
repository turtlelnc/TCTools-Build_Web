// ============================================================================
//  Page.h -- one page of a project
//  Page.h —— 工程中的一个页面
// ============================================================================
#pragma once

#include "DocumentNode.h"
#include "Types.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  Page
//  Owns a page's element tree, its block scripts and its <head> metadata.
//  持有页面的元素树、积木脚本以及 <head> 元数据。
// ---------------------------------------------------------------------------
class Page
{
public:
    Page();
    explicit Page(const QString &title);
    ~Page();

    Page(const Page &) = delete;
    Page &operator=(const Page &) = delete;

    std::unique_ptr<Page> clone(bool keepIds = false) const;

    // --- identity ----------------------------------------------------------
    QString id;
    QString name;                     // layer/tab label, e.g. "index"
    QString fileName;                 // emitted file, e.g. "index.html"
    QString title;                    // <title>
    bool isHome = false;              // used as index.html / 作为首页

    // --- head metadata -----------------------------------------------------
    QString description;
    QStringList keywords;
    QString author;
    QString language = QStringLiteral("en");
    QString charset = QStringLiteral("utf-8");
    QString viewport = QStringLiteral("width=device-width, initial-scale=1");
    QString faviconUrl;
    QStringList headLinks;            // extra <link> hrefs / 额外 <link>
    QStringList headScripts;          // extra <script src> / 额外 <script>
    QString customHeadHtml;
    QString customBodyEndHtml;

    // --- theme tokens (CSS custom properties) ------------------------------
    QMap<QString, QString> cssVariables;

    // --- content -----------------------------------------------------------
    std::unique_ptr<DocumentNode> root;      // always a <body> Root node
    QVector<QString> stylesheetOrder;        // ids of style blocks / 样式块顺序

    // --- helpers -----------------------------------------------------------
    DocumentNode *findNode(const QString &nodeId) const;
    QStringList allNodeIds() const;
    int nodeCount() const;

    //  Generates a unique, human readable element class name for a node.
    //  为节点生成唯一且可读的元素类名。
    QString ensureElementClass(DocumentNode *node);

    // --- serialisation -----------------------------------------------------
    QJsonObject toJson() const;
    static std::unique_ptr<Page> fromJson(const QJsonObject &obj, QString *error = nullptr);

    static std::unique_ptr<Page> makeDefault(const QString &name = QStringLiteral("index"),
                                             bool chineseDefaults = false);

private:
    int m_classCounter = 0;
};

} // namespace tcbw
