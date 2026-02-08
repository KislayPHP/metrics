# KislayPHP Metrics

Metrics extension for KislayPHP.

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
