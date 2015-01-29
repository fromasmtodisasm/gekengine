#pragma once

#include "GEKContext.h"
#include "GEKSystem.h"
#include "GEKAPI.h"
#include "IGEKRenderMaterial.h"
#include "CGEKProperties.h"
#include <concurrent_vector.h>

class CGEKRenderMaterial : public CGEKUnknown
                         , public IGEKRenderMaterial
{
public:
    struct LAYER
    {
        std::vector<CComPtr<IUnknown>> m_aData;
        CGEKRenderStates m_kRenderStates;
        CGEKBlendStates m_kBlendStates;
        bool m_bFullBright;
        float4 m_nColor;

        LAYER(void)
            : m_bFullBright(false)
            , m_nColor(1.0f, 1.0f, 1.0f, 1.0f)
        {
        }
    };

private:
    IGEK3DVideoSystem *m_pVideoSystem;
    IGEKRenderSystem *m_pRenderManager;
    std::unordered_map<CStringW, LAYER> m_aLayers;

public:
    CGEKRenderMaterial(void);
    ~CGEKRenderMaterial(void);
    DECLARE_UNKNOWN(CGEKRenderMaterial);

    // IGEKUnknown
    STDMETHOD(Initialize)                   (THIS);

    // IGEKRenderMaterial
    STDMETHOD(Load)                         (THIS_ LPCWSTR pName);
    STDMETHOD_(bool, Enable)                (THIS_ IGEK3DVideoContext *pContext, LPCWSTR pLayer);
};