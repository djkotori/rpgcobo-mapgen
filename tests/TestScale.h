#pragma once

#include <cstdlib>

// Quick mode for everyday runs. When the environment variable MAPGEN_TEST_QUICK is set (and is not "0"),
// tests that run many seeds cut the number of iterations to a quarter (at least 2).
// Leave it unset for the full run before a commit.
// Use QuickSeeds() only for per-castle invariants that do not depend on the sample size.
// Statistical checks (ratios, variety counts, how often something appears) change their verdict when the
// sample shrinks, so branch on QuickMode() there instead.
inline bool QuickMode() {
    const char* value = std::getenv("MAPGEN_TEST_QUICK");
    return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

inline unsigned QuickSeeds(unsigned full) {
    if (!QuickMode()) return full;
    const unsigned reduced = full / 4;
    return reduced < 2 ? (full < 2 ? full : 2) : reduced;
}
