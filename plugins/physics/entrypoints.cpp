#include <initguid.h>
#include <cguid.h>

#include "GEK\Context\Common.h"
#include "GEK\Engine\Component.h"
#include "GEK\Engine\Processor.h"

namespace Gek
{
    DECLARE_REGISTERED_CLASS(MassImplementation);
    DECLARE_INTERFACE_IID(MassRegistration, "06FD2921-89A3-490C-BBF6-9C909D921E96");

    DECLARE_REGISTERED_CLASS(DynamicBodyImplementation);
    DECLARE_INTERFACE_IID(DynamicBodyRegistration, "5C52FBF3-261D-4016-9881-80E418C8E639");

    DECLARE_REGISTERED_CLASS(PlayerImplementation);
    DECLARE_INTERFACE_IID(PlayerRegistration, "EF85AE66-9544-4B7C-A2CF-B2410BDA63BD");

    DECLARE_REGISTERED_CLASS(NewtonProcessorImplementation);
    DECLARE_INTERFACE_IID(NewtonProcessorRegistration, "BCF7CC29-F375-4F29-9DA7-921F6A78E4E3");
}; // namespace Gek

DECLARE_CONTEXT_SOURCE(Components)
    ADD_CONTEXT_CLASS(Gek::MassRegistration, Gek::MassImplementation)
        ADD_CLASS_TYPE(Gek::ComponentType)

    ADD_CONTEXT_CLASS(Gek::DynamicBodyRegistration, Gek::DynamicBodyImplementation)
        ADD_CLASS_TYPE(Gek::ComponentType)

    ADD_CONTEXT_CLASS(Gek::PlayerRegistration, Gek::PlayerImplementation)
        ADD_CLASS_TYPE(Gek::ComponentType)

    ADD_CONTEXT_CLASS(Gek::NewtonProcessorRegistration, Gek::NewtonProcessorImplementation)
        ADD_CLASS_TYPE(Gek::ProcessorType)
END_CONTEXT_SOURCE