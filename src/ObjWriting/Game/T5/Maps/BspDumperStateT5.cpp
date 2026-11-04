#include "BspDumperStateT5.h"

using namespace bsp;

T5::BspFile& BspDumperStateT5::GetBsp(const std::string& name)
{
    return m_bsps[name];
}
