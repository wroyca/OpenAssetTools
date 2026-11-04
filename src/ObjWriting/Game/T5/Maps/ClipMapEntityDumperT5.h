#pragma once

#include "Game/T5/T5.h"
#include "Zone/Zone.h"

#include <string>

namespace clip_map
{
    [[nodiscard]] std::string DumpEntitiesT5(const T5::clipMap_t& clipMap, const Zone& zone);
} // namespace clip_map
