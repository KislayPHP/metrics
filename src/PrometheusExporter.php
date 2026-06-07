<?php
declare(strict_types=1);

namespace Kislay\Metrics;

/**
 * Exports kislayphp metrics in Prometheus text exposition format (v0.0.4).
 *
 * Equivalent to Micrometer's PrometheusMeterRegistry scrape endpoint.
 *
 * @example
 *   $metrics = new Metrics();
 *   // In your bootstrap:
 *   PrometheusExporter::registerEndpoint($app, $metrics, '/metrics', 'myapp');
 *
 *   // Or export manually:
 *   $exporter = new PrometheusExporter($metrics, 'myapp');
 *   $exporter->help('http_requests_total', 'Total HTTP requests received');
 *   $exporter->type('http_requests_total', 'counter');
 *   header('Content-Type: text/plain; version=0.0.4');
 *   echo $exporter->export();
 */
class PrometheusExporter
{
    /** @var array<string, string> */
    private array $helpText = [];

    /** @var array<string, string> */
    private array $metricTypes = [];

    public function __construct(
        private readonly Metrics $metrics,
        private readonly string $namespace = ''
    ) {}

    // ── Configuration ─────────────────────────────────────────────────────────

    /** Set the HELP comment text for a metric. */
    public function help(string $name, string $text): static
    {
        $this->helpText[$name] = $text;
        return $this;
    }

    /** Set the TYPE (counter, gauge, histogram, summary, untyped) for a metric. */
    public function type(string $name, string $type): static
    {
        $this->metricTypes[$name] = $type;
        return $this;
    }

    // ── Export ────────────────────────────────────────────────────────────────

    /**
     * Render all metrics as a Prometheus text exposition string.
     */
    public function export(): string
    {
        $all   = $this->metrics->all();
        $lines = [];

        foreach ($all as $name => $value) {
            $fullName = $this->buildMetricName($name);
            $type     = $this->metricTypes[$name] ?? 'counter';
            $help     = $this->helpText[$name]    ?? str_replace('_', ' ', $name);

            $lines[] = "# HELP {$fullName} {$help}";
            $lines[] = "# TYPE {$fullName} {$type}";
            $lines[] = "{$fullName} {$value}";
            $lines[] = '';
        }

        return implode("\n", $lines);
    }

    /**
     * Register a Prometheus-compatible /metrics scrape endpoint on a kislayphp App.
     *
     * @param \Kislay\Core\App $app
     */
    public static function registerEndpoint(
        object  $app,
        Metrics $metrics,
        string  $path      = '/metrics',
        string  $namespace = ''
    ): void {
        $exporter = new self($metrics, $namespace);

        $app->get($path, static function ($req, $res) use ($exporter): void {
            $res->header('Content-Type', 'text/plain; version=0.0.4; charset=utf-8');
            $res->send($exporter->export());
        });
    }

    // ── Internals ─────────────────────────────────────────────────────────────

    /** Build a fully-qualified Prometheus metric name. */
    private function buildMetricName(string $name): string
    {
        $full = $this->namespace ? "{$this->namespace}_{$name}" : $name;
        // Prometheus metric names: [a-zA-Z_:][a-zA-Z0-9_:]*
        return preg_replace('/[^a-zA-Z0-9_:]/', '_', $full);
    }
}
