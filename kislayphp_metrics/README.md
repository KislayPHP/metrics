# KislayMetrics

> Lightweight in-process metrics extension for KislayPHP — counters, gauges, histograms, and timers with Prometheus and JSON export.

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

## Installation

**Via PIE (recommended):**
```bash
pie install kislayphp/metrics:0.0.2
```

Add to `php.ini`:
```ini
extension=kislayphp_metrics.so
```

**Build from source:**
```bash
git clone https://github.com/KislayPHP/metrics.git
cd metrics && phpize && ./configure --enable-kislayphp_metrics && make && sudo make install
```

## Requirements

- PHP 8.2+

## Quick Start

```php
<?php
$metrics = new Kislay\Metrics\Metrics();

// Count requests
$metrics->inc('http_requests_total');
$metrics->inc('http_requests_total', 5);   // increment by 5

// Track active connections
$metrics->gaugeInc('active_connections');
// ... handle request ...
$metrics->gaugeDec('active_connections');

// Time an operation
$timer = $metrics->startTimer('db_query_duration_ms');
run_query();
$metrics->stopTimer($timer);

echo $metrics->exportPrometheus();
```

## API Reference

### `Metrics`

#### `__construct()`
Creates a new in-process metrics registry. Use `setClient()` for remote backends.

#### `setClient(Kislay\Metrics\ClientInterface $client): bool`
Delegates all metric operations to a remote client (StatsD, Prometheus pushgateway, etc.).

#### `inc(string $name, int $by = 1): bool`
Increments a counter by `$by`.
- Counter values are monotonically increasing and never decrease
- `$name` should follow Prometheus naming: `snake_case` with `_total` suffix for counters

#### `dec(string $name, int $by = 1): bool`
Decrements a counter by `$by`. Use `gaugeDec()` for values that go up and down.

#### `get(string $name): int`
Returns the current counter value. Returns `0` if the counter does not exist.

#### `reset(?string $name = null): bool`
Resets a counter to `0`. Pass `null` to reset **all** metrics.
Use only in test/dev environments.

#### `all(): array`
Returns all counters as an associative array `['name' => value]`.

#### `gauge(string $name, float $value): bool`
Sets a gauge to an absolute value. Gauges can go up and down.

#### `gaugeInc(string $name, float $by = 1.0): bool`
Increments a gauge.

#### `gaugeDec(string $name, float $by = 1.0): bool`
Decrements a gauge.

#### `getGauge(string $name): float`
Returns the current gauge value. Returns `0.0` if it does not exist.

#### `observe(string $name, float $value): bool`
Records a histogram observation. Use for latency, payload sizes, etc.
- Observations are stored in configurable buckets for percentile calculation

#### `startTimer(string $name): string`
Starts a high-resolution timer. Returns a timer ID used with `stopTimer()`.
```php
$id = $metrics->startTimer('request_duration_ms');
// ... work ...
$ms = $metrics->stopTimer($id); // also records observation
```

#### `stopTimer(string $id): float`
Stops a timer, records the elapsed time as a histogram observation, and returns the duration in milliseconds.

#### `exportPrometheus(): string`
Returns all metrics formatted as Prometheus text exposition format.
```
# HELP http_requests_total Total HTTP requests
# TYPE http_requests_total counter
http_requests_total 42
```

#### `exportJSON(): string`
Returns all metrics as a JSON string.

---

### `ClientInterface`

| Method | Signature | Description |
|--------|-----------|-------------|
| `inc` | `inc(string $name, int $by = 1): bool` | Increment a counter |
| `get` | `get(string $name): int` | Get counter value |
| `all` | `all(): array` | Get all metrics |

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KISLAY_METRICS_FLUSH_INTERVAL_MS` | `0` | Auto-flush interval to remote client (0 = manual) |
| `KISLAY_RPC_ENABLED` | `0` | Enable RPC transport for remote metric push |
| `KISLAY_RPC_TIMEOUT_MS` | `200` | RPC call timeout in ms |

## Examples

### Prometheus Scrape Endpoint

```php
<?php
$app     = new Kislay\Core\App();
$metrics = new Kislay\Metrics\Metrics();

$app->use(function ($req, $res, $next) use ($metrics) {
    $timer = $metrics->startTimer('http_request_duration_ms');
    $next();
    $metrics->stopTimer($timer);
    $metrics->inc('http_requests_total');
});

$app->get('/metrics', function ($req, $res) use ($metrics) {
    $res->header('Content-Type', 'text/plain; version=0.0.4')
        ->send($metrics->exportPrometheus());
});

$app->listen('0.0.0.0', 8080);
```

### Gauge for Queue Depth

```php
$queue   = new Kislay\Queue\Queue();
$metrics = new Kislay\Metrics\Metrics();

$app->get('/health', function ($req, $res) use ($queue, $metrics) {
    $depth = $queue->size('jobs');
    $metrics->gauge('queue_depth', $depth);
    $res->json(['queue_depth' => $depth]);
});
```

### Histogram for DB Query Latency

```php
$id = $metrics->startTimer('db_query_ms');
$results = $pdo->query('SELECT * FROM users');
$elapsed = $metrics->stopTimer($id);
// $elapsed is also recorded in the histogram buckets
```

### JSON Export for Custom Dashboard

```php
$app->get('/internal/metrics', function ($req, $res) use ($metrics) {
    $res->json(json_decode($metrics->exportJSON(), true));
});
```

## Related Extensions

| Extension | Use Case |
|-----------|----------|
| [kislayphp/core](https://github.com/KislayPHP/core) | HTTP server hosting the `/metrics` scrape endpoint |
| [kislayphp/queue](https://github.com/KislayPHP/queue) | Track queue depth as a gauge |
| [kislayphp/gateway](https://github.com/KislayPHP/gateway) | Expose gateway throughput / error counters |

## License

Licensed under the [Apache License 2.0](LICENSE).
