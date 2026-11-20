#include "QualityPolicy.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        MapQualityMetrics passing;
        MapQualityMetrics failing;
        failing.issues = {"entrances are disconnected", "castle room count is outside [4, 34]"};

        Require(passing.Passed() && !failing.Passed(), "test fixtures do not model pass and fail");
        // A failed quality check is a warning, never an error exit, in every mode.
        for (const bool strict : {false, true}) {
            Require(QualityExitCode(strict, passing) == 0, "a passing map returned a non-zero exit code");
            Require(QualityExitCode(strict, failing) == 0,
                std::string("a failed quality check became an error exit code (strict=") + (strict ? "true" : "false") + ")");
        }
        // Every failed check stays visible as a warning.
        Require(QualityWarnings(passing).empty(), "a passing map reported warnings");
        Require(QualityWarnings(failing) == failing.issues, "failed checks were not reported as warnings");
        std::cout << "QUALITY_POLICY: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "QUALITY_POLICY: FAIL " << error.what() << '\n';
        return 1;
    }
}
