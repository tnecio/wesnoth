/* Headless stub: storyscreen/controller.cpp
 * Provides a no-op storyscreen controller for headless builds.
 */
#include "storyscreen/controller.hpp"
#include "storyscreen/parser.hpp"
#include "variable.hpp"

namespace storyscreen {

// story_parser base class virtual method (provides vtable anchor + typeinfo)
void story_parser::resolve_wml(const vconfig& /*cfg*/)
{
}

controller::controller(const vconfig& /*data*/, const std::string& scenario_name)
    : scenario_name_(scenario_name)
    , parts_()
{}

bool controller::resolve_wml_helper(const std::string& /*key*/, const vconfig& /*node*/)
{
    return false;
}

} // namespace storyscreen
