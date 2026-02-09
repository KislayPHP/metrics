# KislayPHP Metrics

KislayPHP Metrics provides counters for lightweight telemetry in PHP microservices.

## Key Features

- Increment and read counters in memory or via a custom client.
- Simple API with minimal overhead.

## Use Cases

- Track request counts in local services.
- Quick instrumentation for demos.

## SEO Keywords

PHP metrics, counters, telemetry, in-memory metrics, C++ PHP extension, microservices

## Repository

- https://github.com/KislayPHP/metrics

## Related Modules

- https://github.com/KislayPHP/core
- https://github.com/KislayPHP/eventbus
- https://github.com/KislayPHP/discovery
- https://github.com/KislayPHP/gateway
- https://github.com/KislayPHP/config
- https://github.com/KislayPHP/queue

## Build

```sh
phpize
./configure --enable-kislayphp_metrics
make
```

## Run Locally

```sh
cd /path/to/metrics
php -d extension=modules/kislayphp_metrics.so example.php
```

## Custom Client Interface

Default is in-memory. To plug in Redis, MySQL, Mongo, or any other backend, provide
your own PHP client that implements `KislayPHP\Metrics\ClientInterface` and call
`setClient()`.

Example:

```php
$metrics = new KislayPHP\Metrics\Metrics();
$metrics->setClient(new MyMetricsClient());
```

## Example

```php
<?php
extension_loaded('kislayphp_metrics') or die('kislayphp_metrics not loaded');

$metrics = new KislayPHP\Metrics\Metrics();
$metrics->inc('requests');
$metrics->inc('requests', 2);

var_dump($metrics->get('requests'));
print_r($metrics->all());
?>
```
