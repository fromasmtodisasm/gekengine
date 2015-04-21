#include "CGEKComponentSize.h"

REGISTER_COMPONENT(size)
    REGISTER_COMPONENT_DEFAULT_VALUE(value, 1.0f)
    REGISTER_COMPONENT_DEFAULT_VALUE(minimum, 1.0f)
    REGISTER_COMPONENT_DEFAULT_VALUE(maximum, 1.0f)
    REGISTER_COMPONENT_SERIALIZE(size)
        REGISTER_COMPONENT_SERIALIZE_VALUE(value, StrFromFloat)
        REGISTER_COMPONENT_SERIALIZE_VALUE(minimum, StrFromFloat)
        REGISTER_COMPONENT_SERIALIZE_VALUE(maximum, StrFromFloat)
    REGISTER_COMPONENT_DESERIALIZE(size)
        REGISTER_COMPONENT_DESERIALIZE_VALUE(value, StrToFloat)
        REGISTER_COMPONENT_DESERIALIZE_VALUE(minimum, StrToFloat)
        REGISTER_COMPONENT_DESERIALIZE_VALUE(maximum, StrToFloat)
END_REGISTER_COMPONENT(size)
