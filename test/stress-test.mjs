#!/usr/bin/env node
/**
 * Stress Test for REST Server
 * Tests how many requests the device can handle within a time limit
 * 
 * Usage: node stress-test.mjs [IP_ADDRESS] [DURATION_SECONDS] [CONCURRENCY]
 * Defaults: IP=192.168.1.100, Duration=5s, Concurrency=10
 */

const TARGET_IP = process.argv[2] || '192.168.1.100';
const DURATION_SECONDS = parseInt(process.argv[3]) || 5;
const CONCURRENCY = parseInt(process.argv[4]) || 10;
const BASE_URL = `http://${TARGET_IP}`;
const REQUEST_TIMEOUT_MS = 2000;

// ANSI colors
const colors = {
    reset: '\x1b[0m',
    bright: '\x1b[1m',
    green: '\x1b[32m',
    red: '\x1b[31m',
    yellow: '\x1b[33m',
    cyan: '\x1b[36m',
    dim: '\x1b[2m',
    magenta: '\x1b[35m'
};

// Statistics
const stats = {
    totalRequests: 0,
    successfulRequests: 0,
    failedRequests: 0,
    timeouts: 0,
    connectionErrors: 0,
    httpErrors: 0,
    totalResponseTime: 0,
    minResponseTime: Infinity,
    maxResponseTime: 0,
    responseTimes: [],
    statusCodes: {},
    errors: {}
};

let running = true;
let startTime = 0;

async function makeRequest(endpoint = '/api/status') {
    const url = `${BASE_URL}${endpoint}`;
    const requestStart = performance.now();
    
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
    
    try {
        const response = await fetch(url, {
            signal: controller.signal,
            headers: {
                'Connection': 'close'  // Don't keep connections alive for stress test
            }
        });
        
        clearTimeout(timeout);
        const responseTime = performance.now() - requestStart;
        
        // Consume body to complete the request
        await response.text();
        
        stats.totalRequests++;
        stats.totalResponseTime += responseTime;
        stats.responseTimes.push(responseTime);
        stats.minResponseTime = Math.min(stats.minResponseTime, responseTime);
        stats.maxResponseTime = Math.max(stats.maxResponseTime, responseTime);
        
        const statusCode = response.status;
        stats.statusCodes[statusCode] = (stats.statusCodes[statusCode] || 0) + 1;
        
        if (response.ok) {
            stats.successfulRequests++;
        } else {
            stats.failedRequests++;
            stats.httpErrors++;
        }
        
        return { success: response.ok, responseTime, statusCode };
        
    } catch (error) {
        clearTimeout(timeout);
        const responseTime = performance.now() - requestStart;
        
        stats.totalRequests++;
        stats.failedRequests++;
        stats.totalResponseTime += responseTime;
        stats.responseTimes.push(responseTime);
        
        if (error.name === 'AbortError') {
            stats.timeouts++;
            stats.errors['Timeout'] = (stats.errors['Timeout'] || 0) + 1;
        } else if (error.code === 'ECONNREFUSED' || error.code === 'ECONNRESET') {
            stats.connectionErrors++;
            stats.errors[error.code] = (stats.errors[error.code] || 0) + 1;
        } else {
            stats.connectionErrors++;
            const errorKey = error.code || error.message.substring(0, 30);
            stats.errors[errorKey] = (stats.errors[errorKey] || 0) + 1;
        }
        
        return { success: false, responseTime, error: error.message };
    }
}

async function worker(id, endpoints) {
    while (running) {
        // Rotate through endpoints
        const endpoint = endpoints[stats.totalRequests % endpoints.length];
        await makeRequest(endpoint);
        
        // Small yield to prevent tight loop issues
        await new Promise(resolve => setImmediate(resolve));
    }
}

function calculatePercentile(sortedArray, percentile) {
    const index = Math.ceil((percentile / 100) * sortedArray.length) - 1;
    return sortedArray[Math.max(0, index)];
}

function formatNumber(num) {
    return num.toLocaleString('en-US');
}

function printProgress() {
    const elapsed = (performance.now() - startTime) / 1000;
    const rps = stats.totalRequests / elapsed;
    const successRate = stats.totalRequests > 0 
        ? ((stats.successfulRequests / stats.totalRequests) * 100).toFixed(1) 
        : 0;
    
    process.stdout.write(`\r${colors.cyan}Progress:${colors.reset} ` +
        `${formatNumber(stats.totalRequests)} requests | ` +
        `${colors.green}${formatNumber(stats.successfulRequests)} OK${colors.reset} | ` +
        `${colors.red}${formatNumber(stats.failedRequests)} failed${colors.reset} | ` +
        `${rps.toFixed(0)} req/s | ` +
        `${elapsed.toFixed(1)}s elapsed   `);
}

async function main() {
    console.log(`\n${colors.bright}═══════════════════════════════════════════${colors.reset}`);
    console.log(`${colors.bright}  REST Server Stress Test${colors.reset}`);
    console.log(`${colors.bright}═══════════════════════════════════════════${colors.reset}`);
    console.log(`${colors.cyan}ℹ${colors.reset} Target: ${BASE_URL}`);
    console.log(`${colors.cyan}ℹ${colors.reset} Duration: ${DURATION_SECONDS} seconds`);
    console.log(`${colors.cyan}ℹ${colors.reset} Concurrency: ${CONCURRENCY} workers`);
    console.log(`${colors.cyan}ℹ${colors.reset} Request timeout: ${REQUEST_TIMEOUT_MS}ms`);
    
    // Test connection first
    console.log(`\n${colors.dim}Testing connection...${colors.reset}`);
    const testResult = await makeRequest('/');
    
    if (!testResult.success) {
        console.log(`${colors.red}✗${colors.reset} Cannot connect to ${BASE_URL}`);
        console.log(`  Error: ${testResult.error || 'Connection failed'}`);
        process.exit(1);
    }
    
    console.log(`${colors.green}✓${colors.reset} Connected (${testResult.responseTime.toFixed(0)}ms)`);
    
    // Reset stats after connection test
    Object.assign(stats, {
        totalRequests: 0,
        successfulRequests: 0,
        failedRequests: 0,
        timeouts: 0,
        connectionErrors: 0,
        httpErrors: 0,
        totalResponseTime: 0,
        minResponseTime: Infinity,
        maxResponseTime: 0,
        responseTimes: [],
        statusCodes: {},
        errors: {}
    });
    
    // Endpoints to test
    const endpoints = [
        '/api/status',
        '/api/i2c-status',
        '/api/info',
    ];
    
    console.log(`\n${colors.bright}Starting stress test...${colors.reset}\n`);
    
    startTime = performance.now();
    
    // Start progress updates
    const progressInterval = setInterval(printProgress, 200);
    
    // Start workers
    const workers = [];
    for (let i = 0; i < CONCURRENCY; i++) {
        workers.push(worker(i, endpoints));
    }
    
    // Wait for duration
    await new Promise(resolve => setTimeout(resolve, DURATION_SECONDS * 1000));
    
    // Stop workers
    running = false;
    clearInterval(progressInterval);
    
    // Wait for workers to finish current requests
    await Promise.allSettled(workers);
    
    const totalTime = (performance.now() - startTime) / 1000;
    
    // Calculate final statistics
    const sortedTimes = [...stats.responseTimes].sort((a, b) => a - b);
    const avgResponseTime = stats.totalRequests > 0 
        ? stats.totalResponseTime / stats.totalRequests 
        : 0;
    const p50 = sortedTimes.length > 0 ? calculatePercentile(sortedTimes, 50) : 0;
    const p95 = sortedTimes.length > 0 ? calculatePercentile(sortedTimes, 95) : 0;
    const p99 = sortedTimes.length > 0 ? calculatePercentile(sortedTimes, 99) : 0;
    const rps = stats.totalRequests / totalTime;
    const successRate = stats.totalRequests > 0 
        ? (stats.successfulRequests / stats.totalRequests) * 100 
        : 0;
    
    // Print results
    console.log(`\n\n${colors.bright}═══════════════════════════════════════════${colors.reset}`);
    console.log(`${colors.bright}  Results${colors.reset}`);
    console.log(`${colors.bright}═══════════════════════════════════════════${colors.reset}`);
    
    console.log(`\n${colors.cyan}Request Statistics:${colors.reset}`);
    console.log(`  Total requests:     ${formatNumber(stats.totalRequests)}`);
    console.log(`  Successful:         ${colors.green}${formatNumber(stats.successfulRequests)}${colors.reset} (${successRate.toFixed(1)}%)`);
    console.log(`  Failed:             ${colors.red}${formatNumber(stats.failedRequests)}${colors.reset}`);
    console.log(`    - Timeouts:       ${stats.timeouts}`);
    console.log(`    - HTTP errors:    ${stats.httpErrors}`);
    console.log(`    - Conn errors:    ${stats.connectionErrors}`);
    
    console.log(`\n${colors.cyan}Performance:${colors.reset}`);
    console.log(`  Requests/second:    ${colors.bright}${rps.toFixed(1)}${colors.reset}`);
    console.log(`  Total time:         ${totalTime.toFixed(2)}s`);
    
    console.log(`\n${colors.cyan}Response Times:${colors.reset}`);
    console.log(`  Min:                ${stats.minResponseTime.toFixed(1)}ms`);
    console.log(`  Max:                ${stats.maxResponseTime.toFixed(1)}ms`);
    console.log(`  Average:            ${avgResponseTime.toFixed(1)}ms`);
    console.log(`  Median (p50):       ${p50.toFixed(1)}ms`);
    console.log(`  p95:                ${p95.toFixed(1)}ms`);
    console.log(`  p99:                ${p99.toFixed(1)}ms`);
    
    if (Object.keys(stats.statusCodes).length > 0) {
        console.log(`\n${colors.cyan}HTTP Status Codes:${colors.reset}`);
        for (const [code, count] of Object.entries(stats.statusCodes).sort()) {
            const color = code.startsWith('2') ? colors.green : colors.red;
            console.log(`  ${color}${code}${colors.reset}: ${formatNumber(count)}`);
        }
    }
    
    if (Object.keys(stats.errors).length > 0) {
        console.log(`\n${colors.cyan}Errors:${colors.reset}`);
        for (const [error, count] of Object.entries(stats.errors).sort((a, b) => b[1] - a[1])) {
            console.log(`  ${colors.red}${error}${colors.reset}: ${count}`);
        }
    }
    
    // Performance rating
    console.log(`\n${colors.cyan}Rating:${colors.reset}`);
    if (successRate >= 99 && rps >= 50) {
        console.log(`  ${colors.green}★★★★★ Excellent${colors.reset} - High throughput, very reliable`);
    } else if (successRate >= 95 && rps >= 30) {
        console.log(`  ${colors.green}★★★★☆ Good${colors.reset} - Good throughput, reliable`);
    } else if (successRate >= 90 && rps >= 15) {
        console.log(`  ${colors.yellow}★★★☆☆ Fair${colors.reset} - Acceptable for light loads`);
    } else if (successRate >= 80) {
        console.log(`  ${colors.yellow}★★☆☆☆ Poor${colors.reset} - May struggle under load`);
    } else {
        console.log(`  ${colors.red}★☆☆☆☆ Critical${colors.reset} - Needs investigation`);
    }
    
    console.log();
}

main().catch(error => {
    console.error(`${colors.red}Fatal error:${colors.reset} ${error.message}`);
    process.exit(1);
});
