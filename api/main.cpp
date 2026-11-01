
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "PerformanceAnalyzer.h"

using json = nlohmann::json;

namespace {

using memsim::Size;

json errorResponse(const std::string& code, const std::string& message) {
    return {
        {"success", false},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

Size readSize(const json& value, const std::string& field) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(field + " must be an integer.");
    }

    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();

        if (number > std::numeric_limits<Size>::max()) {
            throw std::invalid_argument(field + " is too large.");
        }

        return static_cast<Size>(number);
    }

    const auto number = value.get<std::int64_t>();

    if (number <= 0) {
        throw std::invalid_argument(field + " must be positive.");
    }

    if (static_cast<std::uint64_t>(number) >
        std::numeric_limits<Size>::max()) {
        throw std::invalid_argument(field + " is too large.");
    }

    return static_cast<Size>(number);
}

int readPositiveInt(const json& value, const std::string& field) {
    const Size number = readSize(value, field);

    if (number > static_cast<Size>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument(field + " exceeds the supported range.");
    }

    return static_cast<int>(number);
}

json serializeFragmentation(const memsim::FragmentationReport& report) {
    return {
        {"totalMemory", report.totalMemory},
        {"allocatedMemory", report.allocatedMemory},
        {"freeMemory", report.freeMemory},
        {"requestedMemory", report.requestedMemory},
        {"internalFragmentation", report.internalFragmentation},
        {"freeBlockCount", report.freeBlockCount},
        {"largestFreeBlock", report.largestFreeBlock},
        {"externalFragmentation", report.externalFragmentation}
    };
}

json serializeAllocationRun(const memsim::AllocationRunResult& run) {
    const auto& metrics = run.metrics;

    return {
        {"algorithm", memsim::toString(run.algorithm)},
        {"metrics", {
            {"totalProcesses", metrics.totalProcesses},
            {"successfulAllocations", metrics.successfulAllocations},
            {"failedAllocations", metrics.failedAllocations},
            {"failedDespiteEnoughFree", metrics.failedDespiteEnoughFree},
            {"successRate", metrics.successRate},
            {"allocatedUtilization", metrics.allocatedUtilization},
            {"effectiveUtilization", metrics.effectiveUtilization},
            {"fragmentation", serializeFragmentation(metrics.fragmentation)}
        }},
        {"rejectedRequests", run.rejectedRequests},
        {"deallocationMisses", run.deallocationMisses},
        {"peakAllocatedMemory", run.peakAllocatedMemory},
        {"failedProcessIds", run.failedProcessIds}
    };
}

json serializePageResult(const memsim::SimulationResult& result) {
    const auto& metrics = result.metrics;

    json steps = json::array();

    for (const auto& step : result.steps) {
        json frames = json::array();

        for (const auto& frame : step.frames) {
            if (frame.has_value()) {
                frames.push_back(*frame);
            } else {
                frames.push_back(nullptr);
            }
        }

        json evicted = nullptr;

        if (step.evicted.has_value()) {
            evicted = *step.evicted;
        }

        steps.push_back({
            {"position", step.position},
            {"page", step.page},
            {"fault", step.fault},
            {"evicted", evicted},
            {"frames", frames}
        });
    }

    return {
        {"algorithm", result.algorithm},
        {"frameCount", result.frameCount},
        {"metrics", {
            {"totalReferences", metrics.totalReferences},
            {"hits", metrics.hits},
            {"faults", metrics.faults},
            {"replacements", metrics.replacements},
            {"hitRatio", metrics.hitRatio},
            {"faultRatio", metrics.faultRatio}
        }},
        {"steps", steps}
    };
}

void registerRoutes(httplib::Server& server) {

    // Health check
    server.Get("/api/health", [](const httplib::Request&,
                                 httplib::Response& response) {
        response.set_content(
            json{
                {"status", "ok"},
                {"service", "Memory Management Simulator API"},
                {"version", "0.10.0"}
            }.dump(4),
            "application/json"
        );
    });

    // Memory allocation comparison
    server.Post("/api/allocation/compare",
        [](const httplib::Request& request,
           httplib::Response& response) {

        try {
            const json body = json::parse(request.body);

            if (!body.is_object()) {
                throw std::invalid_argument("Request body must be a JSON object.");
            }

            if (!body.contains("blockSizes") ||
                !body["blockSizes"].is_array() ||
                body["blockSizes"].empty()) {
                throw std::invalid_argument(
                    "blockSizes must be a non-empty array."
                );
            }

            if (!body.contains("events") ||
                !body["events"].is_array()) {
                throw std::invalid_argument(
                    "events must be an array."
                );
            }

            std::vector<Size> blockSizes;

            for (const auto& block : body["blockSizes"]) {
                blockSizes.push_back(readSize(block, "block size"));
            }

            memsim::Workload workload;

            for (const auto& event : body["events"]) {
                if (!event.is_object() ||
                    !event.contains("type") ||
                    !event.contains("processId") ||
                    !event["type"].is_string()) {
                    throw std::invalid_argument(
                        "Each event requires a string type and processId."
                    );
                }

                const int processId = readPositiveInt(
                    event["processId"], "processId"
                );

                const std::string type = event["type"].get<std::string>();

                if (type == "allocate") {
                    if (!event.contains("size")) {
                        throw std::invalid_argument(
                            "Allocation event requires size."
                        );
                    }

                    const Size size = readSize(event["size"], "size");

                    workload.push_back(
                        memsim::AllocationEvent::allocate(processId, size)
                    );
                } else if (type == "deallocate") {
                    workload.push_back(
                        memsim::AllocationEvent::deallocate(processId)
                    );
                } else {
                    throw std::invalid_argument(
                        "Event type must be allocate or deallocate."
                    );
                }
            }

            memsim::AllocatorConfig config;

            if (body.contains("config")) {
                if (!body["config"].is_object()) {
                    throw std::invalid_argument(
                        "config must be a JSON object."
                    );
                }

                const auto& configJson = body["config"];

                if (configJson.contains("splitThreshold")) {
                    config.splitThreshold = readSize(
                        configJson["splitThreshold"], "splitThreshold"
                    );
                }

                if (configJson.contains("mergeOnFree")) {
                    if (!configJson["mergeOnFree"].is_boolean()) {
                        throw std::invalid_argument(
                            "mergeOnFree must be a boolean."
                        );
                    }

                    config.mergeOnFree =
                        configJson["mergeOnFree"].get<bool>();
                }
            }

            const auto comparison =
                memsim::PerformanceAnalyzer::compareAllocation(
                    blockSizes, workload, config
                );

            json results = json::array();

            for (const auto& run : comparison.runs) {
                results.push_back(serializeAllocationRun(run));
            }

            response.set_content(
                json{
                    {"success", true},
                    {"eventCount", comparison.eventCount},
                    {"results", results}
                }.dump(4),
                "application/json"
            );

        } catch (const json::parse_error&) {
            response.status = 400;
            response.set_content(
                errorResponse("INVALID_JSON", "Malformed JSON request.").dump(4),
                "application/json"
            );
        } catch (const std::invalid_argument& error) {
            response.status = 400;
            response.set_content(
                errorResponse("INVALID_REQUEST", error.what()).dump(4),
                "application/json"
            );
        } catch (const std::exception&) {
            response.status = 500;
            response.set_content(
                errorResponse("INTERNAL_ERROR",
                              "An internal server error occurred.").dump(4),
                "application/json"
            );
        }
    });

    // Page replacement comparison
    server.Post("/api/pages/compare",
        [](const httplib::Request& request,
           httplib::Response& response) {

        try {
            const json body = json::parse(request.body);

            if (!body.is_object() ||
                !body.contains("frameCount") ||
                !body.contains("references") ||
                !body["references"].is_array()) {
                throw std::invalid_argument(
                    "frameCount and references array are required."
                );
            }

            const Size frameCount = readSize(
                body["frameCount"], "frameCount"
            );

            std::vector<memsim::PageId> references;

            for (const auto& page : body["references"]) {
                if (!page.is_number_integer()) {
                    throw std::invalid_argument(
                        "Page references must be integers."
                    );
                }

                if (page.is_number_unsigned()) {
                    const auto number = page.get<std::uint64_t>();

                    if (number > static_cast<std::uint64_t>(
                                     std::numeric_limits<int>::max())) {
                        throw std::invalid_argument(
                            "Page reference exceeds the supported range."
                        );
                    }

                    references.push_back(static_cast<int>(number));
                } else {
                    const auto number = page.get<std::int64_t>();

                    if (number < 0 ||
                        number > std::numeric_limits<int>::max()) {
                        throw std::invalid_argument(
                            "Page references must be non-negative integers."
                        );
                    }

                    references.push_back(static_cast<int>(number));
                }
            }

            const auto comparison =
                memsim::PerformanceAnalyzer::comparePageReplacement(
                    frameCount, references,
                    memsim::SimulationOptions{true}
                );

            json results = json::array();

            for (const auto& result : comparison.results) {
                results.push_back(serializePageResult(result));
            }

            response.set_content(
                json{
                    {"success", true},
                    {"frameCount", comparison.frameCount},
                    {"totalReferences", comparison.totalReferences},
                    {"results", results}
                }.dump(4),
                "application/json"
            );

        } catch (const json::parse_error&) {
            response.status = 400;
            response.set_content(
                errorResponse("INVALID_JSON", "Malformed JSON request.").dump(4),
                "application/json"
            );
        } catch (const std::invalid_argument& error) {
            response.status = 400;
            response.set_content(
                errorResponse("INVALID_REQUEST", error.what()).dump(4),
                "application/json"
            );
        } catch (const std::exception&) {
            response.status = 500;
            response.set_content(
                errorResponse("INTERNAL_ERROR",
                              "An internal server error occurred.").dump(4),
                "application/json"
            );
        }
    });
}

} // namespace

int main() {
    httplib::Server server;

    registerRoutes(server);

    int port = 8080;

    if (const char* portEnv = std::getenv("PORT")) {
        try {
            std::size_t parsedLength = 0;
            const int parsedPort = std::stoi(portEnv, &parsedLength);

            if (parsedLength != std::string(portEnv).size() ||
                parsedPort < 1 || parsedPort > 65535) {
                throw std::invalid_argument("Invalid port");
            }

            port = parsedPort;
        } catch (const std::exception&) {
            std::cerr << "Invalid PORT environment variable.\n";
            return 1;
        }
    }

    std::cout << "API server listening on port " << port << '\n';

    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to start API server.\n";
        return 1;
    }

    return 0;
}
