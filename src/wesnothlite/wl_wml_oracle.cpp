/**
 * wl-wml-oracle — STUB, NOT YET WORKING. NOT wired into CMake.
 *
 * Intended shape (see docs/TESTING_STRATEGY.md in the outer repo): wrap
 * serialization/{tokenizer,preprocessor,parser}.cpp and dump the parsed
 * `config` tree for a given WML file as JSON, so packages/engine's WML
 * pipeline (tokenizer -> preprocessor -> parser, incl. macro expansion) can
 * be diffed against the real engine's parse tree instead of eyeballed.
 *
 * What's missing before this does anything:
 *   - A CMake target (mirror wl-image-oracle's block in src/CMakeLists.txt:
 *     this one should be much cheaper, since it needs no SDL2/SDL2_image at
 *     all -- tokenizer/preprocessor/parser are already in wesnoth_headless
 *     and build fine under the mock-SDL headless stubs).
 *   - Actually calling into preprocessor::preprocess_file / read_gz (or
 *     whichever entry point src/serialization/preprocessor.hpp exposes) and
 *     ::parser::read() on the result, then walking the resulting `config`'s
 *     attributes and (ordered!) children to emit JSON. config's children are
 *     order-preserving key/value pairs, not a tag->list map -- see
 *     packages/engine/src/wml/config.ts's WmlConfig, which mirrors that
 *     shape deliberately so this oracle's JSON should serialise it directly.
 *   - Deciding what to do about macro-containing input that needs the
 *     campaign's #textdomain / [binary_path] search rules to resolve
 *     includes -- probably requires the same binary_paths_manager dance
 *     wl_image_oracle.cpp does for `[binary_path] path=data/core`.
 *
 * Usage (once implemented):
 *   wl-wml-oracle --data <wesnoth-root> <file.cfg>
 * would print the parsed config tree as JSON to stdout.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    std::fprintf(stderr,
        "wl-wml-oracle: STUB, not implemented yet.\n"
        "This binary is not built by CMake (no target wired up) -- see the\n"
        "TODO comment at the top of this file for what's needed.\n");
    (void)args;
    return 1;
}
