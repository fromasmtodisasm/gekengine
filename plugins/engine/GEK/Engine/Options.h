#pragma once

#include "GEK\Context\Observable.h"
#include <atlbase.h>
#include <atlstr.h>

namespace Gek
{
    GEK_INTERFACE(Options)
        : public Observable
    {
        virtual const CStringW &getValue(const wchar_t *name, const wchar_t *attribute, const CStringW &defaultValue = L"") const = 0;

        virtual void beginChanges(void) = 0;
        virtual void setValue(const wchar_t *name, const wchar_t *attribute, const wchar_t *value) = 0;
        virtual void finishChanges(bool commit) = 0;
    };

    GEK_INTERFACE(OptionsObserver)
        : public Observer
    {
        virtual void onChanged(void) = 0;
    };
}; // namespace Gek
