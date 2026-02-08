# kislayphp_metrics

Metrics extension for KislayPHP.

## Build

```sh
phpize
./configure --enable-kislayphp_metrics
make
```

## Run Locally

```sh
cd /path/to/phpExtension/kislayphp_metrics
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
