#!/usr/bin/env node
/**
 * Client Info Test
 * Fetches basic information from the device REST API
 * 
 * Usage: node client-info.mjs [IP_ADDRESS]
 * Default IP: 192.168.1.100
 */

const TARGET_IP = process.argv[2] || '192.168.1.100';
const BASE_URL = `http://${TARGET_IP}`;
const TIMEOUT_MS = 5000;

// ANSI colors for terminal output
const colors = {
    reset: '\x1b[0m',
    bright: '\x1b[1m',
    green: '\x1b[32m',
    red: '\x1b[31m',
    yellow: '\x1b[33m',
    cyan: '\x1b[36m',
    dim: '\x1b[2m'
};

const log = {
    info: (msg) => console.log(`${colors.cyan}ℹ${colors.reset} ${msg}`),
    success: (msg) => console.log(`${colors.green}✓${colors.reset} ${msg}`),
    error: (msg) => console.log(`${colors.red}✗${colors.reset} ${msg}`),
    header: (msg) => console.log(`\n${colors.bright}${msg}${colors.reset}`),
    data: (key, value) => console.log(`  ${colors.dim}${key}:${colors.reset} ${value}`)
};

async function fetchWithTimeout(url, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), TIMEOUT_MS);
    
    try {
        const response = await fetch(url, {
            ...options,
            signal: controller.signal
        });
        clearTimeout(timeout);
        return response;
    } catch (error) {
        clearTimeout(timeout);
        throw error;
    }
}

async function getEndpoint(path, description) {
    const url = `${BASE_URL}${path}`;
    const startTime = performance.now();
    
    try {
        const response = await fetchWithTimeout(url);
        const elapsed = (performance.now() - startTime).toFixed(1);
        
        if (!response.ok) {
            log.error(`${description}: HTTP ${response.status} (${elapsed}ms)`);
            return null;
        }
        
        const contentType = response.headers.get('content-type') || '';
        let data;
        
        if (contentType.includes('application/json')) {
            data = await response.json();
        } else {
            data = await response.text();
        }
        
        log.success(`${description} (${elapsed}ms)`);
        return data;
    } catch (error) {
        const elapsed = (performance.now() - startTime).toFixed(1);
        if (error.name === 'AbortError') {
            log.error(`${description}: Timeout after ${TIMEOUT_MS}ms`);
        } else if (error.cause) {
            // Node.js fetch wraps errors in cause
            const cause = error.cause;
            const code = cause.code || cause.name || 'UNKNOWN';
            log.error(`${description}: ${code} - ${cause.message || error.message} (${elapsed}ms)`);
        } else {
            log.error(`${description}: ${error.code || error.name} - ${error.message} (${elapsed}ms)`);
        }
        return null;
    }
}

function displayJson(data, indent = 2) {
    if (data === null || data === undefined) return;
    
    if (typeof data === 'object') {
        for (const [key, value] of Object.entries(data)) {
            if (typeof value === 'object' && value !== null) {
                console.log(`  ${colors.dim}${key}:${colors.reset}`);
                for (const [k, v] of Object.entries(value)) {
                    log.data(`  ${k}`, JSON.stringify(v));
                }
            } else {
                log.data(key, JSON.stringify(value));
            }
        }
    } else {
        console.log(`  ${data}`);
    }
}

async function main() {
    console.log(`\n${colors.bright}═══════════════════════════════════════════${colors.reset}`);
    console.log(`${colors.bright}  Device Client Info Test${colors.reset}`);
    console.log(`${colors.bright}═══════════════════════════════════════════${colors.reset}`);
    log.info(`Target: ${BASE_URL}`);
    log.info(`Timeout: ${TIMEOUT_MS}ms per request`);
    
    const startTime = performance.now();
    
    // Common API endpoints to try
    const endpoints = [
        { path: '/ping', description: 'Ping' },
        { path: '/api/socket-status', description: 'Socket Status' },
        { path: '/api/network-status', description: 'Device Info' },
        { path: '/api/i2c-status', description: 'I2C Bus Status' },
        { path: '/api/oled-status', description: 'OLED Status' },
    ];
    
    log.header('Fetching endpoints...');
    
    const results = {};
    for (const endpoint of endpoints) {
        const data = await getEndpoint(endpoint.path, endpoint.description);
        if (data !== null) {
            results[endpoint.path] = data;
        }
    }
    
    // Display collected data
    log.header('Collected Data:');
    
    for (const [path, data] of Object.entries(results)) {
        console.log(`\n  ${colors.cyan}${path}${colors.reset}`);
        if (typeof data === 'string' && data.length > 100) {
            console.log(`    ${colors.dim}(${data.length} bytes of content)${colors.reset}`);
        } else {
            displayJson(data);
        }
    }
    
    const totalTime = ((performance.now() - startTime) / 1000).toFixed(2);
    
    log.header('Summary:');
    log.data('Endpoints tested', endpoints.length);
    log.data('Successful', Object.keys(results).length);
    log.data('Failed', endpoints.length - Object.keys(results).length);
    log.data('Total time', `${totalTime}s`);
    
    console.log();
}

main().catch(error => {
    log.error(`Fatal error: ${error.message}`);
    process.exit(1);
});
