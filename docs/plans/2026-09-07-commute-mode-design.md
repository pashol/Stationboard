# Commute Mode Design

## Goal

Add a destination-aware connections view that tells the user when to leave for
their next usable public-transport connection. Preserve the existing two
independent stationboard views and their layout.

## Configuration

Add a persisted `destination` field and a matching WiFiManager portal field.
Keep the existing configuration semantics:

- `stationId`: first independent stationboard and the commute-mode origin.
- `stationId2`: second independent stationboard.
- `destination`: commute-mode destination.
- `offset`: minutes required to reach the commute-mode origin station.

## API Request And Filtering

Request `/v1/connections` with `from=stationId`, `to=destination`, and the
local date and time calculated as now plus `offset`. This asks the API for
connections that begin when the user can reach the origin.

Retain each connection's `departureTimestamp` and delay in addition to its
display fields. Discard walking-only connections, identified by an empty
`products` array. The API can return such a connection overnight even when the
user expects public transport; it must not become the recommended commute.

The first remaining connection is the next usable public-transport option.

## Display

Keep the existing ten-row connection grid unchanged. Use the existing 25px
header for the route and a compact, right-aligned leave indicator:

```text
Cham, Gemeindehaus -> Zentrum   +7m
Cham, Gemeindehaus -> Zentrum   +1h 5m
Cham, Gemeindehaus -> Zentrum   GO!
```

The route label is ellipsized to the available pixel width after reserving the
indicator region. The status is derived from:

```text
effective departure = departureTimestamp + delay minutes
leave-in = effective departure - current time - offset
```

Display `GO!` at zero or fewer minutes. Otherwise display minutes below one
hour and hours plus minutes from one hour onward. Recalculate the indicator
locally at least once per minute, independently of API refreshes.

## Failure Handling

Do not show error text or symbols on the device. Cache the last successful
public-transport result in memory. If a request or parse fails, retain the
cached board only until its first connection's last known effective departure.
Continue to update the leave indicator from that cached timestamp and delay.
Clear the connections view after that time to avoid displaying a connection
that may already have departed.

Log all diagnostics to Serial, including HTTP status, request origin and
destination, parser/result count, cache retention, and cache expiry. A delay
reported after the last successful refresh cannot be known while requests are
failing; use the last known delay rather than inventing a value.

## Verification

Test the endpoint with `Cham, Gemeindehaus` to `Steinhausen, Zentrum` at an
overnight time such as `01:00`. Verify that normal public-transport results
are displayed and that an empty `products` result is skipped. Verify the
header across minute, hour, and departure boundaries; verify that cached rows
remain visible during a failed request until the last known effective
departure, then clear.
