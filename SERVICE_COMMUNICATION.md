# Service Communication Guide (Metrics)

This extension tracks service-to-service traffic and error rates.

## Namespace

- Primary: `Kislay\Metrics\Metrics`
- Backward compatible alias: `KislayPHP\Metrics\Metrics`

## Pattern

Use low-cardinality metric keys for inter-service calls:

- `svc.call.total.<from>.<to>`
- `svc.call.ok.<from>.<to>`
- `svc.call.error.<from>.<to>`
- `svc.call.latency_ms.sum.<from>.<to>`
- `svc.call.latency_ms.count.<from>.<to>`

Average latency:

```text
latency_sum / max(1, latency_count)
```

## Minimal Example

See `service_communication.php` in this repository.

## Recommended Cross-Module Setup

1. Read call timeout and retry config from `kislayphp/config`.
2. Route sync calls via `kislayphp/gateway`.
3. Emit async workflow events with `kislayphp/queue` or `kislayphp/eventbus`.
