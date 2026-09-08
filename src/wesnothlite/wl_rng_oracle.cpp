/**
 * wl-rng-oracle — STUB, NOT YET WORKING. NOT wired into CMake.
 *
 * Intended shape (see docs/TESTING_STRATEGY.md in the outer repo): wrap
 * mt_rng.cpp / random_deterministic.cpp and dump N raw MT19937 draws for a
 * given seed, as JSON, so packages/engine's MT19937 port can be checked
 * bit-exact against the real engine's generator -- this one specifically
 * must match exactly, not just approximately, since replay/sync depends on
 * it.
 *
 * What's missing before this does anything:
 *   - A CMake target. This should be the cheapest oracle to wire up: mt_rng.cpp
 *     is already in source_lists/wesnoth_headless (no SDL/display dependency
 *     at all), so the target can likely just be
 *       add_executable(wl-rng-oracle wesnothlite/wl_rng_oracle.cpp mt_rng.cpp
 *         random.cpp seed_rng.cpp ...)
 *     linked against Boost, without anything like the stub-swapping
 *     wl-image-oracle needs.
 *   - Deciding exactly which draw function to expose. Upstream's rng::mt_rng
 *     (mt_rng.cpp) wraps std::mt19937 directly and is the one to match bit-
 *     for-bit; random_deterministic.hpp/.cpp layers replay/synced-context
 *     behaviour on top and is probably out of scope for a first pass -- start
 *     with raw draws from rng::mt_rng::get_random_int / next_random directly.
 *   - Picking an output format: most likely a flat JSON array of the N draws
 *     as returned by get_next_random(), since packages/engine will want to
 *     call its own port the same N times with the same seed and compare
 *     element-by-element.
 *
 * Usage (once implemented):
 *   wl-rng-oracle --seed <uint32> --count <n>
 * would print {"seed": ..., "draws": [...]} to stdout.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    std::fprintf(stderr,
        "wl-rng-oracle: STUB, not implemented yet.\n"
        "This binary is not built by CMake (no target wired up) -- see the\n"
        "TODO comment at the top of this file for what's needed.\n");
    (void)args;
    return 1;
}
