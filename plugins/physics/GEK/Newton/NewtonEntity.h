#pragma once

#include <Windows.h>
#include "GEK\Engine\Entity.h"
#include <Newton.h>

namespace Gek
{
    DECLARE_INTERFACE(NewtonEntity) : virtual public IUnknown
    {
        STDMETHOD_(Entity *, getEntity)                 (THIS) const PURE;
        STDMETHOD_(NewtonBody *, getNewtonBody)         (THIS) const PURE;

        // Called before the update phase to set the frame data for the body
        // Applies to rigid and player bodies
        STDMETHOD_(void, onPreUpdate)                   (THIS_ float frameTime, int threadHandle) { };

        // Called after the update phase to react to changes in the world
        // Applies to player bodies only
        STDMETHOD_(void, onPostUpdate)                  (THIS_ float frameTime, int threadHandle) { };

        // Called when setting the transformation matrix of the body
        // Applies to rigid and player bodies
        STDMETHOD_(void, onSetTransform)                (THIS_ const float* const matrixData, int threadHandle) { };
    };
}; // namespace Gek
