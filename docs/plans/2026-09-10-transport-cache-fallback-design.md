# Transport Cache Fallback Design

## Purpose

Keep the stationboard useful during WiFi and transport API outages without
covering the display with a stale-data message or presenting departures that
have already left.

The last successful response remains available in memory. Failed refreshes are
communicated only through the existing status dot in the bottom-right corner.
Cached rows are removed locally when their effective departure times pass, and
hidden reserve rows move into view.

## Goals

- Never render `STALE DATA` in stationboard or connections mode.
- Preserve useful departures through transient network and API failures.
- Remove departed records using their scheduled timestamp and reported delay.
- Keep the status indicator binary: green for a successful transport refresh,
  red for a failed refresh, disconnected WiFi, or cached fallback data.
- Reconnect automatically and refresh immediately after WiFi recovery.
- Keep all caches bounded and in RAM.

## Non-Goals

- Persisting transport snapshots across reboots.
- Showing cache age or adding more status colors.
- Changing BTC failure handling or making BTC availability affect transport
  status.
- Adding more reserve rows to connections mode.

## Architecture

Each station view owns an independent, fixed-capacity snapshot. The stationboard
cache grows from 10 to 15 rows. A request asks for `config.limit + 5` departures,
capped at 15, while rendering remains capped at `config.limit`. The five extra
records form a hidden reserve.

Each transport stores its existing display fields and the API's numeric
`stop.departureTimestamp`. Its effective departure is the scheduled timestamp
plus the reported delay in minutes, calculated with overflow-safe arithmetic.
Using the absolute API timestamp avoids ambiguity around midnight and daylight
saving transitions.

Parsing remains transactional. A response is read into a bounded temporary JSON
document and temporary snapshot. Only a complete, valid response replaces the
current snapshot. HTTP errors, timeouts, oversized responses, parse failures,
and allocation failures leave the previous snapshot untouched.

Connections keep their existing fixed capacity of eight. Their expiry behavior
changes from invalidating the complete snapshot when its first journey departs
to removing expired journeys individually.

## Data Flow

On a successful stationboard fetch:

1. Request up to `min(config.limit + 5, 15)` records, including each departure's
   numeric timestamp.
2. Parse and validate the complete response into a temporary snapshot.
3. Atomically publish the snapshot for the selected station view.
4. Draw at most the first `config.limit` rows.
5. Set the transport status dot to green.

On a failed fetch:

1. Retain the last successful snapshot.
2. Leave the station header and transport rows on screen.
3. Set the transport status dot to red.
4. Continue normal recovery attempts.

While cached fallback data is displayed, the loop compares each row's effective
departure with the current valid clock. Departed rows are removed, remaining
rows are compacted in order, and the board is redrawn only when visible content
changes. Reserve rows then move into the visible range naturally. Once every
cached row has departed, the station header remains and the row area is empty;
no stale-data message is drawn.

A valid API response containing no usable departures is still a successful
refresh. It publishes an empty snapshot and sets the dot green.

## View Switching And Startup

Switching modes immediately renders the requested view's cache when one exists.
The dot is red until the forced refresh for that view succeeds. This prevents
the previous view's rows or status from appearing under the newly selected
mode.

If a view has never fetched successfully, it displays its configured station
header, current time, an empty row area, and a red dot. A boot without WiFi uses
the same presentation. Snapshots are not written to SPIFFS and therefore do not
survive a restart.

## Status And Recovery

The bottom-right status dot remains the only fallback warning:

- Green: the latest transport refresh for the current view succeeded.
- Red: WiFi is disconnected, the latest transport refresh failed, the view has
  no successful snapshot, or cached fallback rows are being shown.

BTC refresh results do not affect this dot.

WiFi reconnection remains exponential but is capped at 60 seconds rather than
five minutes: 1, 2, 4, 8, 16, 32, then 60 seconds. A transition from disconnected
to connected forces an immediate refresh. If WiFi is connected but the
transport API fails, the transport API is retried on the normal 60-second
refresh interval to avoid excessive requests.

## Clock And Error Handling

Rows are pruned only while the clock is valid. If time has not synchronized or
becomes unusable, the cache is retained rather than risk deleting future
departures. Pruning resumes when a valid clock is available.

Missing or invalid required timestamp data makes an individual row unusable;
that row is skipped under the existing malformed-row policy. Delay arithmetic
supports positive and negative values without integer overflow. All loops stay
bounded by the fixed snapshot capacities.

Rendering failures must not destroy the cache or clear an already valid frame.
Pruning updates the in-memory snapshot first and redraws only the affected view.
The footer and status-dot area remain independent from transport row rendering.

## Testing

Unit tests will verify:

- The request limit is `config.limit + 5`, capped at 15.
- Parsing retains the departure timestamp and delay without exceeding capacity.
- Rendering is capped at `config.limit` despite reserve rows in the snapshot.
- Failed parsing and every fetch failure category retain the previous snapshot.
- Effective departure checks work immediately before, exactly at, and after the
  deadline.
- Delay handling, midnight crossings, overflow boundaries, compaction, and
  multiple simultaneous expirations behave correctly.
- Invalid clocks retain all rows.
- Reserve rows move into view in their original order.
- Connections are pruned row by row.
- Status remains green only after transport success and red for all fallback
  conditions, independently of BTC.
- WiFi backoff reaches but does not exceed 60 seconds, then forces a refresh on
  recovery.

Device verification will build the firmware and run an outage scenario. After
a successful board load, WiFi will be disconnected long enough for visible rows
to depart. The display must retain future cached rows, promote reserve rows,
show a red dot, and never render `STALE DATA`. Restoring WiFi must trigger an
immediate successful refresh, replace the cache, and return the dot to green.
A separate offline boot check must show an empty board and red dot.

## Acceptance Criteria

- No runtime path in stationboard or connections mode draws `STALE DATA`.
- Failed refreshes never overwrite a valid transport snapshot.
- Departed rows do not remain visible once a valid clock reaches their effective
  departure time.
- Up to five reserve stationboard departures extend useful fallback operation.
- Recovery is automatic, bounded, and visibly returns the dot from red to green.
- The firmware build and relevant stability tests pass.
