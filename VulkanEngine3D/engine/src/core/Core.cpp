#include "vke/Core.hpp"

namespace vke {

std::string assetPath(const std::string& relative) {
#ifdef VKE_ASSET_DIR
    return std::string(VKE_ASSET_DIR) + "/" + relative;
#else
    return "assets/" + relative;
#endif
}

} // namespace vke
