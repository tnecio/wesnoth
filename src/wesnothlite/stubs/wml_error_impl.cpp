/* Headless stub: wml_error_impl.cpp
 * Provides no-op implementations for gui2::dialogs::wml_error.
 * Placed in stubs/ but includes the REAL wml_error.hpp (the stub header was
 * removed, so source-dir lookup finds the real one via the main -I path).
 */
#include "gui/dialogs/wml_error.hpp"
#include <iostream>

namespace gui2::dialogs {

wml_error::wml_error(const std::string& summary,
                     const std::string& post_summary,
                     const std::vector<std::string>& files,
                     const std::string& details)
    : modal_dialog("wml_error")
    , have_files_(!files.empty())
    , have_post_summary_(!post_summary.empty())
    , report_(details)
{
    std::cout << "[LOG] WML error: " << summary;
    if(!post_summary.empty()) std::cout << " — " << post_summary;
    if(!details.empty()) std::cout << "\n" << details;
    std::cout << "\n";
}

const std::string& wml_error::window_id() const
{
    static const std::string id("wml_error");
    return id;
}

void wml_error::pre_show() {}

void wml_error::copy_report_callback() {}

} // namespace gui2::dialogs
