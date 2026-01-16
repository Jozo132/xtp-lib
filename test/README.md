# XTP-Lib Test Scripts

Node.js test scripts for testing the REST server on XTP devices.

## Requirements

- Node.js 18+ (uses native fetch API)

## Scripts

### Client Info Test

Fetches basic information from the device's REST API endpoints.

```bash
node client-info.mjs [IP_ADDRESS]
```

**Arguments:**
- `IP_ADDRESS` - Target device IP (default: `192.168.1.100`)

**Example:**
```bash
node client-info.mjs 192.168.1.50
```

**Endpoints tested:**
- `/` - Root page
- `/api/status` - API Status
- `/api/info` - Device Info
- `/api/i2c-status` - I2C Bus Status
- `/api/ethernet` - Ethernet Status
- `/api/oled` - OLED Status
- `/api/sockets` - Socket Status

---

### Stress Test

Tests how many requests per second the device can handle.

```bash
node stress-test.mjs [IP_ADDRESS] [DURATION_SECONDS] [CONCURRENCY]
```

**Arguments:**
- `IP_ADDRESS` - Target device IP (default: `192.168.1.100`)
- `DURATION_SECONDS` - Test duration in seconds (default: `5`)
- `CONCURRENCY` - Number of concurrent workers (default: `10`)

**Example:**
```bash
# Basic test with defaults
node stress-test.mjs 192.168.1.50

# 10-second test with 5 concurrent workers
node stress-test.mjs 192.168.1.50 10 5

# Heavy load test with 20 workers
node stress-test.mjs 192.168.1.50 5 20
```

**Output includes:**
- Total requests made
- Success/failure counts
- Requests per second (RPS)
- Response time statistics (min, max, avg, p50, p95, p99)
- HTTP status code breakdown
- Error breakdown
- Performance rating

---

## Interpreting Results

### Stress Test Performance Ratings

| Rating | Success Rate | RPS | Meaning |
|--------|-------------|-----|---------|
| ★★★★★ | ≥99% | ≥50 | Excellent - handles high load |
| ★★★★☆ | ≥95% | ≥30 | Good - reliable under load |
| ★★★☆☆ | ≥90% | ≥15 | Fair - OK for light loads |
| ★★☆☆☆ | ≥80% | any | Poor - may struggle |
| ★☆☆☆☆ | <80% | any | Critical - needs investigation |

### Common Issues

**Low RPS (< 10 req/s):**
- Network latency issues
- Server blocking on I/O
- Too many concurrent connections

**High Timeout Rate:**
- Server getting overwhelmed
- Socket exhaustion on server
- Request processing too slow

**Connection Refused (ECONNREFUSED):**
- Server not running or crashed
- Wrong IP address
- Firewall blocking

**Connection Reset (ECONNRESET):**
- Server forcibly closing connections
- Socket cleanup issues
- Server crash during request

---

## Tips

1. **Start with low concurrency** (5-10 workers) and increase gradually
2. **Watch for timeout spikes** - indicates server is struggling
3. **Monitor p99 latency** - high p99 with low avg indicates occasional blocking
4. **Check device serial output** during stress test for server logs
