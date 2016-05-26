#pragma once

#include "GEK\Context\Observable.h"

namespace Gek
{
    struct ActionParam
    {
        union
        {
            bool state;
            float value;
        };

        ActionParam(bool state)
            : state(state)
        {
        }

        ActionParam(float value)
            : value(value)
        {
        }
    };

    GEK_INTERFACE(ActionObserver)
        : public Observer
    {
        virtual void onAction(const wchar_t *name, const ActionParam &param) = 0;
    };
}; // namespace Gek
