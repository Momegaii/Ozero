# IF-<id>: <interface name>

| Field | Value |
| --- | --- |
| Owner | <agent> |
| Version | <major>.<minor> |
| Status | Proposed / Implemented / Tested / Accepted |
| Consumers | <agents> |
| Since | Stage <n> |
| Related | ADR-xxxx |

## Purpose

What crosses the boundary, and why it exists.

## Producer obligations

What the owner guarantees: format, ordering, error codes, lifetime, threading.

## Consumer obligations

What a consumer must do: validate, retry, free, never assume.

## Data layout / wire format

Exact fields, sizes, endianness, alignment, limits. A diagram is optional; a table is not.

## Error behaviour

What happens on invalid input, timeout, partial write or unsupported version.

## Compatibility

Additive or breaking. For breaking changes, link the ADR and the migration note.

## Version history

| Version | Change | Compatibility | Date |
| --- | --- | --- | --- |
| 0.1 | Initial draft | — | YYYY-MM-DD |
