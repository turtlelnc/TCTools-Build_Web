// ============================================================================
//  BuilderController.h -- visual editing operations behind the canvas
//  BuilderController.h —— 画布背后的可视化编辑操作
// ============================================================================
#pragma once

#include "Commands.h"
#include "DocumentNode.h"
#include "ElementCatalog.h"
#include "Page.h"
#include "ProjectDocument.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace tcbw {

class AppController;

// ---------------------------------------------------------------------------
//  BuilderController
//  Everything the drag-and-drop canvas and the property inspector need:
//  the palette, the layer tree, selection, insertion, geometry and the
//  clipboard. Every mutation goes through CommandStack so it is undoable.
//  拖拽画布与属性检查器所需的一切：素材面板、图层树、选择、插入、几何与剪贴板。
//  每次修改都经过 CommandStack，因此都可撤销。
// ---------------------------------------------------------------------------
class BuilderController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString selectedNodeId READ selectedNodeId NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedNode READ selectedNode NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedPath READ selectedPath NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList layerTree READ layerTree NOTIFY layerTreeChanged)
    Q_PROPERTY(QVariantList outlineNodes READ outlineNodes NOTIFY layerTreeChanged)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList composites READ composites NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY selectionChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(QString layoutMode READ layoutMode NOTIFY selectionChanged)
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY canvasSettingsChanged)
    Q_PROPERTY(int gridSize READ gridSize WRITE setGridSize NOTIFY canvasSettingsChanged)
    Q_PROPERTY(bool snapping READ snapping WRITE setSnapping NOTIFY canvasSettingsChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY canvasSettingsChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY canvasSettingsChanged)

public:
    explicit BuilderController(AppController *app, QObject *parent = nullptr);
    ~BuilderController() override;

    void setAppController(AppController *app);

    // --- selection ---------------------------------------------------------
    QString selectedNodeId() const { return m_selectedNodeId; }
    bool hasSelection() const { return !m_selectedNodeId.isEmpty(); }
    QVariantMap selectedNode() const;
    QVariantList selectedPath() const;
    QVariantList breadcrumbs() const;

    // --- trees -------------------------------------------------------------
    QVariantList layerTree() const;
    //  A flattened, pre-order rendering list for the design canvas. Unlike the
    //  layer tree it includes the display text and the catalogue descriptions,
    //  because the canvas draws a preview of each node rather than a row label.
    //  供设计画布使用的扁平先序渲染列表。与图层树不同，它包含显示文本与目录描述，
    //  因为画布绘制的是每个节点的预览，而不是一行标签。
    QVariantList outlineNodes() const;
    QVariantList palette() const;
    QVariantList composites() const;

    // --- clipboard ---------------------------------------------------------
    bool canPaste() const { return m_clipboard != nullptr; }

    // --- canvas ------------------------------------------------------------
    QString layoutMode() const;
    bool gridVisible() const { return m_gridVisible; }
    void setGridVisible(bool value);
    int gridSize() const { return m_gridSize; }
    void setGridSize(int value);
    bool snapping() const { return m_snapping; }
    void setSnapping(bool value);
    int canvasWidth() const;
    int canvasHeight() const;

    // -----------------------------------------------------------------------
    //  Invokable API
    //  可调用接口
    // -----------------------------------------------------------------------
    Q_INVOKABLE void selectNode(const QString &nodeId);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectParent();
    Q_INVOKABLE void selectNextSibling();
    Q_INVOKABLE void selectPreviousSibling();

    //  Inserts an element from the catalogue. `parentId` empty means the page
    //  root; `index` < 0 appends.
    //  从目录插入元素。`parentId` 为空表示页面根节点；`index` 小于 0 表示追加。
    Q_INVOKABLE QString insertElement(const QString &tag, const QString &parentId = QString(),
                                      int index = -1);
    Q_INVOKABLE QString insertComposite(const QString &compositeId,
                                        const QString &parentId = QString(), int index = -1);
    Q_INVOKABLE QString duplicateSelected();
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool deleteNode(const QString &nodeId);
    Q_INVOKABLE bool moveNode(const QString &nodeId, const QString &newParentId, int index);
    Q_INVOKABLE bool reorderSelected(int delta);

    Q_INVOKABLE bool setNodeText(const QString &nodeId, const QString &text);
    Q_INVOKABLE bool setNodeAttribute(const QString &nodeId, const QString &key,
                                      const QString &value);
    Q_INVOKABLE bool setNodeName(const QString &nodeId, const QString &name);
    Q_INVOKABLE bool setNodeStyle(const QString &nodeId, const QString &property,
                                  const QString &value,
                                  const QString &breakpoint = QString());
    Q_INVOKABLE bool setNodeLayoutMode(const QString &nodeId, const QString &mode);
    Q_INVOKABLE bool setNodeFreeRect(const QString &nodeId, double x, double y,
                                     double w, double h);
    Q_INVOKABLE bool setNodeFlag(const QString &nodeId, const QString &flag, bool value);
    Q_INVOKABLE bool setNodeClasses(const QString &nodeId, const QStringList &classes);
    Q_INVOKABLE bool wrapSelected(const QString &tag);

    Q_INVOKABLE QVariantMap nodeById(const QString &nodeId) const;
    Q_INVOKABLE QVariantMap nodeStyle(const QString &nodeId,
                                      const QString &breakpoint = QString()) const;
    Q_INVOKABLE QVariantList availableElements() const;
    Q_INVOKABLE QVariantList elementsInCategory(const QString &category) const;
    Q_INVOKABLE QVariantList searchElements(const QString &needle) const;

    Q_INVOKABLE bool copySelected();
    Q_INVOKABLE bool cutSelected();
    Q_INVOKABLE bool pasteInto(const QString &parentId = QString());
    Q_INVOKABLE void beginMacro(const QString &labelEn, const QString &labelZh);
    Q_INVOKABLE void endMacro();

    Q_INVOKABLE void setBreakpoint(const QString &key);
    Q_INVOKABLE QVariantList stylePresets() const;
    Q_INVOKABLE bool applyStylePreset(const QString &presetId);

    //  Computed geometry of a node relative to the page root, for the overlay.
    //  节点相对页面根节点的计算几何，用于叠加层。
    Q_INVOKABLE QVariantMap nodeGeometry(const QString &nodeId) const;

signals:
    void selectionChanged();
    void layerTreeChanged();
    void paletteChanged();
    void clipboardChanged();
    void canvasSettingsChanged();
    void requestRepaint();

private slots:
    void onStructureChanged();
    void onNodeChanged(const QString &nodeId);
    void onPageChanged(const QString &pageId);

private:
    Page *currentPage() const;
    CommandStack *stack() const;
    DocumentNode *node(const QString &nodeId) const;
    QString labelEn(const QString &key) const;
    QString labelZh(const QString &key) const;
    void afterMutation(bool structural);
    QVariantMap nodeToVariant(const DocumentNode *n, bool deep) const;

    AppController *m_app = nullptr;
    QString m_selectedNodeId;
    std::unique_ptr<DocumentNode> m_clipboard;
    bool m_gridVisible = false;
    int m_gridSize = 8;
    bool m_snapping = true;
    int m_macroDepth = 0;
};

} // namespace tcbw
