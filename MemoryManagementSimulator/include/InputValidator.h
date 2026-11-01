#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Types.h"

namespace memsim {

// Result of parsing user text: either a value or a human-readable error.
template <typename T>
struct Parsed {
    std::optional<T> value{};
    std::string error{};

    bool ok() const noexcept { return value.has_value(); }
    static Parsed success(T v) {
        Parsed p;
        p.value = std::move(v);
        return p;
    }
    static Parsed failure(std::string message) {
        Parsed p;
        p.error = std::move(message);
        return p;
    }
};

// Upper bounds keep sums, tables and memory use sane and rule out overflow.
struct InputLimits {
    static constexpr unsigned long long kMaxValue = 1000000000ULL;  // any size / ID / page number
    static constexpr Size kMaxBlocks = 1000;
    static constexpr Size kMaxFrames = 1000;
    static constexpr Size kMaxReferences = 10000;
};

// Pure text-to-value validation. No I/O. Accepts only plain decimal digits:
// no signs, decimals, exponents or units. Surrounding whitespace is ignored.
class InputValidator {
public:
    static std::string trim(const std::string& text);

    static Parsed<Size> parseMenuChoice(const std::string& text, Size minChoice, Size maxChoice);
    static Parsed<ProcessId> parseProcessId(const std::string& text);              // > 0
    static Parsed<Size> parseMemorySize(const std::string& text,
                                        const std::string& what = "Size");           // > 0
    static Parsed<Size> parseSplitThreshold(const std::string& text);              // > 0
    static Parsed<Size> parseFrameCount(const std::string& text);                  // 1..kMaxFrames
    // Whitespace- or comma-separated positive sizes; total must fit in Size.
    static Parsed<std::vector<Size>> parseBlockSizes(const std::string& text);
    // Whitespace- or comma-separated non-negative page numbers; blank = empty string.
    static Parsed<std::vector<PageId>> parsePageReferences(const std::string& text);

private:
    static Parsed<unsigned long long> parseWhole(const std::string& token, const std::string& what);
    static std::vector<std::string> tokenize(const std::string& text);
};

}  // namespace memsim
