# KislayPHP Metrics Extension Documentation

## Overview

The KislayPHP Metrics extension provides high-performance application metrics collection and monitoring capabilities. It supports counters, histograms, and gauges with pluggable client interfaces for external metrics backends like Prometheus, StatsD, or DataDog.

## Architecture

### Metrics Types
- **Counters**: Monotonically increasing values (requests served, errors occurred)
- **Histograms**: Distribution of values with percentiles (request latency, response sizes)
- **Gauges**: Point-in-time values (current connections, memory usage)

### Client Interface Pattern
The extension uses a client interface allowing different metrics backends:
- Prometheus/Pushgateway
- StatsD
- DataDog
- InfluxDB
- Custom implementations

## Installation

### Via PIE
```bash
pie install kislayphp/metrics
```

### Manual Build
```bash
cd kislayphp_metrics/
phpize && ./configure --enable-kislayphp_metrics && make && make install
```

### php.ini Configuration
```ini
extension=kislayphp_metrics.so
```

## API Reference

### KislayPHP\\Metrics\\Metrics Class

The main metrics collection class.

#### Constructor
```php
$metrics = new KislayPHP\\Metrics\\Metrics();
```

#### Counter Operations
```php
$metrics->increment(string $name, int $value = 1): void
$metrics->getCounter(string $name): int
$metrics->resetCounter(string $name): void
```

#### Client Integration
```php
$metrics->setClient(KislayPHP\\Metrics\\ClientInterface $client): bool
```

### KislayPHP\\Metrics\\Counter Class

Standalone counter implementation.

#### Constructor
```php
$counter = new KislayPHP\\Metrics\\Counter(string $name);
```

#### Operations
```php
$counter->increment(int $value = 1): void
$counter->get(): int
$counter->reset(): void
```

### KislayPHP\\Metrics\\Histogram Class

Histogram for measuring distributions.

#### Constructor
```php
$histogram = new KislayPHP\\Metrics\\Histogram(string $name);
```

#### Operations
```php
$histogram->observe(float $value): void
$histogram->getPercentile(float $percentile): float
$histogram->getCount(): int
$histogram->getSum(): float
```

### KislayPHP\\Metrics\\ClientInterface

Interface for external metrics clients.

```php
interface ClientInterface {
    public function increment(string $name, int $value = 1): bool;
    public function gauge(string $name, float $value): bool;
    public function histogram(string $name, float $value): bool;
    public function timing(string $name, float $duration): bool;
}
```

## Usage Examples

### Basic Counter Usage
```php
<?php
use KislayPHP\\Metrics\\Counter;

$requestCounter = new Counter('http_requests_total');

// Increment counter for each request
$app->use(function($req, $res, $next) use ($requestCounter) {
    $requestCounter->increment();
    $next();
});

// Get current count
$totalRequests = $requestCounter->get();
echo "Total requests: $totalRequests\n";
```

### Request Latency Histogram
```php
<?php
use KislayPHP\\Metrics\\Histogram;

$latencyHistogram = new Histogram('http_request_duration_seconds');

$app->use(function($req, $res, $next) use ($latencyHistogram) {
    $start = microtime(true);
    $next();
    $duration = microtime(true) - $start;
    $latencyHistogram->observe($duration);
});

// Get latency percentiles
$p50 = $latencyHistogram->getPercentile(50);  // Median
$p95 = $latencyHistogram->getPercentile(95);  // 95th percentile
$p99 = $latencyHistogram->getPercentile(99);  // 99th percentile

echo "P50: {$p50}s, P95: {$p95}s, P99: {$p99}s\n";
```

### Error Rate Monitoring
```php
<?php
use KislayPHP\\Metrics\\Counter;

$totalRequests = new Counter('http_requests_total');
$errorRequests = new Counter('http_requests_errors_total');

$app->get('/api/data', function($req, $res) use ($totalRequests, $errorRequests) {
    $totalRequests->increment();

    try {
        $data = fetchData();
        $res->json($data);
    } catch (Exception $e) {
        $errorRequests->increment();
        $res->internalServerError('Failed to fetch data');
    }
});

// Calculate error rate
$app->get('/metrics/error-rate', function($req, $res) use ($totalRequests, $errorRequests) {
    $total = $totalRequests->get();
    $errors = $errorRequests->get();

    $errorRate = $total > 0 ? ($errors / $total) * 100 : 0;

    $res->json([
        'total_requests' => $total,
        'error_requests' => $errors,
        'error_rate_percent' => round($errorRate, 2)
    ]);
});
```

### Memory Usage Gauge
```php
<?php
use KislayPHP\\Metrics\\Metrics;

$metrics = new Metrics();

// Track memory usage
$app->use(function($req, $res, $next) use ($metrics) {
    $next();

    $memoryUsage = memory_get_peak_usage(true);
    $metrics->gauge('memory_usage_bytes', $memoryUsage);
});

// Get current memory usage
$currentMemory = $metrics->getGauge('memory_usage_bytes');
echo "Current memory usage: " . round($currentMemory / 1024 / 1024, 2) . " MB\n";
```

### Database Connection Pool Monitoring
```php
<?php
use KislayPHP\\Metrics\\Metrics;

$metrics = new Metrics();

class DatabaseConnectionPool {
    private $metrics;
    private $activeConnections = 0;
    private $maxConnections = 10;

    public function __construct(Metrics $metrics) {
        $this->metrics = $metrics;
    }

    public function getConnection() {
        if ($this->activeConnections >= $this->maxConnections) {
            throw new Exception('Connection pool exhausted');
        }

        $this->activeConnections++;
        $this->metrics->gauge('db_connections_active', $this->activeConnections);

        // Return actual database connection
        return $this->createConnection();
    }

    public function releaseConnection($connection) {
        $this->activeConnections--;
        $this->metrics->gauge('db_connections_active', $this->activeConnections);

        // Close/release the connection
        $connection->close();
    }

    public function getStats() {
        return [
            'active' => $this->activeConnections,
            'max' => $this->maxConnections,
            'available' => $this->maxConnections - $this->activeConnections
        ];
    }
}

// Usage
$pool = new DatabaseConnectionPool($metrics);

$app->get('/api/users', function($req, $res) use ($pool) {
    $connection = $pool->getConnection();

    try {
        $users = $connection->query('SELECT * FROM users');
        $res->json($users);
    } finally {
        $pool->releaseConnection($connection);
    }
});

$app->get('/metrics/db-pool', function($req, $res) use ($pool) {
    $res->json($pool->getStats());
});
```

## Client Implementations

### Prometheus Pushgateway Client
```php
<?php
class PrometheusPushgatewayClient implements KislayPHP\\Metrics\\ClientInterface {
    private $gatewayUrl;
    private $jobName;
    private $httpClient;

    public function __construct(string $gatewayUrl, string $jobName = 'kislayphp') {
        $this->gatewayUrl = rtrim($gatewayUrl, '/');
        $this->jobName = $jobName;
        $this->httpClient = new GuzzleHttp\\Client();
    }

    public function increment(string $name, int $value = 1): bool {
        return $this->pushMetric($name, $value, 'counter');
    }

    public function gauge(string $name, float $value): bool {
        return $this->pushMetric($name, $value, 'gauge');
    }

    public function histogram(string $name, float $value): bool {
        return $this->pushMetric($name, $value, 'histogram');
    }

    public function timing(string $name, float $duration): bool {
        return $this->histogram($name, $duration);
    }

    private function pushMetric(string $name, $value, string $type): bool {
        $prometheusFormat = "# TYPE $name $type\n$name $value\n";

        try {
            $this->httpClient->post("{$this->gatewayUrl}/metrics/job/{$this->jobName}", [
                'body' => $prometheusFormat,
                'headers' => [
                    'Content-Type' => 'text/plain; charset=utf-8'
                ]
            ]);
            return true;
        } catch (Exception $e) {
            error_log("Failed to push metric to Prometheus: " . $e->getMessage());
            return false;
        }
    }
}

// Usage
$metrics = new KislayPHP\\Metrics\\Metrics();
$prometheusClient = new PrometheusPushgatewayClient('http://prometheus-pushgateway:9091');
$metrics->setClient($prometheusClient);

// Metrics are now automatically pushed to Prometheus
```

### StatsD Client
```php
<?php
class StatsDClient implements KislayPHP\\Metrics\\ClientInterface {
    private $host;
    private $port;
    private $socket;
    private $prefix;

    public function __construct(string $host = 'localhost', int $port = 8125, string $prefix = '') {
        $this->host = $host;
        $this->port = $port;
        $this->prefix = $prefix;
        $this->connect();
    }

    private function connect(): void {
        $this->socket = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
    }

    public function increment(string $name, int $value = 1): bool {
        return $this->send("$name:$value|c");
    }

    public function gauge(string $name, float $value): bool {
        return $this->send("$name:$value|g");
    }

    public function histogram(string $name, float $value): bool {
        return $this->send("$name:$value|h");
    }

    public function timing(string $name, float $duration): bool {
        return $this->send("$name:" . round($duration * 1000) . "|ms");
    }

    private function send(string $message): bool {
        if (!$this->socket) {
            return false;
        }

        $fullMessage = $this->prefix ? $this->prefix . '.' . $message : $message;

        return socket_sendto(
            $this->socket,
            $fullMessage,
            strlen($fullMessage),
            0,
            $this->host,
            $this->port
       ) !== false;
    }

    public function __destruct() {
        if ($this->socket) {
            socket_close($this->socket);
        }
    }
}

// Usage
$metrics = new KislayPHP\\Metrics\\Metrics();
$statsdClient = new StatsDClient('statsd-server', 8125, 'kislayphp');
$metrics->setClient($statsdClient);
```

### DataDog Client
```php
<?php
class DataDogClient implements KislayPHP\\Metrics\\ClientInterface {
    private $apiKey;
    private $appKey;
    private $httpClient;
    private $tags;

    public function __construct(string $apiKey, string $appKey, array $tags = []) {
        $this->apiKey = $apiKey;
        $this->appKey = $appKey;
        $this->tags = $tags;
        $this->httpClient = new GuzzleHttp\\Client();
    }

    public function increment(string $name, int $value = 1): bool {
        return $this->sendMetric($name, $value, 'count');
    }

    public function gauge(string $name, float $value): bool {
        return $this->sendMetric($name, $value, 'gauge');
    }

    public function histogram(string $name, float $value): bool {
        return $this->sendMetric($name, $value, 'histogram');
    }

    public function timing(string $name, float $duration): bool {
        return $this->sendMetric($name, $duration * 1000, 'histogram', ['unit' => 'millisecond']);
    }

    private function sendMetric(string $name, $value, string $type, array $options = []): bool {
        $data = [
            'series' => [
                [
                    'metric' => $name,
                    'points' => [['timestamp' => time(), 'value' => $value]],
                    'type' => $type,
                    'tags' => $this->tags
                ]
            ]
        ];

        if (isset($options['unit'])) {
            $data['series'][0]['unit'] = $options['unit'];
        }

        try {
            $this->httpClient->post('https://api.datadoghq.com/api/v1/series', [
                'json' => $data,
                'query' => [
                    'api_key' => $this->apiKey,
                    'application_key' => $this->appKey
                ]
            ]);
            return true;
        } catch (Exception $e) {
            error_log("Failed to send metric to DataDog: " . $e->getMessage());
            return false;
        }
    }
}

// Usage
$metrics = new KislayPHP\\Metrics\\Metrics();
$datadogClient = new DataDogClient(
    getenv('DATADOG_API_KEY'),
    getenv('DATADOG_APP_KEY'),
    ['env' => 'production', 'service' => 'kislayphp']
);
$metrics->setClient($datadogClient);
```

### InfluxDB Client
```php
<?php
class InfluxDBClient implements KislayPHP\\Metrics\\ClientInterface {
    private $host;
    private $port;
    private $database;
    private $username;
    private $password;
    private $httpClient;

    public function __construct(
        string $host = 'localhost',
        int $port = 8086,
        string $database = 'metrics',
        string $username = '',
        string $password = ''
    ) {
        $this->host = $host;
        $this->port = $port;
        $this->database = $database;
        $this->username = $username;
        $this->password = $password;
        $this->httpClient = new GuzzleHttp\\Client();
    }

    public function increment(string $name, int $value = 1): bool {
        return $this->writePoint("$name value=$value");
    }

    public function gauge(string $name, float $value): bool {
        return $this->writePoint("$name value=$value");
    }

    public function histogram(string $name, float $value): bool {
        return $this->writePoint("$name value=$value");
    }

    public function timing(string $name, float $duration): bool {
        return $this->writePoint("$name duration=$duration");
    }

    private function writePoint(string $line): bool {
        $url = "http://{$this->host}:{$this->port}/write";
        $query = ['db' => $this->database];

        if ($this->username && $this->password) {
            $query['u'] = $this->username;
            $query['p'] = $this->password;
        }

        try {
            $this->httpClient->post($url, [
                'query' => $query,
                'body' => $line,
                'headers' => [
                    'Content-Type' => 'application/x-www-form-urlencoded'
                ]
            ]);
            return true;
        } catch (Exception $e) {
            error_log("Failed to write to InfluxDB: " . $e->getMessage());
            return false;
        }
    }
}

// Usage
$metrics = new KislayPHP\\Metrics\\Metrics();
$influxClient = new InfluxDBClient('influxdb-server', 8086, 'kislayphp_metrics');
$metrics->setClient($influxClient);
```

## Advanced Usage

### Custom Metrics Collector
```php
<?php
class CustomMetricsCollector {
    private $metrics;
    private $collectors = [];

    public function __construct(KislayPHP\\Metrics\\Metrics $metrics) {
        $this->metrics = $metrics;
    }

    public function addCollector(string $name, callable $collector): void {
        $this->collectors[$name] = $collector;
    }

    public function collect(): void {
        foreach ($this->collectors as $name => $collector) {
            try {
                $value = $collector();
                if (is_numeric($value)) {
                    $this->metrics->gauge($name, $value);
                }
            } catch (Exception $e) {
                error_log("Failed to collect metric $name: " . $e->getMessage());
            }
        }
    }

    // Predefined collectors
    public static function memoryUsage(): callable {
        return function() {
            return memory_get_usage(true);
        };
    }

    public static function cpuUsage(): callable {
        return function() {
            // Simple CPU usage estimation
            $start = microtime(true);
            $load = sys_getloadavg();
            return $load[0] ?? 0;
        };
    }

    public static function diskUsage(string $path = '/'): callable {
        return function() use ($path) {
            $stats = statvfs($path);
            $total = $stats['blocks'] * $stats['bsize'];
            $free = $stats['bfree'] * $stats['bsize'];
            return ($total - $free) / $total * 100;
        };
    }
}

// Usage
$collector = new CustomMetricsCollector($metrics);

// Add custom collectors
$collector->addCollector('memory_usage_bytes', CustomMetricsCollector::memoryUsage());
$collector->addCollector('cpu_load', CustomMetricsCollector::cpuUsage());
$collector->addCollector('disk_usage_percent', CustomMetricsCollector::diskUsage('/'));

// Collect metrics periodically
$app->get('/metrics/system', function($req, $res) use ($collector) {
    $collector->collect();
    $res->json(['status' => 'collected']);
});
```

### Metrics Middleware
```php
<?php
class MetricsMiddleware {
    private $metrics;

    public function __construct(KislayPHP\\Metrics\\Metrics $metrics) {
        $this->metrics = $metrics;
    }

    public function __invoke($req, $res, $next) {
        $start = microtime(true);
        $method = $req->method();
        $path = $req->path();

        // Increment request counter
        $this->metrics->increment('http_requests_total', 1);

        // Track requests by method
        $this->metrics->increment("http_requests_method_{$method}_total", 1);

        // Track requests by path (be careful with high cardinality)
        $sanitizedPath = $this->sanitizePath($path);
        $this->metrics->increment("http_requests_path_{$sanitizedPath}_total", 1);

        $next();

        // Record response time
        $duration = microtime(true) - $start;
        $this->metrics->histogram('http_request_duration_seconds', $duration);

        // Track response status
        $statusCode = $res->getStatusCode();
        $this->metrics->increment("http_responses_status_{$statusCode}_total", 1);

        // Track response size
        $responseSize = strlen($res->getBody());
        $this->metrics->histogram('http_response_size_bytes', $responseSize);
    }

    private function sanitizePath(string $path): string {
        // Replace path parameters with placeholders to avoid high cardinality
        return preg_replace('/\/[0-9]+/', '/:id', $path);
    }
}

// Usage
$metricsMiddleware = new MetricsMiddleware($metrics);
$app->use($metricsMiddleware);
```

### Alerting Based on Metrics
```php
<?php
class MetricsAlertManager {
    private $metrics;
    private $alerts = [];
    private $thresholds = [];

    public function __construct(KislayPHP\\Metrics\\Metrics $metrics) {
        $this->metrics = $metrics;
    }

    public function addThreshold(string $metricName, float $threshold, callable $alertCallback): void {
        $this->thresholds[$metricName] = [
            'threshold' => $threshold,
            'callback' => $alertCallback,
            'last_alert' => 0
        ];
    }

    public function checkAlerts(): void {
        foreach ($this->thresholds as $metricName => $config) {
            $currentValue = $this->metrics->getGauge($metricName);

            if ($currentValue > $config['threshold']) {
                // Only alert once per minute to avoid spam
                if (time() - $config['last_alert'] > 60) {
                    $config['callback']($metricName, $currentValue, $config['threshold']);
                    $this->thresholds[$metricName]['last_alert'] = time();
                }
            }
        }
    }

    // Predefined alert callbacks
    public static function logAlert(): callable {
        return function($metric, $value, $threshold) {
            error_log("ALERT: $metric exceeded threshold. Current: $value, Threshold: $threshold");
        };
    }

    public static function emailAlert(string $to, string $subject): callable {
        return function($metric, $value, $threshold) use ($to, $subject) {
            $message = "Alert: $metric exceeded threshold\nCurrent value: $value\nThreshold: $threshold";
            mail($to, $subject, $message);
        };
    }
}

// Usage
$alertManager = new MetricsAlertManager($metrics);

// Add alerts
$alertManager->addThreshold('memory_usage_bytes', 100 * 1024 * 1024, MetricsAlertManager::logAlert());
$alertManager->addThreshold('http_request_duration_seconds', 5.0, MetricsAlertManager::emailAlert(
    'admin@example.com',
    'High Response Time Alert'
));

// Check alerts periodically
$app->get('/check-alerts', function($req, $res) use ($alertManager) {
    $alertManager->checkAlerts();
    $res->json(['status' => 'checked']);
});
```

### Metrics Dashboard
```php
<?php
class MetricsDashboard {
    private $metrics;

    public function __construct(KislayPHP\\Metrics\\Metrics $metrics) {
        $this->metrics = $metrics;
    }

    public function getDashboardData(): array {
        return [
            'requests' => [
                'total' => $this->metrics->getCounter('http_requests_total'),
                'per_second' => $this->calculateRate('http_requests_total'),
                'by_method' => $this->getMethodBreakdown(),
                'by_status' => $this->getStatusBreakdown()
            ],
            'performance' => [
                'avg_response_time' => $this->metrics->getHistogram('http_request_duration_seconds')->getPercentile(50),
                'p95_response_time' => $this->metrics->getHistogram('http_request_duration_seconds')->getPercentile(95),
                'p99_response_time' => $this->metrics->getHistogram('http_request_duration_seconds')->getPercentile(99)
            ],
            'system' => [
                'memory_usage' => $this->metrics->getGauge('memory_usage_bytes'),
                'cpu_usage' => $this->metrics->getGauge('cpu_usage_percent'),
                'active_connections' => $this->metrics->getGauge('active_connections')
            ],
            'errors' => [
                'total_errors' => $this->metrics->getCounter('http_requests_errors_total'),
                'error_rate' => $this->calculateErrorRate()
            ]
        ];
    }

    private function calculateRate(string $counterName): float {
        // Simplified rate calculation - in practice you'd want more sophisticated logic
        static $lastValue = [];
        static $lastTime = [];

        $currentValue = $this->metrics->getCounter($counterName);
        $currentTime = microtime(true);

        if (!isset($lastValue[$counterName])) {
            $lastValue[$counterName] = $currentValue;
            $lastTime[$counterName] = $currentTime;
            return 0.0;
        }

        $timeDiff = $currentTime - $lastTime[$counterName];
        $valueDiff = $currentValue - $lastValue[$counterName];

        $lastValue[$counterName] = $currentValue;
        $lastTime[$counterName] = $currentTime;

        return $timeDiff > 0 ? $valueDiff / $timeDiff : 0.0;
    }

    private function getMethodBreakdown(): array {
        return [
            'GET' => $this->metrics->getCounter('http_requests_method_GET_total'),
            'POST' => $this->metrics->getCounter('http_requests_method_POST_total'),
            'PUT' => $this->metrics->getCounter('http_requests_method_PUT_total'),
            'DELETE' => $this->metrics->getCounter('http_requests_method_DELETE_total')
        ];
    }

    private function getStatusBreakdown(): array {
        return [
            '2xx' => $this->metrics->getCounter('http_responses_status_2xx_total'),
            '4xx' => $this->metrics->getCounter('http_responses_status_4xx_total'),
            '5xx' => $this->metrics->getCounter('http_responses_status_5xx_total')
        ];
    }

    private function calculateErrorRate(): float {
        $totalRequests = $this->metrics->getCounter('http_requests_total');
        $errorRequests = $this->metrics->getCounter('http_requests_errors_total');

        return $totalRequests > 0 ? ($errorRequests / $totalRequests) * 100 : 0.0;
    }
}

// Usage
$dashboard = new MetricsDashboard($metrics);

$app->get('/dashboard', function($req, $res) use ($dashboard) {
    $data = $dashboard->getDashboardData();
    $res->json($data);
});
```

## Integration Examples

### KislayPHP Integration
```php
<?php
// config/kislay.php
return [
    'metrics' => [
        'client' => env('METRICS_CLIENT', 'prometheus'),
        'prometheus' => [
            'gateway_url' => env('PROMETHEUS_PUSHGATEWAY_URL'),
            'job_name' => env('APP_NAME', 'kislay-app')
        ],
        'statsd' => [
            'host' => env('STATSD_HOST', 'localhost'),
            'port' => env('STATSD_PORT', 8125)
        ]
    ]
];

// In service provider
use Illuminate\\Support\\ServiceProvider;
use KislayPHP\\Metrics\\Metrics;
use KislayPHP\\Metrics\\PrometheusPushgatewayClient;
use KislayPHP\\Metrics\\StatsDClient;

class KislayServiceProvider extends ServiceProvider {
    public function register() {
        $this->app->singleton(Metrics::class, function($app) {
            $metrics = new Metrics();
            $clientType = config('kislay.metrics.client');

            switch ($clientType) {
                case 'prometheus':
                    $client = new PrometheusPushgatewayClient(
                        config('kislay.metrics.prometheus.gateway_url'),
                        config('kislay.metrics.prometheus.job_name')
                    );
                    break;
                case 'statsd':
                    $client = new StatsDClient(
                        config('kislay.metrics.statsd.host'),
                        config('kislay.metrics.statsd.port')
                    );
                    break;
                default:
                    // Use in-memory metrics
                    return $metrics;
            }

            $metrics->setClient($client);
            return $metrics;
        });
    }
}

// In middleware
class MetricsMiddleware {
    public function handle($request, $next) {
        $metrics = app(Metrics::class);
        $start = microtime(true);

        $metrics->increment('http_requests_total');

        $response = $next($request);

        $duration = microtime(true) - $start;
        $metrics->histogram('http_request_duration_seconds', $duration);

        return $response;
    }
}
```

### Framework Integration
```php
<?php
// src/Service/KislayMetrics.php
namespace App\\Service;

use KislayPHP\\Metrics\\Metrics;
use KislayPHP\\Metrics\\PrometheusPushgatewayClient;

class KislayMetrics extends Metrics {
    public function __construct(string $pushgatewayUrl = null, string $jobName = null) {
        parent::__construct();

        if ($pushgatewayUrl) {
            $client = new PrometheusPushgatewayClient($pushgatewayUrl, $jobName ?: 'kislay-app');
            $this->setClient($client);
        }
    }
}

// services.yaml
services:
    App\\Service\\KislayMetrics:
        arguments:
            $pushgatewayUrl: '%env(PROMETHEUS_PUSHGATEWAY_URL)%'
            $jobName: '%env(APP_NAME)%'

// In controller
class MetricsController extends AbstractController {
    public function dashboard(KislayMetrics $metrics) {
        $data = [
            'requests_total' => $metrics->getCounter('http_requests_total'),
            'avg_response_time' => $metrics->getHistogram('http_request_duration_seconds')->getPercentile(50)
        ];

        return $this->json($data);
    }
}
```

## Testing

### Unit Testing
```php
<?php
use PHPUnit\\Framework\\TestCase;
use KislayPHP\\Metrics\\Counter;
use KislayPHP\\Metrics\\Histogram;

class MetricsTest extends TestCase {
    public function testCounter() {
        $counter = new Counter('test_counter');

        $this->assertEquals(0, $counter->get());

        $counter->increment();
        $this->assertEquals(1, $counter->get());

        $counter->increment(5);
        $this->assertEquals(6, $counter->get());

        $counter->reset();
        $this->assertEquals(0, $counter->get());
    }

    public function testHistogram() {
        $histogram = new Histogram('test_histogram');

        $histogram->observe(1.0);
        $histogram->observe(2.0);
        $histogram->observe(3.0);

        $this->assertEquals(3, $histogram->getCount());
        $this->assertEquals(6.0, $histogram->getSum());

        // Test percentiles (simplified)
        $p50 = $histogram->getPercentile(50);
        $this->assertGreaterThanOrEqual(1.0, $p50);
        $this->assertLessThanOrEqual(3.0, $p50);
    }

    public function testMetricsCollection() {
        $metrics = new KislayPHP\\Metrics\\Metrics();

        $metrics->increment('test_metric', 5);
        $this->assertEquals(5, $metrics->getCounter('test_metric'));

        $metrics->gauge('test_gauge', 42.5);
        $this->assertEquals(42.5, $metrics->getGauge('test_gauge'));
    }
}
```

### Mock Client for Testing
```php
<?php
class MockMetricsClient implements KislayPHP\\Metrics\\ClientInterface {
    public $metrics = [];

    public function increment(string $name, int $value = 1): bool {
        $this->metrics[$name] = ($this->metrics[$name] ?? 0) + $value;
        return true;
    }

    public function gauge(string $name, float $value): bool {
        $this->metrics[$name] = $value;
        return true;
    }

    public function histogram(string $name, float $value): bool {
        if (!isset($this->metrics[$name])) {
            $this->metrics[$name] = [];
        }
        $this->metrics[$name][] = $value;
        return true;
    }

    public function timing(string $name, float $duration): bool {
        return $this->histogram($name, $duration);
    }

    public function getMetric(string $name) {
        return $this->metrics[$name] ?? null;
    }

    public function getAllMetrics(): array {
        return $this->metrics;
    }
}

// Usage in tests
class ApplicationTest extends TestCase {
    public function testMetricsAreCollected() {
        $mockClient = new MockMetricsClient();
        $metrics = new KislayPHP\\Metrics\\Metrics();
        $metrics->setClient($mockClient);

        // Simulate application usage
        $metrics->increment('requests_total', 10);
        $metrics->gauge('active_users', 25);
        $metrics->histogram('response_time', 0.5);

        // Verify metrics were sent to client
        $this->assertEquals(10, $mockClient->getMetric('requests_total'));
        $this->assertEquals(25, $mockClient->getMetric('active_users'));
        $this->assertIsArray($mockClient->getMetric('response_time'));
    }
}
```

### Integration Testing
```php
<?php
class MetricsIntegrationTest extends PHPUnit\\Framework\\TestCase {
    private static $serverProcess;

    public static function setUpBeforeClass(): void {
        // Start a test metrics server
        self::$serverProcess = proc_open(
            'php -d extension=kislayphp_metrics.so test_metrics_server.php',
            [],
            $pipes,
            __DIR__ . '/fixtures',
            []
        );
        sleep(2);
    }

    public static function tearDownAfterClass(): void {
        if (self::$serverProcess) {
            proc_terminate(self::$serverProcess);
            proc_close(self::$serverProcess);
        }
    }

    public function testMetricsCollection() {
        $metrics = new KislayPHP\\Metrics\\Metrics();
        $httpClient = new HttpMetricsClient('http://localhost:9090');
        $metrics->setClient($httpClient);

        // Test counter
        $result = $metrics->increment('test_counter', 5);
        $this->assertTrue($result);

        // Test gauge
        $result = $metrics->gauge('test_gauge', 42.5);
        $this->assertTrue($result);

        // Test histogram
        $result = $metrics->histogram('test_histogram', 1.5);
        $this->assertTrue($result);

        // Verify metrics were received by server
        $response = file_get_contents('http://localhost:9090/metrics');
        $serverMetrics = json_decode($response, true);

        $this->assertEquals(5, $serverMetrics['test_counter']);
        $this->assertEquals(42.5, $serverMetrics['test_gauge']);
        $this->assertContains(1.5, $serverMetrics['test_histogram']);
    }
}
```

## Troubleshooting

### Common Issues

#### Metrics Not Being Collected
**Symptoms:** Metrics values remain at 0 or don't update

**Solutions:**
1. Check if metrics client is properly set: `$metrics->setClient($client)`
2. Verify client connection to backend service
3. Check client-specific error logs
4. Ensure metrics are being called in the right places

#### High Memory Usage
**Symptoms:** Memory usage grows when collecting many metrics

**Solutions:**
1. Use appropriate metric names to avoid high cardinality
2. Implement metric expiration/cleanup
3. Use sampling for high-frequency metrics
4. Monitor memory usage patterns

#### Performance Impact
**Symptoms:** Application performance degrades with metrics collection

**Solutions:**
1. Use asynchronous metric submission
2. Implement batching for high-frequency metrics
3. Use sampling for detailed metrics
4. Profile metrics collection overhead

### Debug Logging
```php
<?php
class DebugMetrics extends KislayPHP\\Metrics\\Metrics {
    public function increment(string $name, int $value = 1): void {
        error_log("Incrementing metric: $name by $value");
        parent::increment($name, $value);
    }

    public function gauge(string $name, float $value): void {
        error_log("Setting gauge: $name = $value");
        parent::gauge($name, $value);
    }

    public function histogram(string $name, float $value): void {
        error_log("Recording histogram: $name = $value");
        parent::histogram($name, $value);
    }
}

// Usage
$metrics = new DebugMetrics();
// All metric operations will now be logged
```

### Performance Monitoring
```php
<?php
class MonitoredMetrics extends KislayPHP\\Metrics\\Metrics {
    private $operationCount = [];
    private $operationTime = [];

    public function increment(string $name, int $value = 1): void {
        $start = microtime(true);
        parent::increment($name, $value);
        $this->recordOperation('increment', microtime(true) - $start);
    }

    private function recordOperation(string $operation, float $duration): void {
        $this->operationCount[$operation] = ($this->operationCount[$operation] ?? 0) + 1;
        $this->operationTime[$operation] = ($this->operationTime[$operation] ?? 0) + $duration;
    }

    public function getPerformanceStats(): array {
        $stats = [];
        foreach ($this->operationCount as $operation => $count) {
            $stats[$operation] = [
                'count' => $count,
                'total_time' => $this->operationTime[$operation],
                'avg_time' => $this->operationTime[$operation] / $count
            ];
        }
        return $stats;
    }
}

// Usage
$metrics = new MonitoredMetrics();
// ... use metrics ...
$stats = $metrics->getPerformanceStats();
print_r($stats);
```

## Best Practices

### Metric Naming Conventions
1. **Use lowercase with underscores**: `http_requests_total`, `response_time_seconds`
2. **Include units in name**: `response_time_seconds`, `memory_usage_bytes`
3. **Use prefixes for categorization**: `http_`, `db_`, `cache_`
4. **Be consistent across services**: Use same naming patterns

### Cardinality Management
1. **Avoid high cardinality labels**: Don't use user IDs, timestamps in metric names
2. **Use appropriate aggregation**: Group similar metrics instead of individual ones
3. **Implement metric expiration**: Clean up old/unused metrics
4. **Monitor cardinality growth**: Alert when metrics exceed expected bounds

### Collection Strategies
1. **Use appropriate metric types**: Counters for events, gauges for states, histograms for distributions
2. **Implement sampling**: For high-frequency, detailed metrics
3. **Batch metric submissions**: Reduce network overhead
4. **Use asynchronous collection**: Don't block application flow

### Alerting and Monitoring
1. **Define clear thresholds**: Based on historical data and business requirements
2. **Implement multiple alert levels**: Warning, critical, emergency
3. **Use appropriate time windows**: Short for immediate issues, long for trends
4. **Include context in alerts**: What service, what metric, what threshold

### Security Considerations
1. **Protect metrics endpoints**: Use authentication and authorization
2. **Encrypt metric data**: In transit and at rest if sensitive
3. **Audit metric access**: Log who accesses metrics and when
4. **Rate limit metric collection**: Prevent abuse of metrics APIs

This comprehensive documentation covers all aspects of the KislayPHP Metrics extension, from basic usage to advanced implementations and best practices.