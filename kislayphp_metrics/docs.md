# KislayPHP Metrics Extension — Technical Reference

## Overview

KislayPHP Metrics is a high-performance PHP metrics collection extension providing Counters, Gauges, and Histograms with Prometheus text format export. Built in C++ with thread-safe primitives, it enables production-grade observability for PHP applications.

---

## 1. Architecture

### Internal Data Structures

The extension maintains three primary data structures protected by a single `pthread_mutex_t` lock per metrics object:

**Counters Map** (`std::unordered_map<std::string, zend_long>`)
- Stores 64-bit signed integer metrics
- Keys: `metric_name{labels_encoded}` format
- Use case: Request count, error count, bytes processed

**Gauges Map** (`std::unordered_map<std::string, double>`)
- Stores IEEE 754 double-precision floats
- Can be set, incremented, or decremented
- Use case: Memory usage, active connections, queue depth

**Histogram Observations Map** (`std::unordered_map<std::string, std::vector<double>>`)
- Stores all raw observations as vector of doubles
- No pre-bucketing; buckets computed at export time
- Default buckets (milliseconds): `0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, +Inf`
- Use case: Request latency, query duration, processing time

### Timer Subsystem

**Active Timers Map** (`std::unordered_map<int, std::pair<std::string, std::chrono::steady_clock::time_point>>`)
- Maps timer ID → (metric_key, start_time)
- Precision: `std::chrono::steady_clock` (nanosecond resolution, system-dependent accuracy)
- Thread-safe ID generation: `std::atomic<int> next_timer_id` (lock-free increment)

**Timer Lifecycle:**
1. `startTimer(name, labels=[])` → acquires lock, records now, generates atomic ID, returns int
2. `stopTimer(id)` → acquires lock, calculates elapsed ms, appends to observations, removes entry, returns elapsed_ms

### Thread Safety

- **Lock Granularity:** One `pthread_mutex_t` per `Metrics` object instance
- **Scope:** Protects all three maps and active_timers during read/write
- **Lock-Free Timer ID:** `std::atomic<int>` for non-blocking ID generation
- **No Global State:** Each PHP `Metrics` instance has independent lock—safe for concurrent requests

**Key Implication:** Multiple concurrent requests can operate different `Metrics` objects without contention.

### Label Encoding

**Input (PHP):**
```php
$labels = ['method' => 'GET', 'status' => '200'];
```

**Internal Storage:**
Keys are sorted alphabetically and formatted as: `http_request_duration{method=GET,status=200}`

**Prometheus Export:**
Quotes added for Prometheus compliance: `http_request_duration{method="GET",status="200"}`

**Empty Labels:**
`$labels = []` → key is just `metric_name` (no braces)

---

## 2. Configuration Reference

### Environment Variables

| Variable | Type | Default | Purpose |
|----------|------|---------|---------|
| `KISLAY_RPC_ENABLED` | bool | `false` | Enable gRPC metrics backend |
| `KISLAY_RPC_TIMEOUT_MS` | long | `200` | gRPC request timeout (milliseconds) |
| `KISLAY_RPC_PLATFORM_ENDPOINT` | string | `127.0.0.1:9100` | gRPC endpoint address |

**Example Configuration:**
```bash
export KISLAY_RPC_ENABLED=true
export KISLAY_RPC_TIMEOUT_MS=500
export KISLAY_RPC_PLATFORM_ENDPOINT="metrics-service.internal:9100"
php app.php
```

### Client Configuration

```php
use Kislay\Metrics\Metrics;

$metrics = new Metrics();

// Optional: Configure custom gRPC client
// $client implements ClientInterface
if (class_exists('\CustomMetricsClient')) {
    $success = $metrics->setClient(new CustomMetricsClient());
    if (!$success) {
        error_log("Failed to set metrics client");
    }
}
```

---

## 3. API Reference

### Counters

#### `inc(string $name, int $by = 1, array $labels = []): int`
Increments counter by `$by`. Returns new value.

```php
$metrics->inc('http_requests_total', 1, ['method' => 'GET']);
$metrics->inc('bytes_processed', 1024, ['type' => 'upload']);
```

**Parameters:**
- `$name`: Metric name (string)
- `$by`: Increment amount, default 1 (int ≥ 0)
- `$labels`: Label map, default empty (array)

**Returns:** New counter value (int)

**Thread Safety:** Atomic under lock

---

#### `dec(string $name, int $by = 1, array $labels = []): int`
Decrements counter by `$by`. Returns new value.

```php
$metrics->dec('queue_depth', 1, ['queue' => 'high_priority']);
```

**Parameters:** Same as `inc()`

**Returns:** New counter value (may be negative)

**Note:** Counters are monotonic by convention; decrement is allowed but not recommended for Prometheus counters.

---

#### `get(string $name): int`
Retrieves counter value or 0 if not found.

```php
$current = $metrics->get('http_requests_total');
echo "Total requests: {$current}\n";
```

---

#### `all(): array`
Returns all counters as associative array.

```php
$all = $metrics->all();
// Returns: ['http_requests_total{method="GET"}' => 150, 'http_requests_total{method="POST"}' => 45]
```

---

#### `reset(?string $name = null): bool`
Resets counter(s).

```php
$metrics->reset('http_requests_total');  // Reset single counter
$metrics->reset();                       // Reset ALL counters
```

**Parameters:**
- `$name`: Counter name to reset. If `null`, resets all counters.

**Returns:** true

---

### Gauges

#### `gauge(string $name, float $value, array $labels = []): void`
Sets gauge to exact value.

```php
$metrics->gauge('memory_usage_bytes', memory_get_usage(), ['type' => 'current']);
```

**Parameters:**
- `$name`: Metric name
- `$value`: Gauge value (float)
- `$labels`: Label map

---

#### `gaugeInc(string $name, float $by = 1.0, array $labels = []): double`
Increments gauge. Returns new value.

```php
$metrics->gaugeInc('active_connections', 1.0, ['pool' => 'read']);
```

---

#### `gaugeDec(string $name, float $by = 1.0, array $labels = []): double`
Decrements gauge. Returns new value.

```php
$metrics->gaugeDec('active_connections', 1.0, ['pool' => 'read']);
```

---

#### `getGauge(string $name, array $labels = []): double`
Retrieves gauge value or 0.0 if not found.

```php
$current = $metrics->getGauge('memory_usage_bytes', ['type' => 'current']);
```

---

### Histograms

#### `observe(string $name, float $value, array $labels = []): void`
Records raw observation (typically milliseconds).

```php
$elapsed = microtime(true) - $start;
$metrics->observe('request_duration_ms', $elapsed * 1000, ['endpoint' => '/api/v1']);
```

---

#### `startTimer(string $name, array $labels = []): int`
Starts timer, returns unique ID. Uses `steady_clock::now()` internally.

```php
$timerId = $metrics->startTimer('database_query_ms', ['table' => 'users']);
// ... execute query
$elapsed = $metrics->stopTimer($timerId);
```

**Returns:** Integer timer ID (never reused during process lifetime)

---

#### `stopTimer(int $timerId): double`
Stops timer, records elapsed milliseconds, returns elapsed time.

```php
$elapsed = $metrics->stopTimer($timerId);
echo "Query took: {$elapsed} ms\n";
```

**Returns:** Elapsed time in milliseconds (double)

**Exceptions:** Returns -1.0 if timer ID not found

---

### Export

#### `exportPrometheus(): string`
Returns Prometheus 0.0.4 text format.

```php
header('Content-Type: text/plain; version=0.0.4');
echo $metrics->exportPrometheus();
```

**Format:**
```
# HELP http_requests_total Counter: http_requests_total
# TYPE http_requests_total counter
http_requests_total{method="GET"} 150

# HELP request_duration_ms Histogram: request_duration_ms
# TYPE request_duration_ms histogram
request_duration_ms_bucket{endpoint="/api",le="0.005"} 2
request_duration_ms_bucket{endpoint="/api",le="0.01"} 5
...
request_duration_ms_bucket{endpoint="/api",le="+Inf"} 1000
request_duration_ms_sum{endpoint="/api"} 45000.5
request_duration_ms_count{endpoint="/api"} 1000
```

---

#### `exportJSON(): array`
Returns metrics as nested PHP array.

```php
$data = $metrics->exportJSON();
header('Content-Type: application/json');
echo json_encode($data);
```

**Returns:**
```php
[
    'counters' => ['http_requests_total{method="GET"}' => 150],
    'gauges' => ['memory_usage_bytes' => 8388608],
    'histograms' => [
        'request_duration_ms' => [
            'observations' => [10.5, 12.3, 11.8, ...],
            'bucket_counts' => [2, 5, 8, 12, 25, 50, 100, 200, 250, 300, 1000],
            'sum' => 45000.5,
            'count' => 1000
        ]
    ]
]
```

---

### Configuration

#### `setClient(ClientInterface $client): bool`
Configures custom gRPC client for remote backend.

```php
$client = new CustomMetricsClient('remote-endpoint:9100');
$success = $metrics->setClient($client);
```

**Returns:** true on success, false on failure

---

## 4. Patterns and Recipes

### HTTP Request Counter with Labels

```php
class RequestMetrics {
    private $metrics;
    
    public function __construct() {
        $this->metrics = new Metrics();
    }
    
    public function recordRequest($method, $endpoint, $statusCode) {
        $this->metrics->inc('http_requests_total', 1, [
            'method' => $method,
            'endpoint' => $endpoint,
            'status' => (string)$statusCode
        ]);
    }
}

// Usage
$rm = new RequestMetrics();
$rm->recordRequest('GET', '/api/users', 200);
$rm->recordRequest('POST', '/api/users', 201);
$rm->recordRequest('GET', '/api/users', 404);
```

### Response Time Histogram with Timer

```php
class RequestTiming {
    private $metrics;
    
    public function __construct() {
        $this->metrics = new Metrics();
    }
    
    public function handleRequest() {
        $timerId = $this->metrics->startTimer('http_request_duration_ms', [
            'endpoint' => $_SERVER['REQUEST_URI']
        ]);
        
        try {
            // Process request
            $result = $this->processRequest();
            $statusCode = 200;
        } catch (Exception $e) {
            $statusCode = 500;
        }
        
        $elapsed = $this->metrics->stopTimer($timerId);
        $this->metrics->inc('http_requests_total', 1, ['status' => (string)$statusCode]);
        
        return $result;
    }
    
    private function processRequest() {
        // Business logic here
    }
}
```

### Memory Gauge with Peak Tracking

```php
class MemoryMonitor {
    private $metrics;
    
    public function __construct() {
        $this->metrics = new Metrics();
    }
    
    public function updateMetrics() {
        $current = memory_get_usage();
        $peak = memory_get_peak_usage();
        
        $this->metrics->gauge('memory_usage_bytes', $current, ['type' => 'current']);
        $this->metrics->gauge('memory_usage_bytes', $peak, ['type' => 'peak']);
    }
}
```

### Prometheus Scrape Endpoint (Laravel Example)

```php
Route::get('/metrics', function () {
    $metrics = app('metrics');
    return response($metrics->exportPrometheus())
        ->header('Content-Type', 'text/plain; version=0.0.4');
});
```

### Queue Depth Monitoring

```php
class QueueMetrics {
    private $metrics;
    
    public function jobEnqueued($queueName) {
        $this->metrics->gaugeInc('queue_depth', 1.0, ['queue' => $queueName]);
    }
    
    public function jobProcessed($queueName) {
        $this->metrics->gaugeDec('queue_depth', 1.0, ['queue' => $queueName]);
        $this->metrics->inc('jobs_processed_total', 1, ['queue' => $queueName]);
    }
}
```

---

## 5. Performance Notes

### Histogram Memory Growth

**Issue:** Raw observations stored in unbounded vector; memory grows linearly with observation count.

**Mitigation Strategies:**
1. Periodic export + reset: Call `exportPrometheus()` every N seconds in background job
2. Client implementation: Use `setClient()` to push to remote backend immediately
3. Label cardinality: Limit unique label combinations (high cardinality = more vectors)

**Example: Periodic Export**
```php
register_tick_function(function() {
    static $lastExport = 0;
    if (time() - $lastExport > 60) {
        file_put_contents('/tmp/metrics.txt', $metrics->exportPrometheus());
        $metrics->reset();
        $lastExport = time();
    }
});
```

### Export Performance

**Time Complexity:** O(C + G + H×N) where:
- C = number of counter keys
- G = number of gauge keys
- H = number of histogram keys
- N = average observations per histogram

**Tip:** Cache export if not changing rapidly. Call Prometheus scrape no more than once per 15 seconds. Consider aggregation strategies for high-volume environments.

### Timer Precision

- **Resolution:** Nanosecond (system-dependent; typically microsecond accuracy on modern systems)
- **Type:** `std::chrono::steady_clock` (monotonic, ignores system clock adjustments)
- **Overhead:** ~500-1000 ns per timer start/stop
- **Lock Cost:** Included in start/stop; typically 1-10 µs contention-free

### Label Cardinality Impact

- **Best Practice:** Keep label combinations under 100 per metric
- **Memory:** Each unique label combination = separate vector in histogram
- **Export Time:** Scales with number of unique label combinations
- **Prometheus Compatibility:** High cardinality labels can cause scrape timeouts

---

## 6. Troubleshooting

### Histogram Memory Growing Unbounded

**Symptom:** PHP process memory usage increases over time with no plateau.

**Cause:** Observations never exported or reset; accumulate indefinitely.

**Solution:**
```php
// Option 1: Periodic reset
$metrics->reset();

// Option 2: Export regularly
$prometheus = $metrics->exportPrometheus();
// Send to backend

// Option 3: Use custom client
$metrics->setClient(new RemoteMetricsBackend());
```

### Labels Missing from Output

**Symptom:** Metrics appear without labels in Prometheus export.

**Cause:** Labels array not passed to `inc()`, `dec()`, `observe()`, etc.

**Check:**
```php
// Wrong: no labels passed
$metrics->inc('requests');

// Correct: pass labels array
$metrics->inc('requests', 1, ['method' => 'GET']);
```

**Note:** Empty labels array `[]` is valid and produces metric without braces.

### Timer ID Not Found

**Symptom:** `stopTimer($id)` returns -1.0; elapsed time not recorded.

**Cause:** Timer ID already stopped, expired, or invalid.

**Prevention:**
```php
$id = $metrics->startTimer('duration', []);
if (($elapsed = $metrics->stopTimer($id)) !== -1.0) {
    // Success
} else {
    error_log("Timer {$id} not found or already stopped");
}
```

### High Label Cardinality

**Symptom:** Memory bloat; many histogram vectors created; slow export.

**Cause:** Too many unique label combinations (e.g., user ID as label).

**Solution:** Limit labels to low-cardinality dimensions:
```php
// Bad: user_id is unbounded cardinality
$metrics->inc('requests', 1, ['user_id' => $_GET['user_id']]);

// Good: use bounded dimensions
$metrics->inc('requests', 1, ['tier' => 'premium', 'region' => 'us-east']);
```

### Missing Prometheus TYPE Declarations

**Symptom:** Prometheus scrape fails; missing or malformed type declarations.

**Cause:** Histogram not properly exported; bucket definitions missing.

**Verification:**
```php
$output = $metrics->exportPrometheus();
// Check that output contains:
// # TYPE metric_name histogram
// metric_name_bucket{...}
// metric_name_sum{...}
// metric_name_count{...}
```

---

## Quick Start

```php
<?php
use Kislay\Metrics\Metrics;

// Initialize
$metrics = new Metrics();

// Counters
$metrics->inc('page_views', 1, ['page' => '/home']);
$metrics->inc('api_calls_total', 1, ['endpoint' => '/api/users', 'status' => '200']);

// Gauges
$metrics->gauge('active_users', 42);
$metrics->gauge('memory_usage_bytes', memory_get_usage(), ['type' => 'current']);

// Histograms with Timer
$id = $metrics->startTimer('request_ms');
usleep(100000); // Simulate work
$elapsed = $metrics->stopTimer($id);

// Manual observation
$metrics->observe('custom_duration_ms', 123.45, ['operation' => 'cleanup']);

// Export for Prometheus
header('Content-Type: text/plain; version=0.0.4');
echo $metrics->exportPrometheus();

// Or JSON export
header('Content-Type: application/json');
echo json_encode($metrics->exportJSON());
?>
```

---

## References

- **Prometheus Format:** [prometheus.io/docs/instrumenting/exposition_formats](https://prometheus.io/docs/instrumenting/exposition_formats)
- **Thread Safety:** POSIX `pthread_mutex_t` + `std::atomic<int>` for lock-free ID generation
- **Histogram Bucketing:** Computed on-demand using default 11-bucket Prometheus scheme
- **Label Specification:** Alphabetically sorted, URL-encoded special characters

---

*Documentation Version: 1.0 | KislayPHP Metrics C++ Extension*
*Last Updated: 2024 | For Source Code: kislayphp_metrics.cpp (1121 lines)*
