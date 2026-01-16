#!/usr/bin/env node
/**
 * Fetch and display timing telemetry from the device
 * 
 * Usage: node timing.mjs [IP_ADDRESS] [--reset]
 * Defaults: IP=192.168.1.100
 */

const TARGET_IP = process.argv[2] || '192.168.1.100';
const RESET = process.argv.includes('--reset');
const BASE_URL = `http://${TARGET_IP}`;

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

function fmt(num) {
    return num.toLocaleString('en-US');
}

function fmtUs(us) {
    if (us === 0 || us === undefined) return 'N/A';
    if (us < 1000) return `${us}µs`;
    if (us < 1000000) return `${(us / 1000).toFixed(2)}ms`;
    return `${(us / 1000000).toFixed(2)}s`;
}

async function main() {
    console.log(`\n${c.bright}╔═══════════════════════════════════════════════════════════╗${c.reset}`);
    console.log(`${c.bright}║            DEVICE TIMING TELEMETRY                         ║${c.reset}`);
    console.log(`${c.bright}╚═══════════════════════════════════════════════════════════╝${c.reset}`);
    console.log(`\n${c.dim}Target: ${BASE_URL}${c.reset}\n`);
    
    if (RESET) {
        try {
            console.log(`${c.yellow}Resetting timing stats...${c.reset}`);
            const res = await fetch(`${BASE_URL}/api/timing/reset`, {
                method: 'POST',
                signal: AbortSignal.timeout(3000)
            });
            if (res.ok) {
                console.log(`${c.green}✓ Timing stats reset${c.reset}\n`);
            } else {
                console.log(`${c.red}✗ Reset failed: ${res.status}${c.reset}\n`);
            }
        } catch (e) {
            console.log(`${c.red}✗ Reset failed: ${e.message}${c.reset}\n`);
        }
    }
    
    try {
        const res = await fetch(`${BASE_URL}/api/timing`, {
            signal: AbortSignal.timeout(3000)
        });
        
        if (!res.ok) {
            console.log(`${c.red}Error: HTTP ${res.status}${c.reset}`);
            return;
        }
        
        const data = await res.json();
        
        if (data.enabled === false) {
            console.log(`${c.yellow}Timing telemetry is not enabled on the device.${c.reset}`);
            console.log(`${c.dim}Add #define XTP_TIMING_TELEMETRY before including xtp-lib.h${c.reset}`);
            return;
        }
        
        console.log(`${c.cyan}━━━ System Info ━━━${c.reset}`);
        console.log(`  Uptime: ${data.uptime_s}s (${(data.uptime_s / 60).toFixed(1)} min)`);
        
        if (!data.sections || Object.keys(data.sections).length === 0) {
            console.log(`\n${c.yellow}No timing data collected yet.${c.reset}`);
            return;
        }
        
        console.log(`\n${c.cyan}━━━ Timing Sections ━━━${c.reset}`);
        console.log(`  ${'Section'.padEnd(18)} ${'Count'.padStart(10)} ${'Min'.padStart(10)} ${'Avg'.padStart(10)} ${'Max'.padStart(10)} ${'Last'.padStart(10)}`);
        console.log(`  ${'-'.repeat(68)}`);
        
        // Sort sections by count (descending)
        const sortedSections = Object.entries(data.sections)
            .sort((a, b) => b[1].cnt - a[1].cnt);
        
        for (const [name, stats] of sortedSections) {
            if (stats.cnt === 0) continue;
            
            const maxColor = stats.max > 10000 ? c.red : stats.max > 1000 ? c.yellow : c.green;
            
            console.log(`  ${name.padEnd(18)} ${fmt(stats.cnt).padStart(10)} ${fmtUs(stats.min).padStart(10)} ${fmtUs(stats.avg).padStart(10)} ${maxColor}${fmtUs(stats.max).padStart(10)}${c.reset} ${fmtUs(stats.last).padStart(10)}`);
        }
        
        // Calculate loop frequency
        const loopStats = data.sections.loop_total;
        if (loopStats && loopStats.cnt > 0 && data.uptime_s > 0) {
            const loopFreq = loopStats.cnt / data.uptime_s;
            console.log(`\n${c.cyan}━━━ Performance Metrics ━━━${c.reset}`);
            console.log(`  Loop frequency:  ${loopFreq.toFixed(0)} Hz (${fmtUs(loopStats.avg)} avg per loop)`);
            
            // Check for bottlenecks
            const sections = data.sections;
            let bottlenecks = [];
            
            if (sections.http_handler?.max > 50000) {
                bottlenecks.push(`HTTP handler slow (max ${fmtUs(sections.http_handler.max)})`);
            }
            if (sections.oled_update?.max > 5000) {
                bottlenecks.push(`OLED updates slow (max ${fmtUs(sections.oled_update.max)})`);
            }
            if (sections.i2c_recovery?.cnt > 0) {
                bottlenecks.push(`I2C recoveries: ${sections.i2c_recovery.cnt}`);
            }
            if (sections.socket_cleanup?.max > 5000) {
                bottlenecks.push(`Socket cleanup slow (max ${fmtUs(sections.socket_cleanup.max)})`);
            }
            
            if (bottlenecks.length > 0) {
                console.log(`\n${c.yellow}Potential Bottlenecks:${c.reset}`);
                for (const b of bottlenecks) {
                    console.log(`  ${c.yellow}⚠${c.reset} ${b}`);
                }
            } else {
                console.log(`  ${c.green}✓ No significant bottlenecks detected${c.reset}`);
            }
        }
        
    } catch (e) {
        if (e.name === 'AbortError') {
            console.log(`${c.red}Error: Request timeout${c.reset}`);
        } else if (e.cause?.code === 'ECONNREFUSED') {
            console.log(`${c.red}Error: Connection refused - device not reachable${c.reset}`);
        } else {
            console.log(`${c.red}Error: ${e.message}${c.reset}`);
        }
    }
    
    console.log();
}

main();
