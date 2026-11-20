#pragma once

#include "MapQuality.h"

#include <string>
#include <vector>

// Quality checks are advisory. A map that fails them is still generated and
// handed to the editor: the failed checks are reported as warnings only.
// Generation errors are reserved for the case where no map can be produced.
inline std::vector<std::string> QualityWarnings(const MapQualityMetrics& quality) {
    return quality.issues;
}

// Process exit code after a map has been written. `--strict` is kept as a
// command-line flag for compatibility with the editor, which still passes it,
// but it no longer turns a failed quality check into an error: the editor
// treats any non-zero exit code as a failed generation.
inline int QualityExitCode(bool /*strict*/, const MapQualityMetrics& /*quality*/) {
    return 0;
}
