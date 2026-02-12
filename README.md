# KislayPHP Metrics

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/metrics/ci.yml)](https://github.com/KislayPHP/metrics/actions)
[![codecov](https://codecov.io/gh/KislayPHP/metrics/branch/main/graph/badge.svg)](https://codecov.io/gh/KislayPHP/metrics)

A high-performance C++ PHP extension providing comprehensive metrics collection, monitoring, and observability for microservices with support for Prometheus, StatsD, and custom backends.

## ⚡ Key Features

- 🚀 **High Performance**: Ultra-low latency metrics collection
- 📊 **Multiple Metrics Types**: Counters, gauges, histograms, timers
- 🔄 **Backend Support**: Prometheus, StatsD, InfluxDB, and custom clients
- 📈 **Aggregation**: Statistical aggregations and percentiles
- 🏷️ **Labels/Tags**: Multi-dimensional metrics with labels
- 📝 **Logging**: Structured metrics logging and export
- 🔍 **Querying**: Metrics querying and filtering
- 🌐 **Distributed**: Cross-service metrics aggregation

## 📦 Installation

### Via PECL (Recommended)

```bash
pecl install kislayphp_metrics
```

Add to your `php.ini`:

```ini
extension=kislayphp_metrics.so
```

### Manual Build

```bash
git clone https://github.com/KislayPHP/metrics.git
cd metrics
phpize
./configure
make
sudo make install
```

### Docker

```dockerfile
FROM php:8.2-cli
RUN pecl install kislayphp_metrics && docker-php-ext-enable kislayphp_metrics
```

## 🚀 Quick Start

### Basic Metrics Collection

```php
<?php

// Create metrics collector
$metrics = new KislayMetrics();

// Counter metrics
$metrics->increment('requests_total');
$metrics->increment('requests_total', 5);

// Gauge metrics
$metrics->gauge('active_connections', 42);
$metrics->increment('active_connections');  // +1
$metrics->decrement('active_connections');  // -1

// Histogram metrics
$metrics->histogram('request_duration', 0.145); // in seconds

// Timer metrics (automatic duration measurement)
$timer = $metrics->startTimer('database_query');
performDatabaseQuery();
$timer->stop();
```

### Labeled Metrics

```php
<?php

$metrics = new KislayMetrics();

// HTTP request metrics with labels
$metrics->increment('http_requests_total', [
    'method' => 'GET',
    'endpoint' => '/api/users',
    'status' => '200'
]);

$metrics->histogram('http_request_duration', 0.089, [
    'method' => 'POST',
    'endpoint' => '/api/orders'
]);
```

### Custom Backend Integration

```php
<?php

$metrics = new KislayMetrics();

// Use Prometheus backend
$prometheus = new PrometheusClient([
    'gateway_url' => 'http://prometheus-pushgateway:9091',
    'job_name' => 'my-service'
]);
$metrics->setBackend($prometheus);

// Use StatsD backend
$statsd = new StatsDClient([
    'host' => 'statsd-server',
    'port' => 8125,
    'namespace' => 'myapp'
]);
$metrics->setBackend($statsd);

// Use InfluxDB backend
$influxdb = new InfluxDBClient([
    'host' => 'influxdb-server',
    'port' => 8086,
    'database' => 'metrics',
    'username' => 'metrics_user',
    'password' => 'secret'
]);
$metrics->setBackend($influxdb);
```

### Middleware Integration

```php
<?php

$metrics = new KislayMetrics();

// HTTP middleware for automatic metrics
$middleware = new MetricsMiddleware($metrics, [
    'request_count' => 'http_requests_total',
    'request_duration' => 'http_request_duration',
    'response_size' => 'http_response_size'
]);

// In your framework
$app->use($middleware);

// Custom middleware
$app->use(function($request, $response, $next) use ($metrics) {
    $start = microtime(true);

    $result = $next($request, $response);

    $duration = microtime(true) - $start;
    $metrics->histogram('custom_operation_duration', $duration, [
        'operation' => 'user_registration'
    ]);

    return $result;
});
```

### Metrics Export

```php
<?php

$metrics = new KislayMetrics();

// Export metrics in Prometheus format
$prometheusFormat = $metrics->export('prometheus');
echo $prometheusFormat;

// Export metrics as JSON
$jsonFormat = $metrics->export('json');
echo $jsonFormat;

// Get raw metrics data
$allMetrics = $metrics->getAllMetrics();
print_r($allMetrics);
```

## 📚 Documentation

📖 **[Complete Documentation](docs.md)** - API reference, backend integrations, configuration options, and best practices

## 🏗️ Architecture

KislayPHP Metrics implements a layered architecture:

```
┌─────────────────┐
│ Application     │
│ Code            │
└─────────────────┘
         │
    ┌─────────────┐
    │  Metrics    │
    │ Collector   │
    │  (PHP)      │
    │             │
    │ ┌─────────┐ │
    │ │ Storage │ │
    │ │ Engine  │ │
    │ └─────────┘ │
    │             │
    │ ┌─────────┐ │
    │ │ Backend │ │
    │ │ Client  │ │
    │ └─────────┘ │
    └─────────────┘
         │
    ┌─────────────┐
    │ Monitoring  │
    │ Systems     │
    │ (Prometheus │
    │  StatsD...) │
    └─────────────┘
```

## 🎯 Use Cases

- **Application Monitoring**: Track performance and usage metrics
- **Service Health**: Monitor service availability and response times
- **Business Metrics**: Track user behavior and business KPIs
- **Infrastructure Monitoring**: System resource usage and performance
- **Distributed Tracing**: Request tracing across microservices
- **Alerting**: Set up alerts based on metric thresholds

## 📊 Performance

```
Metrics Collection Benchmark:
===========================
Metrics/Second:        500,000
Memory Usage:          12 MB
Average Latency:       0.02 ms
P95 Latency:           0.05 ms
Label Cardinality:     10,000
Export Time (JSON):    15 ms
Export Time (Prom):    8 ms
```

## 🔧 Configuration

### php.ini Settings

```ini
; Metrics extension settings
kislayphp.metrics.max_labels = 10000
kislayphp.metrics.max_series = 100000
kislayphp.metrics.collection_interval = 60
kislayphp.metrics.export_timeout = 30

; Storage settings
kislayphp.metrics.storage_engine = "memory"
kislayphp.metrics.storage_max_size = 1000000

; Backend settings
kislayphp.metrics.backend = "prometheus"
kislayphp.metrics.prometheus_gateway = "http://localhost:9091"
```

### Environment Variables

```bash
export KISLAYPHP_METRICS_BACKEND=prometheus
export KISLAYPHP_METRICS_PROMETHEUS_GATEWAY=http://prometheus:9091
export KISLAYPHP_METRICS_STATSD_HOST=statsd:8125
export KISLAYPHP_METRICS_INFLUXDB_HOST=influxdb:8086
export KISLAYPHP_METRICS_MAX_LABELS=10000
```

## 🧪 Testing

```bash
# Run unit tests
php run-tests.php

# Test metrics collection
cd tests/
php test_metrics_collection.php

# Test backend integration
php test_prometheus_backend.php

# Performance tests
php test_performance.php
```

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](.github/CONTRIBUTING.md) for details.

## 📄 License

Licensed under the [Apache License 2.0](LICENSE).

## 🆘 Support

- 📖 [Documentation](docs.md)
- 🐛 [Issue Tracker](https://github.com/KislayPHP/metrics/issues)
- 💬 [Discussions](https://github.com/KislayPHP/metrics/discussions)
- 📧 [Security Issues](.github/SECURITY.md)

## 📈 Roadmap

- [ ] OpenTelemetry integration
- [ ] Distributed tracing
- [ ] Custom aggregation functions
- [ ] Metrics dashboards
- [ ] Alert manager integration
- [ ] Kubernetes metrics collection

## 🙏 Acknowledgments

- **Prometheus**: Metrics collection and monitoring
- **StatsD**: Simple metrics daemon
- **InfluxDB**: Time-series database
- **PHP**: Zend API for extension development

---

**Built with ❤️ for observable PHP applications**
