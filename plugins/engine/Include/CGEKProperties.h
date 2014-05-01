#pragma once

#include "GEKContext.h"
#include "GEKSystem.h"
#include "IGEKRenderManager.h"
#include "IGEKRenderFilter.h"

DECLARE_INTERFACE(IUnknown);
DECLARE_INTERFACE(IUnknown);

class CGEKRenderStates
{
protected:
    CComPtr<IUnknown> m_spRenderStates;

public:
    virtual ~CGEKRenderStates(void);

    HRESULT Load(IGEKVideoSystem *pSystem, CLibXMLNode &kNode);
    void Enable(IGEKVideoSystem *pSystem);
};

class CGEKBlendStates
{
protected:
    float4 m_nBlendFactor;
    CComPtr<IUnknown> m_spBlendStates;

public:
    virtual ~CGEKBlendStates(void);

    HRESULT Load(IGEKVideoSystem *pSystem, CLibXMLNode &kNode);
    void Enable(IGEKVideoSystem *pSystem);
};