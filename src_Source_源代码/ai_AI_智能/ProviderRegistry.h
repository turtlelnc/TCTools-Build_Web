// ============================================================================
//  ProviderRegistry.h -- the preset catalogue of AI vendors
//  ProviderRegistry.h —— AI 厂商预设目录
// ============================================================================
#pragma once

#include "AiTypes.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  ProviderRegistry
//  Process-wide singleton holding presets for the major international and
//  Chinese AI vendors. Presets contain only public, documented information
//  (endpoint shape, auth style, model ids) -- never credentials.
//  进程级单例，保存国内外主流 AI 厂商的预设。预设只包含公开且可考证的信息
//  （接口形态、授权方式、模型标识），绝不包含任何凭据。
// ---------------------------------------------------------------------------
class ProviderRegistry
{
public:
    static const ProviderRegistry &instance();

    // --- lookup ------------------------------------------------------------
    const QVector<VendorInfo> &vendors() const { return m_vendors; }
    const VendorInfo *find(const QString &id) const;
    bool contains(const QString &id) const { return find(id) != nullptr; }

    // --- filtering ---------------------------------------------------------
    QVector<const VendorInfo *> byRegion(const QString &region) const;
    QVector<const VendorInfo *> byFlavor(ApiFlavor flavor) const;
    QVector<const VendorInfo *> supporting(AuthMode mode) const;
    QVector<const VendorInfo *> localServers() const;
    QVector<const VendorInfo *> search(const QString &needle) const;

    // --- models ------------------------------------------------------------
    //  Flat list of (vendorId, ModelInfo) across every preset.
    //  所有预设中 (vendorId, ModelInfo) 的扁平列表。
    struct ModelRef
    {
        QString vendorId;
        QString vendorLabel;
        ModelInfo model;
        bool operator<(const ModelRef &other) const
        {
            return vendorLabel + model.id < other.vendorLabel + other.model.id;
        }
    };
    QVector<ModelRef> allModels() const;
    QVector<ModelRef> modelsWithCapability(ModelCapability capability) const;
    const ModelInfo *findModel(const QString &modelId) const;

    // --- statistics used by the UI and the documentation -------------------
    int vendorCount() const { return static_cast<int>(m_vendors.size()); }
    int modelCount() const;
    int regionCount() const;
    QStringList regions() const;

    // --- custom / self-hosted ---------------------------------------------
    //  Builds a synthetic vendor entry for a user supplied OpenAI-compatible
    //  endpoint (vLLM, LM Studio, Ollama, an internal gateway, ...).
    //  为用户自建的 OpenAI 兼容端点构造合成厂商条目（vLLM、LM Studio、
    //  Ollama、内网网关等）。
    static VendorInfo makeCustomVendor(const QString &id, const QString &label,
                                       const QString &baseUrl, ApiFlavor flavor,
                                       const QString &defaultModel);

private:
    ProviderRegistry();
    void registerVendors();
    void add(const VendorInfo &vendor);

    QVector<VendorInfo> m_vendors;
    QHash<QString, int> m_byId;
};

} // namespace tcbw
