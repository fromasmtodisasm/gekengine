#pragma once

#include "GEK\Utility\Common.h"
#include "GEK\Context\ObserverInterface.h"
#include "GEK\System\VideoInterface.h"
#include "GEK\Shape\Frustum.h"

namespace Gek
{
    namespace Engine
    {
        namespace Render
        {
            DECLARE_INTERFACE_IID(Class, "97A6A7BC-B739-49D3-808F-3911AE3B8A77");

            DECLARE_INTERFACE_IID(Interface, "C851EC91-8B07-4793-B9F5-B15C54E92014") : virtual public IUnknown
            {
                STDMETHOD(initialize)                       (THIS_ IUnknown *initializerContext) PURE;

                STDMETHOD_(Handle, loadPlugin)              (THIS_ LPCWSTR fileName) PURE;
                STDMETHOD_(Handle, loadShader)              (THIS_ LPCWSTR fileName) PURE;
                STDMETHOD_(Handle, loadMaterial)            (THIS_ LPCWSTR fileName) PURE;

                STDMETHOD_(Handle, createRenderStates)              (THIS_ const Video3D::RenderStates &renderStates) PURE;
                STDMETHOD_(Handle, createDepthStates)               (THIS_ const Video3D::DepthStates &depthStates) PURE;
                STDMETHOD_(Handle, createBlendStates)               (THIS_ const Video3D::UnifiedBlendStates &blendStates) PURE;
                STDMETHOD_(Handle, createBlendStates)               (THIS_ const Video3D::IndependentBlendStates &blendStates) PURE;
                STDMETHOD_(Handle, createRenderTarget)              (THIS_ UINT32 width, UINT32 height, Video3D::Format format) PURE;
                STDMETHOD_(Handle, createDepthTarget)               (THIS_ UINT32 width, UINT32 height, Video3D::Format format) PURE;
                STDMETHOD_(Handle, createBuffer)                    (THIS_ Video3D::Format format, UINT32 count, UINT32 flags, LPCVOID staticData = nullptr) PURE;
                STDMETHOD_(Handle, loadComputeProgram)              (THIS_ LPCWSTR fileName, LPCSTR entryFunction, std::function<HRESULT(LPCSTR, std::vector<UINT8> &)> onInclude = nullptr, std::unordered_map<CStringA, CStringA> *defineList = nullptr) PURE;
                STDMETHOD_(Handle, loadPixelProgram)                (THIS_ LPCWSTR fileName, LPCSTR entryFunction, std::function<HRESULT(LPCSTR, std::vector<UINT8> &)> onInclude = nullptr, std::unordered_map<CStringA, CStringA> *defineList = nullptr) PURE;

                STDMETHOD_(void, drawPrimitive)                     (THIS_ Handle pluginHandle, Handle materialHandle, Handle vertexHandle, UINT32 vertexCount, UINT32 firstVertex) PURE;
                STDMETHOD_(void, drawIndexedPrimitive)              (THIS_ Handle pluginHandle, Handle materialHandle, Handle vertexHandle, UINT32 firstVertex, Handle indexHandle, UINT32 indexCount, UINT32 firstIndex) PURE;

                STDMETHOD_(void, drawInstancedPrimitive)            (THIS_ Handle pluginHandle, Handle materialHandle, LPCVOID instanceData, UINT32 instanceStride, UINT32 instanceCount, Handle vertexHandle, UINT32 vertexCount, UINT32 firstVertex) PURE;
                STDMETHOD_(void, drawInstancedIndexedPrimitive)     (THIS_ Handle pluginHandle, Handle materialHandle, LPCVOID instanceData, UINT32 instanceStride, UINT32 instanceCount, Handle vertexHandle, UINT32 firstVertex, Handle indexHandle, UINT32 indexCount, UINT32 firstIndex) PURE;
            };

            DECLARE_INTERFACE_IID(Observer, "16333226-FE0A-427D-A3EF-205486E1AD4D") : virtual public Gek::ObserverInterface
            {
                STDMETHOD_(void, OnRenderScene)             (THIS_ Handle cameraHandle, const Gek::Shape::Frustum &viewFrustum) { };
                STDMETHOD_(void, onRenderOverlay)           (THIS) { };
            };
        }; // namespace Render
    }; // namespace Engine
}; // namespace Gek
