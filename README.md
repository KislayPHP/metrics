# KislayPHP Metrics

[![PHP Version](https://img.shields.io/badge/PHP-8.2%2B-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/metrics/ci.yml?branch=main&label=CI)](https://github.com/KislayPHP/metrics/actions)
[![PIE](https://img.shields.io/badge/install-pie-blueviolet)](https://github.com/php/pie)

> **Application metrics for PHP microservices.** Counter, gauge, histogram, and timer types with zero instrumentation overhead. Prometheus-compatible export.

Part of the [KislayPHP ecosystem](https://skelves.com/kislayphp/docs).

---

## ✨ What It Does

`kislayphp/metrics` provides C++-backed metrics primitives for PHP applications. All metric operations use atomic operations — measuring your code doesn't slow it down.

```php
<?php
$metrics = new Kislay\Metrics\Collector();
$metrics->counter('requests_total')->increment();
$metrics->histogram('request_duration_ms')->observe($ms);
echo $metrics->export(); // Prometheus text format
```

---

## 📦 Installation

```bash
pie install kislayphp/metrics
```

Enable in `php.ini`:
```ini
extension=kislayphp_metrics.so
```

---

## 🚀 Quick Start

```php
<?php
$metrics = new Kislay\Metrics\Collector();

// Counter — monotonically increasing
$metrics->counter('http_requests_total', ['method' => 'GET', 'status' => '200'])
        ->increment();

// Gauge — value that goes up and down
$metrics->gauge('active_connections')->set(42);
$metrics->gauge('queue_depth')->increment();
$metrics->gauge('queue_depth')->decrement();

// Histogram — distribution of values
$start = microtime(true);
// ... handle request ...
$metrics->histogram('request_duration_seconds')
        ->observe(microtime(true) - $start);

// Timer (convenience wrapper for histogram) — start()/stop() on the same
// object; start() returns void, not a chainable handle.
$timer = $metrics->timer('db_query_seconds');
$timer->start();
// ... run query ...
$timer->stop(); // records the elapsed time into the underlying histogram

// Export as Prometheus text format
$app->get('/metrics', function($req, $res) use ($metrics) {
    $res->send($metrics->export(), 'text/plain; version=0.0.4');
});
```

---

## 📖 Public API

```php
namespace Kislay\Metrics;

class Collector {
    public function counter(string $name, array $labels = []): Counter;
    public function gauge(string $name, array $labels = []): Gauge;
    public function histogram(string $name, array $labels = [], array $buckets = []): Histogram;
    public function timer(string $name, array $labels = []): Timer;
    public function export(): string;    // Prometheus text format
    public function reset(): void;
}

class Counter  { public function increment(float $by = 1): void; public function get(): float; }
class Gauge    { public function set(float $value): void; public function increment(float $by = 1): void; public function decrement(float $by = 1): void; public function get(): float; }
class Histogram { public function observe(float $value): void; }
class Timer    { public function __construct(string $name, Histogram $histogram); public function start(): void; public function stop(): float; public function record(float $ms): void; }
```

Note: `Counter::get()`/`Gauge::get()` genuinely return `float` (fixed 2026-08-12 — previously returned `int` despite this signature). `Collector::timer()` (also added 2026-08-12) constructs and returns a real `Timer` bound to a lazily-created/looked-up `Histogram` of the same name, same lookup-or-create semantics as `counter()`/`gauge()`/`histogram()`.

Known gap: the `labels` parameter shown above on `counter()`/`gauge()`/`histogram()`/`timer()` is accepted syntactically but not implemented — labels are not attached to the exported series. Don't rely on it for cardinality/dimensioning yet.

`start()` returns `void`, not a chainable handle — call `start()`/`stop()` on the same `Timer` instance (there is no separate `TimerHandle` class). `stop()` returns the elapsed milliseconds AND records that value into the underlying `Histogram` automatically.

Legacy aliases: `KislayPHP\Metrics\*`

---

## 🔗 Ecosystem

[core](https://github.com/KislayPHP/core) · [gateway](https://github.com/KislayPHP/gateway) · [discovery](https://github.com/KislayPHP/discovery) · **metrics** · [queue](https://github.com/KislayPHP/queue) · [eventbus](https://github.com/KislayPHP/eventbus)

## 📄 License

[Apache License 2.0](LICENSE) · **[Full Docs](https://skelves.com/kislayphp/docs)**
