/**
 * wl-image-oracle — rasterise Wesnoth image locators using the real engine.
 *
 * This is a reference implementation, not part of the game. The frontend
 * reimplements Wesnoth's image path functions in TypeScript
 * (frontend/src/board/images/), and screenshots are far too coarse to tell
 * whether that reimplementation is faithful: they catch gross faults like
 * missing team colour, and say nothing about whether a mask lands a few pixels
 * off or a crop picks the wrong region.
 *
 * So this links the engine's actual picture.cpp / image_modifications.cpp and
 * writes what upstream would draw for a given locator, as a PNG. Diff that
 * against the frontend's output for the same locator and the comparison is
 * exact.
 *
 * It is deliberately built from the same headless sources as wesnothlite, with
 * only the picture.cpp stub swapped for the real thing, so the two agree about
 * everything except image rasterisation.
 *
 * Usage:
 *   wl-image-oracle --data <path> --out <dir> [--type hexed|unscaled] [locator...]
 *
 * With no locators on the command line, one locator per line is read from
 * stdin. Output files are named by the locator's SHA-free sanitised form plus
 * an index, and a manifest.tsv maps index -> locator -> file.
 */

#include "config.hpp"
#include "filesystem.hpp"
#include "game_config.hpp"
#include "game_config_view.hpp"
#include "picture.hpp"
#include "sdl/rect.hpp"
#include "sdl/surface.hpp"
#include "sdl/texture.hpp"

#include <SDL2/SDL.h>

#include <array>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/*
 * Shims for symbols the shared engine sources reference but this tool cannot
 * meaningfully provide.
 *
 * The wl_hook_* functions normally post events into a running session's channel
 * (wesnothlite/wl_lua.cpp); there is no session here, and pulling that in would
 * drag the whole engine-driver machinery into a tool that only rasterises
 * images. video::get_renderer() is referenced by sdl/texture.cpp, which is
 * linked for picture.cpp's get_texture() path — unused here, since only the
 * surface path runs, but its symbols still have to resolve.
 */
void wl_hook_story_part(const std::string&, const std::string&, const std::string&) {}
void wl_hook_music_change(const std::string&, const std::string&) {}
int  wl_hook_message(const std::string&, const std::string&, const std::string&,
                     const std::vector<std::string>&) { return 0; }

namespace video {
SDL_Renderer* get_renderer() { return nullptr; }
}

/* Only reachable from picture.cpp's texture-drawing helpers, which this tool
   never calls — it uses the surface path exclusively. */
namespace draw {
void flipped(const texture&, const ::rect&, bool, bool) {}
void smooth_shaded(const texture&, const std::array<SDL_Vertex, 4>&) {}
}

namespace {

void usage_and_exit()
{
    std::fprintf(stderr,
        "usage: wl-image-oracle --data <wesnoth-root> --out <dir>\n"
        "                       [--type hexed|unscaled] [locator ...]\n"
        "\n"
        "Rasterises each image locator (\"path~MODS()\") the way the engine\n"
        "would draw it, and writes a PNG per locator into <dir>.\n"
        "Locators may be given as arguments or one per line on stdin.\n");
    std::exit(2);
}

/** Make a locator string safe to use as a filename. */
std::string sanitise(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for(char c : s) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') ? c : '_';
    }
    if(out.size() > 120) out.resize(120);
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    std::string data_path, out_dir;
    image::TYPE type = image::HEXED;
    std::vector<std::string> locators;

    for(int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if(a == "--data" && i + 1 < argc)      data_path = argv[++i];
        else if(a == "--out" && i + 1 < argc)  out_dir   = argv[++i];
        else if(a == "--type" && i + 1 < argc) {
            const std::string t = argv[++i];
            if(t == "hexed")          type = image::HEXED;
            else if(t == "unscaled")  type = image::UNSCALED;
            else { std::fprintf(stderr, "unknown --type: %s\n", t.c_str()); usage_and_exit(); }
        }
        else if(a == "--help" || a == "-h") usage_and_exit();
        else if(!a.empty() && a[0] == '-')  { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); usage_and_exit(); }
        else locators.push_back(a);
    }

    if(data_path.empty() || out_dir.empty()) usage_and_exit();

    if(locators.empty()) {
        std::string line;
        while(std::getline(std::cin, line)) {
            while(!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if(!line.empty()) locators.push_back(line);
        }
    }

    // SDL_image decodes via SDL surfaces; no window or renderer is needed
    // because only the surface path (get_surface) is used.

    std::string root = filesystem::normalize_path(data_path, true, true);
    if(filesystem::file_exists(root + "/cores.cfg")) {
        const auto sep = root.find_last_of('/');
        if(sep != std::string::npos) root = root.substr(0, sep);
    }
    game_config::path = root;
    filesystem::set_user_data_dir(out_dir + "/_userdata");

    // Register the core binary path. The game gets this from [binary_path] in
    // data/_main.cfg; this tool does not parse WML, and without it every image
    // lookup searches <root>/images/ instead of <root>/data/core/images/.
    config bp_cfg;
    bp_cfg.add_child("binary_path")["path"] = "data/core";
    const filesystem::binary_paths_manager binary_paths(game_config_view::wrap(bp_cfg));

    // image::HEXED clips through this mask. It normally arrives via [game_config]
    // in data/game_config.cfg; unset, get_hexmask() returns a null surface and
    // mask_surface() dereferences it. Keep in step with that file.
    if(game_config::images::terrain_mask.empty()) {
        game_config::images::terrain_mask = "terrain/alphamask.png";
    }

    if(!filesystem::is_directory(out_dir)) filesystem::make_directory(out_dir);

    const std::string manifest_path = out_dir + "/manifest.tsv";
    std::ofstream manifest(manifest_path);
    manifest << "index\tstatus\twidth\theight\tfile\tlocator\n";

    int ok = 0, failed = 0, index = 0;
    for(const std::string& loc_str : locators) {
        const int idx = index++;
        const image::locator loc(loc_str);

        surface surf = image::get_surface(loc, type);
        if(!surf) {
            manifest << idx << "\tMISSING\t0\t0\t\t" << loc_str << "\n";
            ++failed;
            continue;
        }

        char stem[160];
        std::snprintf(stem, sizeof(stem), "%05d_%s.png", idx, sanitise(loc_str).c_str());
        const std::string file = out_dir + "/" + stem;

        const auto res = image::save_image(surf, file);
        if(res != image::save_result::success) {
            manifest << idx << "\tWRITE_FAILED\t" << surf->w << "\t" << surf->h
                     << "\t\t" << loc_str << "\n";
            ++failed;
            continue;
        }

        manifest << idx << "\tOK\t" << surf->w << "\t" << surf->h << "\t"
                 << stem << "\t" << loc_str << "\n";
        ++ok;
    }

    manifest.close();
    std::fprintf(stderr, "wl-image-oracle: %d written, %d failed -> %s\n",
                 ok, failed, manifest_path.c_str());
    return failed > 0 && ok == 0 ? 1 : 0;
}
