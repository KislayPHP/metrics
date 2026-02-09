<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_metrics.so example.php

extension_loaded('kislayphp_metrics') or die('kislayphp_metrics not loaded');

$metrics = new KislayPHP\Metrics\Metrics();

class ArrayMetricsClient implements KislayPHP\Metrics\ClientInterface {
	private array $counters = [];

	public function inc(string $name, ?int $by = 1): bool {
		$this->counters[$name] = ($this->counters[$name] ?? 0) + ($by ?? 1);
		return true;
	}

	public function get(string $name): int {
		return (int)($this->counters[$name] ?? 0);
	}

	public function all(): array {
		return $this->counters;
	}
}

$use_client = false;
if ($use_client) {
	$metrics->setClient(new ArrayMetricsClient());
}
$metrics->inc('requests');
$metrics->inc('requests', 2);

var_dump($metrics->get('requests'));
print_r($metrics->all());
