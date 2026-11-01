
"use strict";

/* =========================================
   1. DOM REFERENCES AND STATE
========================================= */

const API = {
    health: "/api/health",
    allocation: "/api/allocation/compare",
    paging: "/api/pages/compare"
};

const elements = {
    statusIndicator: document.querySelector("#status-indicator"),
    statusText: document.querySelector("#status-text"),

    allocationForm: document.querySelector("#allocation-form"),
    blockSizes: document.querySelector("#block-sizes"),
    eventType: document.querySelector("#event-type"),
    processId: document.querySelector("#process-id"),
    processSize: document.querySelector("#process-size"),
    sizeGroup: document.querySelector("#size-group"),
    addEvent: document.querySelector("#add-event"),
    clearEvents: document.querySelector("#clear-events"),
    eventList: document.querySelector("#event-list"),
    allocationButton: document.querySelector("#run-allocation"),
    allocationMessage: document.querySelector("#allocation-message"),
    allocationResults: document.querySelector("#allocation-results"),

    pagingForm: document.querySelector("#paging-form"),
    frameCount: document.querySelector("#frame-count"),
    references: document.querySelector("#references"),
    pagingButton: document.querySelector("#run-paging"),
    pagingMessage: document.querySelector("#paging-message"),
    pagingResults: document.querySelector("#paging-results")
};

let workload = [];

/* =========================================
   2. GENERAL UTILITIES
========================================= */

function escapeHTML(value) {
    return String(value).replace(/[&<>"']/g, character => {
        const entities = {
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#039;"
        };

        return entities[character];
    });
}

function formatNumber(value, digits = 2) {
    const number = Number(value);

    if (!Number.isFinite(number)) {
        return "—";
    }

    return number.toFixed(digits);
}

function formatPercent(value) {
    return `${formatNumber(Number(value) * 100, 2)}%`;
}

function showMessage(element, message, type = "error") {
    element.textContent = message;
    element.className = `message ${type}`;
}

function clearMessage(element) {
    element.textContent = "";
    element.className = "message hidden";
}

function setLoading(button, loading, originalText) {
    button.disabled = loading;
    button.textContent = loading ? "Running simulation..." : originalText;
}

function parsePositiveInteger(value, fieldName) {
    const trimmed = String(value).trim();

    if (!/^\d+$/.test(trimmed)) {
        throw new Error(`${fieldName} must be a positive integer.`);
    }

    const number = Number(trimmed);

    if (!Number.isSafeInteger(number) || number <= 0) {
        throw new Error(`${fieldName} must be greater than zero.`);
    }

    return number;
}

function parseIntegerList(value, fieldName, allowZero = false) {
    const tokens = String(value)
        .split(",")
        .map(item => item.trim());

    if (!tokens.length || tokens.some(item => item === "")) {
        throw new Error(`${fieldName} contains an empty value.`);
    }

    return tokens.map(token => {
        if (!/^\d+$/.test(token)) {
            throw new Error(`${fieldName} must contain integers only.`);
        }

        const number = Number(token);

        if (!Number.isSafeInteger(number)) {
            throw new Error(`${fieldName} contains an unsafe integer.`);
        }

        if (allowZero ? number < 0 : number <= 0) {
            throw new Error(
                allowZero
                    ? `${fieldName} cannot contain negative values.`
                    : `${fieldName} must contain positive integers.`
            );
        }

        return number;
    });
}

async function apiRequest(url, payload) {
    let response;

    try {
        response = await fetch(url, {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify(payload)
        });
    } catch {
        throw new Error(
            "Cannot connect to the API. Check that the C++ server is running."
        );
    }

    let data;

    try {
        data = await response.json();
    } catch {
        throw new Error("The API returned an invalid JSON response.");
    }

    if (!response.ok || data.success === false) {
        throw new Error(
            data.error?.message ||
            `Request failed with HTTP ${response.status}.`
        );
    }

    return data;
}

/* =========================================
   3. API HEALTH CHECK
========================================= */

async function checkAPI() {
    try {
        const response = await fetch(API.health);
        const data = await response.json();

        if (!response.ok || data.status !== "ok") {
            throw new Error("API is unavailable.");
        }

        elements.statusIndicator.className = "status-dot online";
        elements.statusText.textContent = "API Connected";
    } catch {
        elements.statusIndicator.className = "status-dot offline";
        elements.statusText.textContent = "API Offline";
    }
}

/* =========================================
   4. WORKLOAD MANAGEMENT
========================================= */

function renderWorkload() {
    if (workload.length === 0) {
        elements.eventList.innerHTML = `
            <p class="empty-message">No events added yet.</p>
        `;
        return;
    }

    elements.eventList.innerHTML = workload.map((event, index) => {
        const isAllocation = event.type === "allocate";

        return `
            <div class="event-item">
                <div class="event-item-info">
                    <span class="event-type ${event.type}">
                        ${isAllocation ? "ALLOCATE" : "DEALLOCATE"}
                    </span>

                    <span>Process ${event.processId}</span>

                    ${
                        isAllocation
                            ? `<span>${event.size} units</span>`
                            : ""
                    }
                </div>

                <button
                    type="button"
                    class="remove-event"
                    data-index="${index}"
                    aria-label="Remove event ${index + 1}"
                >
                    &times;
                </button>
            </div>
        `;
    }).join("");
}

function addWorkloadEvent() {
    try {
        const type = elements.eventType.value;

        const processId = parsePositiveInteger(
            elements.processId.value,
            "Process ID"
        );

        const event = {
            type,
            processId
        };

        if (type === "allocate") {
            event.size = parsePositiveInteger(
                elements.processSize.value,
                "Memory size"
            );
        }

        workload.push(event);
        renderWorkload();

        elements.processId.value = String(processId + 1);
    } catch (error) {
        showMessage(
            elements.allocationMessage,
            error.message
        );
    }
}

function removeWorkloadEvent(index) {
    workload.splice(index, 1);
    renderWorkload();
}

/* =========================================
   5. ALLOCATION RESULT RENDERING
========================================= */

function renderAllocationResults(data) {
    const results = data.results || [];

    if (results.length === 0) {
        throw new Error("The API returned no allocation results.");
    }

    const metric = (run, key) => run.metrics?.[key] ?? 0;

    const bestSuccess = Math.max(
        ...results.map(run => metric(run, "successRate"))
    );

    const bestUtilization = Math.max(
        ...results.map(run => metric(run, "allocatedUtilization"))
    );

    const bestExternal = Math.min(
        ...results.map(run =>
            run.metrics?.fragmentation?.externalFragmentation ?? Infinity
        )
    );

    const rows = results.map(run => {
        const metrics = run.metrics || {};
        const fragmentation = metrics.fragmentation || {};

        const successRate = metric(run, "successRate");
        const utilization = metric(run, "allocatedUtilization");
        const external = fragmentation.externalFragmentation ?? 0;

        return `
            <tr>
                <td>${escapeHTML(run.algorithm)}</td>
                <td class="${successRate === bestSuccess ? "best-value" : ""}">
                    ${formatPercent(successRate)}
                </td>
                <td class="${utilization === bestUtilization ? "best-value" : ""}">
                    ${formatPercent(utilization)}
                </td>
                <td>${metrics.successfulAllocations ?? 0}</td>
                <td>${metrics.failedAllocations ?? 0}</td>
                <td>${formatPercent(external)}</td>
            </tr>
        `;
    }).join("");

    const details = results.map(run => {
        const metrics = run.metrics || {};
        const fragmentation = metrics.fragmentation || {};

        return `
            <div class="trace-algorithm">
                <h4>${escapeHTML(run.algorithm)}</h4>

                <div class="metric-grid">
                    <div class="metric-card">
                        <span>Total Memory</span>
                        <strong>${fragmentation.totalMemory ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Allocated Memory</span>
                        <strong>${fragmentation.allocatedMemory ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Free Memory</span>
                        <strong>${fragmentation.freeMemory ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Internal Fragmentation</span>
                        <strong>${fragmentation.internalFragmentation ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Largest Free Block</span>
                        <strong>${fragmentation.largestFreeBlock ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Peak Allocated Memory</span>
                        <strong>${run.peakAllocatedMemory ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Free Blocks</span>
                        <strong>${fragmentation.freeBlockCount ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Rejected Requests</span>
                        <strong>${run.rejectedRequests ?? 0}</strong>
                    </div>

                    <div class="metric-card">
                        <span>Deallocation Misses</span>
                        <strong>${run.deallocationMisses ?? 0}</strong>
                    </div>
                </div>

                <p class="trace-summary">
                    <span>
                        Failed despite enough free memory:
                        <strong>${metrics.failedDespiteEnoughFree ?? 0}</strong>
                    </span>

                    <span>
                        Effective utilization:
                        <strong>${formatPercent(metrics.effectiveUtilization ?? 0)}</strong>
                    </span>
                </p>
            </div>
        `;
    }).join("");

    elements.allocationResults.innerHTML = `
        <div class="results-content">
            <div>
                <h4 class="results-subheading">
                    Algorithm Comparison
                </h4>

                <div class="table-wrapper">
                    <table class="data-table">
                        <thead>
                            <tr>
                                <th>Algorithm</th>
                                <th>Success Rate</th>
                                <th>Utilization</th>
                                <th>Successful</th>
                                <th>Failed</th>
                                <th>External Frag.</th>
                            </tr>
                        </thead>

                        <tbody>${rows}</tbody>
                    </table>
                </div>
            </div>

            <div>
                <h4 class="results-subheading">
                    Detailed Memory Analysis
                </h4>

                <div class="trace-section">
                    ${details}
                </div>
            </div>
        </div>
    `;
}

/* =========================================
   6. RUN ALLOCATION SIMULATION
========================================= */

async function runAllocationSimulation(event) {
    event.preventDefault();

    clearMessage(elements.allocationMessage);

    try {
        const blockSizes = parseIntegerList(
            elements.blockSizes.value,
            "Block sizes"
        );

        if (workload.length === 0) {
            throw new Error("Add at least one workload event.");
        }

        const events = workload.map(item => {
            if (item.type === "allocate") {
                return {
                    type: "allocate",
                    processId: item.processId,
                    size: item.size
                };
            }

            return {
                type: "deallocate",
                processId: item.processId
            };
        });

        const payload = {
            blockSizes,
            events
        };

        setLoading(
            elements.allocationButton,
            true,
            "Run Allocation Comparison"
        );

        const data = await apiRequest(API.allocation, payload);

        renderAllocationResults(data);

        showMessage(
            elements.allocationMessage,
            "Allocation comparison completed successfully.",
            "success"
        );
    } catch (error) {
        showMessage(
            elements.allocationMessage,
            error.message
        );
    } finally {
        setLoading(
            elements.allocationButton,
            false,
            "Run Allocation Comparison"
        );
    }
}

/* =========================================
   7. PAGE REPLACEMENT RESULT RENDERING
========================================= */

function renderPagingResults(data) {
    const results = data.results || [];

    if (results.length === 0) {
        throw new Error("The API returned no page replacement results.");
    }

    const bestFaults = Math.min(
        ...results.map(run => run.metrics?.faults ?? Infinity)
    );

    const rows = results.map(run => {
        const metrics = run.metrics || {};

        return `
            <tr>
                <td>${escapeHTML(run.algorithm)}</td>
                <td class="${metrics.faults === bestFaults ? "best-value" : ""}">
                    ${metrics.faults ?? 0}
                </td>
                <td>${metrics.hits ?? 0}</td>
                <td>${metrics.replacements ?? 0}</td>
                <td>${formatPercent(metrics.faultRatio ?? 0)}</td>
                <td>${formatPercent(metrics.hitRatio ?? 0)}</td>
            </tr>
        `;
    }).join("");

    const traces = results.map(run => {
        const steps = run.steps || [];
        const frameCount = run.frameCount || data.frameCount || 0;

        const frameHeaders = Array.from(
            { length: frameCount },
            (_, index) => `<th>Frame ${index + 1}</th>`
        ).join("");

        const stepRows = steps.map(step => {
            const frames = step.frames || [];

            const frameCells = Array.from(
                { length: frameCount },
                (_, index) => {
                    const value = frames[index];

                    return `
                        <td class="frame-cell">
                            ${value === null || value === undefined
                                ? "—"
                                : escapeHTML(value)}
                        </td>
                    `;
                }
            ).join("");

            return `
                <tr>
                    <td>${step.position + 1}</td>
                    <td><strong>${escapeHTML(step.page)}</strong></td>
                    ${frameCells}
                    <td class="${step.fault ? "fault" : "hit"}">
                        ${step.fault ? "FAULT" : "HIT"}
                    </td>
                    <td>
                        ${
                            step.evicted === null || step.evicted === undefined
                                ? "—"
                                : escapeHTML(step.evicted)
                        }
                    </td>
                </tr>
            `;
        }).join("");

        return `
            <div class="trace-algorithm">
                <h4>${escapeHTML(run.algorithm)}</h4>

                <div class="trace-summary">
                    <span>Faults: <strong>${run.metrics?.faults ?? 0}</strong></span>
                    <span>Hits: <strong>${run.metrics?.hits ?? 0}</strong></span>
                    <span>Replacements: <strong>${run.metrics?.replacements ?? 0}</strong></span>
                </div>

                <div class="table-wrapper">
                    <table class="trace-table">
                        <thead>
                            <tr>
                                <th>Step</th>
                                <th>Page</th>
                                ${frameHeaders}
                                <th>Result</th>
                                <th>Evicted</th>
                            </tr>
                        </thead>

                        <tbody>${stepRows}</tbody>
                    </table>
                </div>
            </div>
        `;
    }).join("");

    elements.pagingResults.innerHTML = `
        <div class="results-content">
            <div>
                <h4 class="results-subheading">
                    Algorithm Comparison
                </h4>

                <div class="table-wrapper">
                    <table class="data-table">
                        <thead>
                            <tr>
                                <th>Algorithm</th>
                                <th>Faults</th>
                                <th>Hits</th>
                                <th>Replacements</th>
                                <th>Fault Ratio</th>
                                <th>Hit Ratio</th>
                            </tr>
                        </thead>

                        <tbody>${rows}</tbody>
                    </table>
                </div>
            </div>

            <div>
                <h4 class="results-subheading">
                    Step-by-Step Frame Trace
                </h4>

                <div class="trace-section">
                    ${traces}
                </div>
            </div>
        </div>
    `;
}

/* =========================================
   8. RUN PAGE REPLACEMENT SIMULATION
========================================= */

async function runPagingSimulation(event) {
    event.preventDefault();

    clearMessage(elements.pagingMessage);

    try {
        const frameCount = parsePositiveInteger(
            elements.frameCount.value,
            "Frame count"
        );

        const references = parseIntegerList(
            elements.references.value,
            "Page references",
            true
        );

        const payload = {
            frameCount,
            references
        };

        setLoading(
            elements.pagingButton,
            true,
            "Run Page Replacement"
        );

        const data = await apiRequest(API.paging, payload);

        renderPagingResults(data);

        showMessage(
            elements.pagingMessage,
            "Page replacement comparison completed successfully.",
            "success"
        );
    } catch (error) {
        showMessage(
            elements.pagingMessage,
            error.message
        );
    } finally {
        setLoading(
            elements.pagingButton,
            false,
            "Run Page Replacement"
        );
    }
}

/* =========================================
   9. EVENT LISTENERS
========================================= */

elements.eventType.addEventListener("change", () => {
    const isAllocation = elements.eventType.value === "allocate";

    elements.sizeGroup.classList.toggle(
        "hidden",
        !isAllocation
    );
});

elements.addEvent.addListener?.("click", addWorkloadEvent);

elements.addEvent.addEventListener(
    "click",
    addWorkloadEvent
);

elements.clearEvents.addEventListener("click", () => {
    workload = [];
    renderWorkload();
    clearMessage(elements.allocationMessage);
});

elements.eventList.addEventListener("click", event => {
    const button = event.target.closest(".remove-event");

    if (!button) {
        return;
    }

    const index = Number(button.dataset.index);

    if (Number.isInteger(index) && index >= 0) {
        removeWorkloadEvent(index);
    }
});

elements.allocationForm.addEventListener(
    "submit",
    runAllocationSimulation
);

elements.pagingForm.addEventListener(
    "submit",
    runPagingSimulation
);

/* =========================================
   10. INITIALIZATION
========================================= */

renderWorkload();
checkAPI();