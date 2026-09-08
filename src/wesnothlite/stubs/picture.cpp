/*
 * Headless stubs for picture.hpp.
 *
 * These live here rather than in misc_stubs.cpp so that a build wanting the
 * *real* image pipeline can simply drop this file: wl-image-oracle links the
 * engine's actual picture.cpp, and having the stubs in a shared translation
 * unit made that a duplicate-symbol collision. See source_lists/
 * wesnoth_oracle_stubs.
 */
#include "picture.hpp"
#include "sdl/point.hpp"
#include "filesystem.hpp"

#include <unordered_map>
#include <unordered_set>

namespace image {

static std::unordered_map<std::string, bool> s_file_cache;
static std::unordered_set<std::string> s_precached_dirs;

/*
 * Subdirectories that have been walked exhaustively into s_file_cache.
 *
 * Once a subtree is precached, a cache miss for a path under it is authoritative
 * — the file does not exist — so there is no need to search the filesystem for
 * it. That matters: terrain_builder::load_images() probes ~65k image paths while
 * parsing terrain rules, and most miss (upstream notes ~95% of rules are
 * discarded for having no valid images). Letting each of those fall through to
 * filesystem::get_binary_file_location() cost ~3.5s of the ~5.4s spent building
 * the terrain rule set.
 */
static std::unordered_set<std::string> s_precached_prefixes;

static bool under_precached_prefix(const std::string& fn)
{
    // Only names that look like files can be judged from the precache: the walk
    // records file names verbatim, so an extension-less reference (e.g. the
    // off-map "void", which resolves to a directory) would be wrongly reported
    // absent. Those are rare, so let them take the filesystem path.
    const std::size_t slash = fn.find_last_of('/');
    const std::string leaf = slash == std::string::npos ? fn : fn.substr(slash + 1);
    if(leaf.find('.') == std::string::npos) return false;

    for(const auto& p : s_precached_prefixes) {
        if(fn.size() >= p.size() && fn.compare(0, p.size(), p) == 0) return true;
    }
    return false;
}

static void precache_dir(const std::string& base, const std::string& subdir)
{
    const std::string full = base + "/" + subdir;
    if(s_precached_dirs.count(full)) return;
    s_precached_dirs.insert(full);
    if(!filesystem::is_directory(full)) return;

    std::vector<std::string> files, dirs;
    filesystem::get_files_in_dir(full, &files, &dirs,
        filesystem::name_mode::FILE_NAME_ONLY,
        filesystem::filter_mode::NO_FILTER,
        filesystem::reorder_mode::DONT_REORDER);
    for(const auto& f : files)
        s_file_cache[subdir + f] = true;
    for(const auto& d : dirs)
        precache_dir(base, subdir + d + "/");
}

bool exists(const locator& i_locator) {
    if(i_locator.get_filename().empty()) return false;
    const std::string& fn = i_locator.get_filename();
    auto it = s_file_cache.find(fn);
    if(it != s_file_cache.end()) return it->second;

    // A miss inside an exhaustively precached subtree means "absent"; skip the
    // filesystem search entirely.
    if(under_precached_prefix(fn)) {
        s_file_cache[fn] = false;
        return false;
    }

    bool found = filesystem::get_binary_file_location("images", fn).has_value();
    s_file_cache[fn] = found;
    return found;
}
void flush_cache() {}
point get_size(const locator& /*i_locator*/, bool /*skip_cache*/) { return {0, 0}; }
bool is_empty_hex(const locator& /*i_locator*/) { return false; }
bool precached_file_exists(const std::string& file) {
    auto it = s_file_cache.find(file);
    if(it != s_file_cache.end()) return it->second;
    return false;
}
void precache_file_existence(const std::string& subdir) {
    const std::size_t before = s_file_cache.size();
    for(const auto& p : filesystem::get_binary_paths("images"))
        precache_dir(p, subdir);
    // Only trust the subtree as exhaustive if the walk actually found files;
    // otherwise the directory may simply not have been readable.
    if(s_file_cache.size() > before) s_precached_prefixes.insert(subdir);
}

locator::locator(const std::string& filename)
    : type_(FILE)
    , filename_(filename)
{}

locator::locator(const std::string& filename, const std::string& modifications)
    : type_(SUB_FILE)
    , filename_(filename)
    , modifications_(modifications)
{}

locator::locator(const std::string& filename, const map_location& loc,
    int center_x, int center_y, const std::string& modifications)
    : type_(SUB_FILE)
    , filename_(filename)
    , modifications_(modifications)
    , loc_(loc)
    , center_x_(center_x)
    , center_y_(center_y)
{}

} // namespace image
