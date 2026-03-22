/* Headless stub: soundsource.cpp */
#include <memory>
#include "soundsource.hpp"
#include "config.hpp"

namespace soundsource {
sourcespec::sourcespec(const config& cfg)
    : id_(cfg["id"].str()), files_(cfg["files"].str()) {}
} // namespace soundsource
