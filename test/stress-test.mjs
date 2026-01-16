#!/usr/bin/env node
/**
 * Comprehensive Stress Test for REST Server
 * Tests throughput, latency, reliability, and detects anomalies
 * 
 * Usage: node stress-test.mjs [IP_ADDRESS] [DURATION_SECONDS] [CONCURRENCY]
 * Defaults: IP=192.168.1.100, Duration=10s, Concurrency=5
 */

const TARGET_IP = process.argv[2] || '192.168.1.100';
const DURATION_SECONDS = parseInt(process.argv[3]) || 10;
const CONCURRENCY = parseInt(process.argv[4]) || 5;
const BASE_URL = `http://${TARGET_IP}`;
const REQUEST_TIMEOUT_MS = 3000;

// Anomaly detection thresholds
const ANOMALY_THRESHOLDS = {
    responseTime: 500,      // ms - requests slower than this are anomalies
    burstFailures: 3,       // consecutive failures to flag
};

// ANSI colors
const c = {
    reset: '\x1b[0m',
    bright: '\x1b[1m',
    dim: '\x1b[2m',
    green: '\x1b[32m',
    red: '\x1b[31m',
    yellow: '\x1b[33m',
    cyan: '\x1b[36m',
    magenta: '\x1b[35m',
    blue: '\x1b[34m',
};

// Statistics
const stats = {
    totalRequests: 0,
    successfulRequests: 0,
    failedRequests: 0,
    timeouts: 0,
    connectionErrors: 0,
    httpErrors: 0,
    parseErrors: 0,
    totalResponseTime: 0,
    minResponseTime: Infinity,
    maxResponseTime: 0,
    responseTimes: [],
    statusCodes: {},
    errors: {},

    // Per-endpoint stats
    endpoints: {},

    // Timeline data (per-second buckets)
    timeline: [],

    // Anomaly tracking
    anomalies: [],
    consecutiveFailures: 0,
    maxConsecutiveFailures: 0,
    lastResponseTime: 0,
};

let running = true;
let startTime = 0;
let currentSecond = 0;

function getTimelineBucket() {
    const elapsed = (performance.now() - startTime) / 1000;
    const bucket = Math.floor(elapsed);

    if (bucket !== currentSecond) {
        currentSecond = bucket;
    }

    while (stats.timeline.length <= bucket) {
        stats.timeline.push({
            second: stats.timeline.length,
            requests: 0,
            successes: 0,
            failures: 0,
            totalTime: 0,
            minTime: Infinity,
            maxTime: 0,
        });
    }

    return stats.timeline[bucket];
}

function recordAnomaly(type, details) {
    const elapsed = ((performance.now() - startTime) / 1000).toFixed(2);
    stats.anomalies.push({
        time: elapsed,
        type,
        details,
    });
}

async function makeRequest(endpoint) {
    const url = `${BASE_URL}${endpoint}`;
    const requestStart = performance.now();

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);

    // Get or create endpoint stats
    if (!stats.endpoints[endpoint]) {
        stats.endpoints[endpoint] = {
            requests: 0,
            successes: 0,
            failures: 0,
            totalTime: 0,
            minTime: Infinity,
            maxTime: 0,
            times: [],
        };
    }
    const epStats = stats.endpoints[endpoint];

    try {
        const response = await fetch(url, {
            signal: controller.signal,
            headers: { 'Connection': 'close' }
        });

        clearTimeout(timeout);
        const responseTime = performance.now() - requestStart;

        // Try to consume body
        let bodyOk = true;
        try {
            await response.text();
        } catch (e) {
            bodyOk = false;
            stats.parseErrors++;
        }

        // Update global stats
        stats.totalRequests++;
        stats.totalResponseTime += responseTime;
        stats.responseTimes.push(responseTime);
        stats.minResponseTime = Math.min(stats.minResponseTime, responseTime);
        stats.maxResponseTime = Math.max(stats.maxResponseTime, responseTime);
        stats.lastResponseTime = performance.now();

        // Update endpoint stats
        epStats.requests++;
        epStats.totalTime += responseTime;
        epStats.times.push(responseTime);
        epStats.minTime = Math.min(epStats.minTime, responseTime);
        epStats.maxTime = Math.max(epStats.maxTime, responseTime);

        // Update timeline
        const bucket = getTimelineBucket();
        bucket.requests++;
        bucket.totalTime += responseTime;
        bucket.minTime = Math.min(bucket.minTime, responseTime);
        bucket.maxTime = Math.max(bucket.maxTime, responseTime);

        // Track status codes
        const statusCode = response.status;
        stats.statusCodes[statusCode] = (stats.statusCodes[statusCode] || 0) + 1;

        if (response.ok && bodyOk) {
            stats.successfulRequests++;
            epStats.successes++;
            bucket.successes++;
            stats.consecutiveFailures = 0;

            // Check for slow response anomaly
            if (responseTime > ANOMALY_THRESHOLDS.responseTime) {
                recordAnomaly('SLOW_RESPONSE', {
                    endpoint,
                    responseTime: responseTime.toFixed(1),
                    threshold: ANOMALY_THRESHOLDS.responseTime,
                });
            }
        } else {
            stats.failedRequests++;
            stats.httpErrors++;
            epStats.failures++;
            bucket.failures++;
            handleFailure(`HTTP ${statusCode}`, endpoint);
        }

        return { success: response.ok && bodyOk, responseTime, statusCode };

    } catch (error) {
        clearTimeout(timeout);
        const responseTime = performance.now() - requestStart;

        stats.totalRequests++;
        stats.failedRequests++;
        stats.totalResponseTime += responseTime;
        stats.responseTimes.push(responseTime);

        epStats.requests++;
        epStats.failures++;
        epStats.totalTime += responseTime;
        epStats.times.push(responseTime);

        const bucket = getTimelineBucket();
        bucket.requests++;
        bucket.failures++;
        bucket.totalTime += responseTime;

        let errorType = 'Unknown';
        if (error.name === 'AbortError') {
            stats.timeouts++;
            errorType = 'Timeout';
        } else if (error.cause?.code === 'ECONNREFUSED') {
            stats.connectionErrors++;
            errorType = 'ECONNREFUSED';
        } else if (error.cause?.code === 'ECONNRESET') {
            stats.connectionErrors++;
            errorType = 'ECONNRESET';
        } else if (error.cause?.code === 'UND_ERR_SOCKET') {
            stats.connectionErrors++;
            errorType = 'SOCKET_ERROR';
        } else if (error.cause?.code) {
            stats.connectionErrors++;
            errorType = error.cause.code;
        } else {
            stats.connectionErrors++;
            errorType = error.message?.substring(0, 40) || 'Unknown';
        }

        stats.errors[errorType] = (stats.errors[errorType] || 0) + 1;
        handleFailure(errorType, endpoint);

        return { success: false, responseTime, error: errorType };
    }
}

function handleFailure(errorType, endpoint) {
    stats.consecutiveFailures++;
    stats.maxConsecutiveFailures = Math.max(stats.maxConsecutiveFailures, stats.consecutiveFailures);

    // Detect burst of failures
    if (stats.consecutiveFailures === ANOMALY_THRESHOLDS.burstFailures) {
        recordAnomaly('FAILURE_BURST', {
            errorType,
            endpoint,
            consecutiveFailures: stats.consecutiveFailures,
        });
    }
}

async function worker(id, endpoints) {
    let requestIndex = id;  // Offset each worker
    while (running) {
        const endpoint = endpoints[requestIndex % endpoints.length];
        requestIndex++;
        await makeRequest(endpoint);

        // Small yield
        await new Promise(resolve => setImmediate(resolve));
    }
}

function calculatePercentile(sortedArray, percentile) {
    if (sortedArray.length === 0) return 0;
    const index = Math.ceil((percentile / 100) * sortedArray.length) - 1;
    return sortedArray[Math.max(0, index)];
}

function calculateStdDev(times, mean) {
    if (times.length < 2) return 0;
    const squareDiffs = times.map(t => Math.pow(t - mean, 2));
    const avgSquareDiff = squareDiffs.reduce((a, b) => a + b, 0) / times.length;
    return Math.sqrt(avgSquareDiff);
}

function fmt(num) {
    return num.toLocaleString('en-US');
}

function fmtMs(ms) {
    if (ms === Infinity || ms === -Infinity || isNaN(ms)) return 'N/A';
    if (ms < 1) return `${(ms * 1000).toFixed(0)}µs`;
    if (ms < 1000) return `${ms.toFixed(1)}ms`;
    return `${(ms / 1000).toFixed(2)}s`;
}

function printProgress() {
    const elapsed = (performance.now() - startTime) / 1000;
    const remaining = Math.max(0, DURATION_SECONDS - elapsed);
    const rps = stats.totalRequests / Math.max(0.1, elapsed);
    const successRate = stats.totalRequests > 0
        ? ((stats.successfulRequests / stats.totalRequests) * 100).toFixed(1)
        : '0.0';

    const pct = Math.min(100, (elapsed / DURATION_SECONDS) * 100);
    const bar = '█'.repeat(Math.floor(pct / 5)).padEnd(20, '░');

    process.stdout.write(`\r${c.dim}[${bar}]${c.reset} ` +
        `${c.cyan}${fmt(stats.totalRequests)}${c.reset} req | ` +
        `${c.green}${successRate}%${c.reset} OK | ` +
        `${c.yellow}${rps.toFixed(0)}${c.reset} rps | ` +
        `${c.dim}${remaining.toFixed(0)}s left${c.reset}   `);
}

async function fetchDeviceStatus() {
    try {
        const [socketRes, oledRes, i2cRes, timingRes] = await Promise.allSettled([
            fetch(`${BASE_URL}/api/socket-status`, { signal: AbortSignal.timeout(2000) }).then(r => r.json()),
            fetch(`${BASE_URL}/api/oled-status`, { signal: AbortSignal.timeout(2000) }).then(r => r.json()),
            fetch(`${BASE_URL}/api/i2c-status`, { signal: AbortSignal.timeout(2000) }).then(r => r.json()),
            fetch(`${BASE_URL}/api/timing`, { signal: AbortSignal.timeout(2000) }).then(r => r.json()),
        ]);

        return {
            sockets: socketRes.status === 'fulfilled' ? socketRes.value : null,
            oled: oledRes.status === 'fulfilled' ? oledRes.value : null,
            i2c: i2cRes.status === 'fulfilled' ? i2cRes.value : null,
            timing: timingRes.status === 'fulfilled' ? timingRes.value : null,
        };
    } catch {
        return null;
    }
}

function printHistogram(times, width = 40) {
    if (times.length === 0) return;

    // Create buckets
    const sorted = [...times].sort((a, b) => a - b);
    const min = sorted[0];
    const max = sorted[sorted.length - 1];
    const bucketCount = 10;
    const bucketSize = (max - min) / bucketCount || 1;

    const buckets = Array(bucketCount).fill(0);
    for (const t of times) {
        const bucket = Math.min(bucketCount - 1, Math.floor((t - min) / bucketSize));
        buckets[bucket]++;
    }

    const maxCount = Math.max(...buckets);

    console.log(`\n${c.cyan}━━━ Response Time Distribution ━━━${c.reset}`);
    for (let i = 0; i < bucketCount; i++) {
        const rangeStart = min + i * bucketSize;
        const rangeEnd = min + (i + 1) * bucketSize;
        const count = buckets[i];
        const barLen = Math.round((count / maxCount) * width);
        const bar = '▓'.repeat(barLen);
        const pct = ((count / times.length) * 100).toFixed(0);

        console.log(`  ${fmtMs(rangeStart).padStart(8)} - ${fmtMs(rangeEnd).padEnd(8)} │${c.green}${bar.padEnd(width)}${c.reset}│ ${fmt(count).padStart(6)} (${pct.padStart(2)}%)`);
    }
}

function printTimeline() {
    if (stats.timeline.length === 0) return;

    console.log(`\n${c.cyan}━━━ Timeline (per second) ━━━${c.reset}`);
    console.log(`  ${c.dim}Second │ Requests │ Success │ Failed │ Avg Time │ Max Time${c.reset}`);
    console.log(`  ${c.dim}───────┼──────────┼─────────┼────────┼──────────┼─────────${c.reset}`);

    for (const bucket of stats.timeline) {
        if (bucket.requests === 0) continue;

        const avgTime = bucket.totalTime / bucket.requests;
        const successRate = bucket.requests > 0 ? (bucket.successes / bucket.requests * 100) : 0;
        const successColor = successRate >= 95 ? c.green : successRate >= 80 ? c.yellow : c.red;

        console.log(`  ${String(bucket.second).padStart(6)} │ ${fmt(bucket.requests).padStart(8)} │ ` +
            `${successColor}${fmt(bucket.successes).padStart(7)}${c.reset} │ ` +
            `${bucket.failures > 0 ? c.red : ''}${fmt(bucket.failures).padStart(6)}${c.reset} │ ` +
            `${fmtMs(avgTime).padStart(8)} │ ${fmtMs(bucket.maxTime).padStart(8)}`);
    }
}

function printAnomalies() {
    if (stats.anomalies.length === 0) {
        console.log(`  ${c.green}✓ No anomalies detected${c.reset}`);
        return;
    }

    console.log(`  ${c.yellow}⚠ ${stats.anomalies.length} anomalies detected:${c.reset}`);

    // Group by type
    const byType = {};
    for (const a of stats.anomalies) {
        if (!byType[a.type]) byType[a.type] = [];
        byType[a.type].push(a);
    }

    for (const [type, anomalies] of Object.entries(byType)) {
        const icon = type.includes('SLOW') ? '🐢' : type.includes('FAILURE') ? '💥' : '⚡';
        console.log(`\n  ${icon} ${c.yellow}${type}${c.reset} (${anomalies.length} occurrences)`);

        // Show first 5
        const shown = anomalies.slice(0, 5);
        for (const a of shown) {
            const details = Object.entries(a.details)
                .map(([k, v]) => `${k}=${v}`)
                .join(', ');
            console.log(`     ${c.dim}@${a.time}s${c.reset} - ${details}`);
        }

        if (anomalies.length > 5) {
            console.log(`     ${c.dim}... and ${anomalies.length - 5} more${c.reset}`);
        }
    }
}

function printEndpointStats() {
    console.log(`\n${c.cyan}━━━ Per-Endpoint Statistics ━━━${c.reset}`);
    console.log(`  ${c.dim}Endpoint                    │ Requests │ Success │  Avg   │  p95   │  Max${c.reset}`);
    console.log(`  ${c.dim}────────────────────────────┼──────────┼─────────┼────────┼────────┼────────${c.reset}`);

    for (const [endpoint, ep] of Object.entries(stats.endpoints).sort((a, b) => b[1].requests - a[1].requests)) {
        const avg = ep.requests > 0 ? ep.totalTime / ep.requests : 0;
        const sorted = [...ep.times].sort((a, b) => a - b);
        const p95 = calculatePercentile(sorted, 95);
        const successRate = ep.requests > 0 ? (ep.successes / ep.requests * 100) : 0;
        const successColor = successRate >= 95 ? c.green : successRate >= 80 ? c.yellow : c.red;

        console.log(`  ${endpoint.padEnd(27)} │ ${fmt(ep.requests).padStart(8)} │ ` +
            `${successColor}${successRate.toFixed(0).padStart(6)}%${c.reset} │ ` +
            `${fmtMs(avg).padStart(6)} │ ${fmtMs(p95).padStart(6)} │ ${fmtMs(ep.maxTime).padStart(6)}`);
    }
}

async function main() {
    console.log(`\n${c.bright}╔═══════════════════════════════════════════════════════════╗${c.reset}`);
    console.log(`${c.bright}║          REST Server Comprehensive Stress Test            ║${c.reset}`);
    console.log(`${c.bright}╚═══════════════════════════════════════════════════════════╝${c.reset}`);
    console.log(`${c.cyan}ℹ${c.reset} Target:      ${c.bright}${BASE_URL}${c.reset}`);
    console.log(`${c.cyan}ℹ${c.reset} Duration:    ${DURATION_SECONDS} seconds`);
    console.log(`${c.cyan}ℹ${c.reset} Concurrency: ${CONCURRENCY} workers`);
    console.log(`${c.cyan}ℹ${c.reset} Timeout:     ${REQUEST_TIMEOUT_MS}ms per request`);

    // Fetch initial device status
    console.log(`\n${c.dim}Connecting and fetching device status...${c.reset}`);
    const statusBefore = await fetchDeviceStatus();

    // Test connection
    const testResult = await makeRequest('/ping');
    if (!testResult.success) {
        console.log(`${c.red}✗${c.reset} Cannot connect to ${BASE_URL}`);
        console.log(`  Error: ${testResult.error || 'Connection failed'}`);
        process.exit(1);
    }

    console.log(`${c.green}✓${c.reset} Connected (${testResult.responseTime.toFixed(0)}ms)`);

    if (statusBefore?.sockets) {
        const s = statusBefore.sockets;
        console.log(`${c.dim}  Server stats: ${s.requests?.success || 0} requests, ${s.server_restarts || 0} restarts${c.reset}`);
    }

    // Reset stats after connection test
    Object.assign(stats, {
        totalRequests: 0,
        successfulRequests: 0,
        failedRequests: 0,
        timeouts: 0,
        connectionErrors: 0,
        httpErrors: 0,
        parseErrors: 0,
        totalResponseTime: 0,
        minResponseTime: Infinity,
        maxResponseTime: 0,
        responseTimes: [],
        statusCodes: {},
        errors: {},
        endpoints: {},
        timeline: [],
        anomalies: [],
        consecutiveFailures: 0,
        maxConsecutiveFailures: 0,
        lastResponseTime: 0,
    });

    // Endpoints to test (varied load)
    const endpoints = [
        "/ping",
        "/api/socket-status",
        "/api/network-status",
        "/api/i2c-status",
        "/api/oled-status",
    ];

    console.log(`\n${c.bright}Starting stress test with ${endpoints.length} endpoints...${c.reset}\n`);

    startTime = performance.now();

    // Progress updates
    const progressInterval = setInterval(printProgress, 100);

    // Start workers
    const workers = [];
    for (let i = 0; i < CONCURRENCY; i++) {
        workers.push(worker(i, endpoints));
    }

    // Wait for duration
    await new Promise(resolve => setTimeout(resolve, DURATION_SECONDS * 1000));

    // Stop
    running = false;
    clearInterval(progressInterval);
    await Promise.allSettled(workers);

    const totalTime = (performance.now() - startTime) / 1000;

    // Fetch final device status
    const statusAfter = await fetchDeviceStatus();

    // Calculate statistics
    const sortedTimes = [...stats.responseTimes].sort((a, b) => a - b);
    const avgResponseTime = stats.totalRequests > 0 ? stats.totalResponseTime / stats.totalRequests : 0;
    const stdDev = calculateStdDev(stats.responseTimes, avgResponseTime);
    const p50 = calculatePercentile(sortedTimes, 50);
    const p75 = calculatePercentile(sortedTimes, 75);
    const p90 = calculatePercentile(sortedTimes, 90);
    const p95 = calculatePercentile(sortedTimes, 95);
    const p99 = calculatePercentile(sortedTimes, 99);
    const rps = stats.totalRequests / totalTime;
    const successRate = stats.totalRequests > 0 ? (stats.successfulRequests / stats.totalRequests) * 100 : 0;

    // Print results
    console.log(`\n\n${c.bright}╔═══════════════════════════════════════════════════════════╗${c.reset}`);
    console.log(`${c.bright}║                    STRESS TEST RESULTS                     ║${c.reset}`);
    console.log(`${c.bright}╚═══════════════════════════════════════════════════════════╝${c.reset}`);

    // Summary box
    console.log(`\n${c.cyan}━━━ Summary ━━━${c.reset}`);
    console.log(`  Total Requests:    ${c.bright}${fmt(stats.totalRequests)}${c.reset}`);
    console.log(`  Throughput:        ${c.bright}${rps.toFixed(1)} req/s${c.reset}`);
    console.log(`  Success Rate:      ${successRate >= 95 ? c.green : successRate >= 80 ? c.yellow : c.red}${successRate.toFixed(2)}%${c.reset}`);
    console.log(`  Total Duration:    ${totalTime.toFixed(2)}s`);

    // Request breakdown
    console.log(`\n${c.cyan}━━━ Request Breakdown ━━━${c.reset}`);
    console.log(`  ${c.green}✓ Successful:${c.reset}    ${fmt(stats.successfulRequests)}`);
    console.log(`  ${c.red}✗ Failed:${c.reset}        ${fmt(stats.failedRequests)}`);
    if (stats.timeouts > 0) console.log(`    └─ Timeouts:     ${stats.timeouts}`);
    if (stats.connectionErrors > 0) console.log(`    └─ Conn Errors:  ${stats.connectionErrors}`);
    if (stats.httpErrors > 0) console.log(`    └─ HTTP Errors:  ${stats.httpErrors}`);
    if (stats.parseErrors > 0) console.log(`    └─ Parse Errors: ${stats.parseErrors}`);

    // Response times
    console.log(`\n${c.cyan}━━━ Response Times ━━━${c.reset}`);
    console.log(`  Min:      ${fmtMs(stats.minResponseTime).padStart(10)}`);
    console.log(`  Max:      ${fmtMs(stats.maxResponseTime).padStart(10)}  ${stats.maxResponseTime > 1000 ? c.yellow + '(slow!)' + c.reset : ''}`);
    console.log(`  Average:  ${fmtMs(avgResponseTime).padStart(10)}`);
    console.log(`  Std Dev:  ${fmtMs(stdDev).padStart(10)}`);
    console.log(`  Median:   ${fmtMs(p50).padStart(10)}`);
    console.log(`  p75:      ${fmtMs(p75).padStart(10)}`);
    console.log(`  p90:      ${fmtMs(p90).padStart(10)}`);
    console.log(`  p95:      ${fmtMs(p95).padStart(10)}  ${p95 > 200 ? c.yellow + '(elevated)' + c.reset : ''}`);
    console.log(`  p99:      ${fmtMs(p99).padStart(10)}  ${p99 > 500 ? c.red + '(high!)' + c.reset : ''}`);

    // HTTP Status Codes
    if (Object.keys(stats.statusCodes).length > 0) {
        console.log(`\n${c.cyan}━━━ HTTP Status Codes ━━━${c.reset}`);
        for (const [code, count] of Object.entries(stats.statusCodes).sort()) {
            const pct = ((count / stats.totalRequests) * 100).toFixed(1);
            const color = code.startsWith('2') ? c.green : code.startsWith('4') ? c.yellow : c.red;
            console.log(`  ${color}${code}${c.reset}: ${fmt(count).padStart(8)} (${pct}%)`);
        }
    }

    // Errors
    if (Object.keys(stats.errors).length > 0) {
        console.log(`\n${c.cyan}━━━ Errors ━━━${c.reset}`);
        for (const [error, count] of Object.entries(stats.errors).sort((a, b) => b[1] - a[1])) {
            const pct = ((count / stats.totalRequests) * 100).toFixed(1);
            console.log(`  ${c.red}${error}${c.reset}: ${fmt(count)} (${pct}%)`);
        }
    }

    // Per-endpoint stats
    printEndpointStats();

    // Timeline
    printTimeline();

    // Histogram
    printHistogram(stats.responseTimes);

    // Anomalies
    console.log(`\n${c.cyan}━━━ Anomaly Detection ━━━${c.reset}`);
    console.log(`  Max consecutive failures: ${stats.maxConsecutiveFailures}`);
    console.log(`  Slow responses (>${ANOMALY_THRESHOLDS.responseTime}ms): ${stats.anomalies.filter(a => a.type === 'SLOW_RESPONSE').length}`);
    printAnomalies();

    // Device health comparison
    if (statusBefore && statusAfter) {
        console.log(`\n${c.cyan}━━━ Device Health (Before → After) ━━━${c.reset}`);

        if (statusAfter.sockets && statusBefore.sockets) {
            const reqBefore = statusBefore.sockets.requests?.success || 0;
            const reqAfter = statusAfter.sockets.requests?.success || 0;
            const failBefore = statusBefore.sockets.requests?.failed || 0;
            const failAfter = statusAfter.sockets.requests?.failed || 0;
            const restartsBefore = statusBefore.sockets.server_restarts || 0;
            const restartsAfter = statusAfter.sockets.server_restarts || 0;

            console.log(`  Server requests:  ${fmt(reqBefore)} → ${fmt(reqAfter)} (+${fmt(reqAfter - reqBefore)})`);
            console.log(`  Server failures:  ${failBefore} → ${failAfter} (+${failAfter - failBefore})`);
            if (restartsAfter > restartsBefore) {
                console.log(`  ${c.red}⚠ Server restarts: ${restartsBefore} → ${restartsAfter} (+${restartsAfter - restartsBefore})${c.reset}`);
            } else {
                console.log(`  ${c.green}✓ No server restarts during test${c.reset}`);
            }
        }

        if (statusAfter.i2c && statusBefore.i2c) {
            const errBefore = statusBefore.i2c.errorCount || 0;
            const errAfter = statusAfter.i2c.errorCount || 0;
            if (errAfter > errBefore) {
                console.log(`  ${c.yellow}⚠ I2C errors: ${errBefore} → ${errAfter} (+${errAfter - errBefore})${c.reset}`);
            }
        }

        if (statusAfter.oled && statusBefore.oled) {
            const errBefore = statusBefore.oled.errors || 0;
            const errAfter = statusAfter.oled.errors || 0;
            if (errAfter > errBefore) {
                console.log(`  ${c.yellow}⚠ OLED errors: ${errBefore} → ${errAfter} (+${errAfter - errBefore})${c.reset}`);
            }
        }
        
        // Print timing telemetry if available
        if (statusAfter.timing && statusAfter.timing.sections) {
            console.log(`\n${c.cyan}━━━ Device Timing Telemetry (µs) ━━━${c.reset}`);
            console.log(`  ${c.dim}Uptime: ${statusAfter.timing.uptime_s}s${c.reset}`);
            
            const sections = statusAfter.timing.sections;
            for (const [name, data] of Object.entries(sections)) {
                if (data.cnt === 0) continue;
                const avgUs = data.avg || 0;
                const maxUs = data.max || 0;
                const color = maxUs > 10000 ? c.red : maxUs > 1000 ? c.yellow : c.green;
                console.log(`  ${name.padEnd(16)} cnt=${fmt(data.cnt).padStart(8)} min=${String(data.min).padStart(6)} avg=${String(avgUs).padStart(6)} max=${color}${String(maxUs).padStart(6)}${c.reset} last=${data.last}`);
            }
        } else if (statusAfter.timing && statusAfter.timing.enabled === false) {
            console.log(`\n${c.dim}Timing telemetry not enabled on device${c.reset}`);
        }
    }

    // Final rating
    console.log(`\n${c.cyan}━━━ Performance Rating ━━━${c.reset}`);
    let rating = 0;
    let notes = [];

    if (successRate >= 99.5) { rating += 2; notes.push('Excellent reliability'); }
    else if (successRate >= 98) { rating += 1; notes.push('Good reliability'); }
    else if (successRate < 90) { rating -= 1; notes.push('Poor reliability'); }

    if (rps >= 50) { rating += 2; notes.push('High throughput'); }
    else if (rps >= 30) { rating += 1; notes.push('Good throughput'); }
    else if (rps < 15) { rating -= 1; notes.push('Low throughput'); }

    if (p95 <= 100) { rating += 1; notes.push('Fast p95'); }
    else if (p95 > 300) { rating -= 1; notes.push('Slow p95'); }

    if (stats.maxConsecutiveFailures <= 1) { rating += 1; notes.push('Stable'); }
    else if (stats.maxConsecutiveFailures >= 5) { rating -= 1; notes.push('Unstable bursts'); }

    const stars = Math.max(1, Math.min(5, 3 + rating));
    const starStr = '★'.repeat(stars) + '☆'.repeat(5 - stars);
    const ratingColor = stars >= 4 ? c.green : stars >= 3 ? c.yellow : c.red;
    const ratingText = stars >= 5 ? 'Excellent' : stars >= 4 ? 'Good' : stars >= 3 ? 'Fair' : stars >= 2 ? 'Poor' : 'Critical';

    console.log(`  ${ratingColor}${starStr} ${ratingText}${c.reset}`);
    console.log(`  ${c.dim}${notes.join(' • ')}${c.reset}`);

    console.log();
}

main().catch(error => {
    console.error(`${c.red}Fatal error:${c.reset} ${error.message}`);
    console.error(error.stack);
    process.exit(1);
});
