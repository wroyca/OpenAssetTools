#pragma once

#include "Dumping/IZoneAssetDumperState.h"
#include "Game/T5/Bsp/BspT5.h"

#include <map>
#include <string>

namespace bsp
{
    class BspDumperStateT5 final : public IZoneAssetDumperState
    {
    public:
        T5::BspFile& GetBsp(const std::string& name);

        std::map<std::string, T5::BspFile> m_bsps;
    };
} // namespace bsp
