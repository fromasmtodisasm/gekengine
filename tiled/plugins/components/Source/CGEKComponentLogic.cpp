#include "CGEKComponentLogic.h"
#include <algorithm>
#include <ppl.h>

BEGIN_INTERFACE_LIST(CGEKComponentLogic)
    INTERFACE_LIST_ENTRY_COM(IGEKContextUser)
    INTERFACE_LIST_ENTRY_COM(IGEKComponent)
END_INTERFACE_LIST_UNKNOWN

CGEKComponentLogic::CGEKComponentLogic(IGEKLogicSystem *pSystem, IGEKEntity *pEntity)
    : CGEKComponent(pEntity)
{
}

CGEKComponentLogic::~CGEKComponentLogic(void)
{
    m_spState = nullptr;
}

void CGEKComponentLogic::SetState(IGEKLogicState *pState)
{
    if (m_spState)
    {
        m_spState->OnExit();
    }

    m_spState = pState;
    if (m_spState)
    {
        m_spState->OnEnter(GetEntity());
    }
}

void CGEKComponentLogic::OnUpdate(float nGameTime, float nFrameTime)
{
    if (m_spState)
    {
        m_spState->OnUpdate(nGameTime, nFrameTime);
    }
}

void CGEKComponentLogic::OnRender(const frustum &kFrustum)
{
    if (m_spState)
    {
        m_spState->OnRender(kFrustum);
    }
}

STDMETHODIMP_(LPCWSTR) CGEKComponentLogic::GetType(void) const
{
    return L"logic";
}

STDMETHODIMP_(void) CGEKComponentLogic::ListProperties(std::function<void(LPCWSTR, const GEKVALUE &)> OnProperty)
{
    OnProperty(L"state", m_strDefaultState.GetString());
}

static GEKHASH gs_nState(L"state");
STDMETHODIMP_(bool) CGEKComponentLogic::GetProperty(LPCWSTR pName, GEKVALUE &kValue) const
{
    GEKHASH nHash(pName);
    if (nHash == gs_nState)
    {
        kValue = m_strDefaultState.GetString();
        return true;
    }

    return false;
}

STDMETHODIMP_(bool) CGEKComponentLogic::SetProperty(LPCWSTR pName, const GEKVALUE &kValue)
{
    GEKHASH nHash(pName);
    if (nHash == gs_nState)
    {
        m_strDefaultState = kValue.GetString();
        return true;
    }

    return false;
}

STDMETHODIMP CGEKComponentLogic::OnEntityCreated(void)
{
    HRESULT hRetVal = S_OK;
    if (!m_strDefaultState.IsEmpty())
    {
        CComPtr<IGEKLogicState> spState;
        hRetVal = GetContext()->CreateNamedInstance(m_strDefaultState, IID_PPV_ARGS(&spState));
        if (spState)
        {
            SetState(spState);
        }
    }

    return hRetVal;
}

STDMETHODIMP_(void) CGEKComponentLogic::OnEvent(LPCWSTR pAction, const GEKVALUE &kParamA, const GEKVALUE &kParamB)
{
    if (m_spState)
    {
        m_spState->OnEvent(pAction, kParamA, kParamB);
    }
}

BEGIN_INTERFACE_LIST(CGEKComponentSystemLogic)
    INTERFACE_LIST_ENTRY_COM(IGEKContextUser)
    INTERFACE_LIST_ENTRY_COM(IGEKSceneManagerUser)
    INTERFACE_LIST_ENTRY_COM(IGEKViewManagerUser)
    INTERFACE_LIST_ENTRY_COM(IGEKContextObserver)
    INTERFACE_LIST_ENTRY_COM(IGEKSceneObserver)
    INTERFACE_LIST_ENTRY_COM(IGEKComponentSystem)
    INTERFACE_LIST_ENTRY_MEMBER_COM(IGEKSceneManager, GetSceneManager())
    INTERFACE_LIST_ENTRY_MEMBER_COM(IGEKViewManager, GetViewManager())
END_INTERFACE_LIST_UNKNOWN

REGISTER_CLASS(CGEKComponentSystemLogic)

CGEKComponentSystemLogic::CGEKComponentSystemLogic(void)
{
}

CGEKComponentSystemLogic::~CGEKComponentSystemLogic(void)
{
}

STDMETHODIMP CGEKComponentSystemLogic::OnRegistration(IUnknown *pObject)
{
    HRESULT hRetVal = S_OK;
    CComQIPtr<IGEKLogicSystemUser> spUser(pObject);
    if (spUser != nullptr)
    {
        hRetVal = spUser->Register(this);
    }

    return hRetVal;
}

STDMETHODIMP CGEKComponentSystemLogic::Initialize(void)
{
    HRESULT hRetVal = CGEKObservable::AddObserver(GetContext(), (IGEKContextObserver *)this);
    if (SUCCEEDED(hRetVal))
    {
        hRetVal = CGEKObservable::AddObserver(GetSceneManager(), (IGEKSceneObserver *)this);
    }

    return hRetVal;
}

STDMETHODIMP_(void) CGEKComponentSystemLogic::Destroy(void)
{
    CGEKObservable::RemoveObserver(GetSceneManager(), (IGEKSceneObserver *)this);
    CGEKObservable::RemoveObserver(GetContext(), (IGEKContextObserver *)this);
}

STDMETHODIMP_(void) CGEKComponentSystemLogic::Clear(void)
{
    m_aComponents.clear();
}

STDMETHODIMP CGEKComponentSystemLogic::Create(const CLibXMLNode &kEntityNode, IGEKEntity *pEntity, IGEKComponent **ppComponent)
{
    HRESULT hRetVal = E_FAIL;
    if (kEntityNode.HasAttribute(L"type") && kEntityNode.GetAttribute(L"type").CompareNoCase(L"logic") == 0)
    {
        hRetVal = E_OUTOFMEMORY;
        CComPtr<CGEKComponentLogic> spComponent(new CGEKComponentLogic(this, pEntity));
        if (spComponent)
        {
            CComPtr<IUnknown> spComponentUnknown;
            spComponent->QueryInterface(IID_PPV_ARGS(&spComponentUnknown));
            if (spComponentUnknown)
            {
                GetContext()->RegisterInstance(spComponentUnknown);
            }

            hRetVal = spComponent->QueryInterface(IID_PPV_ARGS(ppComponent));
            if (SUCCEEDED(hRetVal))
            {
                kEntityNode.ListAttributes([&spComponent] (LPCWSTR pName, LPCWSTR pValue) -> void
                {
                    spComponent->SetProperty(pName, pValue);
                } );

                m_aComponents[pEntity] = spComponent;
            }
        }
    }

    return hRetVal;
}

STDMETHODIMP CGEKComponentSystemLogic::Destroy(IGEKEntity *pEntity)
{
    HRESULT hRetVal = E_FAIL;
    auto pIterator = std::find_if(m_aComponents.begin(), m_aComponents.end(), [&](std::map<IGEKEntity *, CComPtr<CGEKComponentLogic>>::value_type &kPair) -> bool
    {
        return (kPair.first == pEntity);
    });

    if (pIterator != m_aComponents.end())
    {
        m_aComponents.erase(pIterator);
        hRetVal = S_OK;
    }

    return hRetVal;
}

STDMETHODIMP_(void) CGEKComponentSystemLogic::OnPreUpdate(float nGameTime, float nFrameTime)
{
    concurrency::parallel_for_each(m_aComponents.begin(), m_aComponents.end(), [&](std::map<IGEKEntity *, CComPtr<CGEKComponentLogic>>::value_type &kPair) -> void
    {
        kPair.second->OnUpdate(nGameTime, nFrameTime);
    });
}

STDMETHODIMP_(void) CGEKComponentSystemLogic::OnRender(const frustum &kFrustum)
{
    for (auto &kPair : m_aComponents)
    //concurrency::parallel_for_each(m_aComponents.begin(), m_aComponents.end(), [&](std::map<IGEKEntity *, CComPtr<CGEKComponentLogic>>::value_type &kPair) -> void
    {
        kPair.second->OnRender(kFrustum);
    }
}

STDMETHODIMP_(void) CGEKComponentSystemLogic::SetState(IGEKEntity *pEntity, IGEKLogicState *pState)
{
    auto pIterator = std::find_if(m_aComponents.begin(), m_aComponents.end(), [&](std::map<IGEKEntity *, CComPtr<CGEKComponentLogic>>::value_type &kPair) -> bool
    {
        return (kPair.first == pEntity);
    });

    if (pIterator != m_aComponents.end())
    {
        (*pIterator).second->SetState(pState);
    }
}
