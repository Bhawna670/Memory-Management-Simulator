#include "InputValidator.h"

#include <cctype>
#include <limits>
#include <sstream>

namespace memsim {

std::string InputValidator::trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> InputValidator::tokenize(const std::string& text) {
    std::string spaced = text;
    for (char& c : spaced) {
        if (c == ',') {
            c = ' ';
        }
    }
    std::istringstream stream(spaced);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

Parsed<unsigned long long> InputValidator::parseWhole(const std::string& token, const std::string& what) {
    const std::string text = trim(token);
    if (text.empty()) {
        return Parsed<unsigned long long>::failure(what + " is required");
    }
    if (text[0] == '-') {
        return Parsed<unsigned long long>::failure(what + " must not be negative (got '" + text + "')");
    }
    unsigned long long value = 0;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return Parsed<unsigned long long>::failure(what + " must be a whole number (got '" + text + "')");
        }
        value = value * 10 + static_cast<unsigned long long>(c - '0');
        if (value > InputLimits::kMaxValue) {
            return Parsed<unsigned long long>::failure(
                what + " is too large (maximum " + std::to_string(InputLimits::kMaxValue) + ")");
        }
    }
    return Parsed<unsigned long long>::success(value);
}

Parsed<Size> InputValidator::parseMenuChoice(const std::string& text, Size minChoice, Size maxChoice) {
    const std::string message =
        "Invalid choice. Enter a number from " + std::to_string(minChoice) + " to " + std::to_string(maxChoice);
    const Parsed<unsigned long long> whole = parseWhole(text, "Choice");
    if (!whole.ok() || *whole.value < minChoice || *whole.value > maxChoice) {
        return Parsed<Size>::failure(message);
    }
    return Parsed<Size>::success(static_cast<Size>(*whole.value));
}

Parsed<ProcessId> InputValidator::parseProcessId(const std::string& text) {
    const Parsed<unsigned long long> whole = parseWhole(text, "Process ID");
    if (!whole.ok()) {
        return Parsed<ProcessId>::failure(whole.error);
    }
    if (*whole.value == 0) {
        return Parsed<ProcessId>::failure("Process ID must be greater than 0");
    }
    return Parsed<ProcessId>::success(static_cast<ProcessId>(*whole.value));
}

Parsed<Size> InputValidator::parseMemorySize(const std::string& text, const std::string& what) {
    const Parsed<unsigned long long> whole = parseWhole(text, what);
    if (!whole.ok()) {
        return Parsed<Size>::failure(whole.error);
    }
    if (*whole.value == 0) {
        return Parsed<Size>::failure(what + " must be greater than 0");
    }
    return Parsed<Size>::success(static_cast<Size>(*whole.value));
}

Parsed<Size> InputValidator::parseSplitThreshold(const std::string& text) {
    return parseMemorySize(text, "Split threshold");
}

Parsed<Size> InputValidator::parseFrameCount(const std::string& text) {
    const Parsed<unsigned long long> whole = parseWhole(text, "Frame count");
    if (!whole.ok()) {
        return Parsed<Size>::failure(whole.error);
    }
    if (*whole.value == 0) {
        return Parsed<Size>::failure("Frame count must be at least 1");
    }
    if (*whole.value > InputLimits::kMaxFrames) {
        return Parsed<Size>::failure("Frame count must be at most " + std::to_string(InputLimits::kMaxFrames));
    }
    return Parsed<Size>::success(static_cast<Size>(*whole.value));
}

Parsed<std::vector<Size>> InputValidator::parseBlockSizes(const std::string& text) {
    const std::vector<std::string> tokens = tokenize(text);
    if (tokens.empty()) {
        return Parsed<std::vector<Size>>::failure("At least one block size is required");
    }
    if (tokens.size() > InputLimits::kMaxBlocks) {
        return Parsed<std::vector<Size>>::failure("At most " + std::to_string(InputLimits::kMaxBlocks) +
                                                  " blocks are allowed");
    }
    std::vector<Size> sizes;
    unsigned long long total = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Parsed<Size> size = parseMemorySize(tokens[i], "Block size");
        if (!size.ok()) {
            return Parsed<std::vector<Size>>::failure("Block " + std::to_string(i + 1) + ": " + size.error);
        }
        total += *size.value;
        if (total > std::numeric_limits<Size>::max()) {
            return Parsed<std::vector<Size>>::failure("Total memory is too large");
        }
        sizes.push_back(*size.value);
    }
    return Parsed<std::vector<Size>>::success(std::move(sizes));
}

Parsed<std::vector<PageId>> InputValidator::parsePageReferences(const std::string& text) {
    const std::vector<std::string> tokens = tokenize(text);
    if (tokens.size() > InputLimits::kMaxReferences) {
        return Parsed<std::vector<PageId>>::failure("At most " + std::to_string(InputLimits::kMaxReferences) +
                                                    " references are allowed");
    }
    std::vector<PageId> pages;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Parsed<unsigned long long> whole = parseWhole(tokens[i], "Page number");
        if (!whole.ok()) {
            return Parsed<std::vector<PageId>>::failure("Reference " + std::to_string(i + 1) + ": " +
                                                        whole.error);
        }
        pages.push_back(static_cast<PageId>(*whole.value));
    }
    return Parsed<std::vector<PageId>>::success(std::move(pages));
}

}  // namespace memsim
