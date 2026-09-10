# Slow Stationboard Retry Design

## Purpose

Keep the display responsive when the stationboard server is slow or unavailable.
The device must keep cached transport rows on screen, update BTC before a
stationboard request, and limit repeated long blocking requests.

## Stationboard Retry State

The active stationboard view has one RAM-only retry state. It has three modes:
normal, long retry, and cooldown. The state resets to normal at restart.

In normal mode, the device starts a stationboard request on the normal
60-second refresh cycle. The request has a 15-second socket timeout and a
20-second total limit. BTC loads first. A failed stationboard request retains
cached rows and changes the transport status dot to red.

The next stationboard request starts 60 seconds after a failed normal request.
This long retry has a 60-second socket timeout and a 90-second total limit.
BTC still loads first. The device keeps cached departures visible while it
waits.

A successful stationboard response replaces the cache, changes the dot to
green, and resets the retry state to normal. A failed long retry starts a
five-minute cooldown. During cooldown, the device does not make a stationboard
request. It keeps cached rows, removes rows that have departed, and keeps the
dot red. After cooldown, it returns to normal mode.

WiFi recovery and a view change start a normal request. They do not force a
long retry.

## BTC Behavior

BTC is independent from stationboard retry state. The device requests BTC
before every stationboard request. A successful Coinbase response replaces the
cached price. A failed response keeps the last valid price on screen.

Before the first successful BTC response, the footer has its normal white
background but displays no BTC label or value. The device never writes `N/A`.
BTC failure does not change the transport status dot.

During stationboard cooldown, BTC still updates on the normal refresh cycle.
During a long stationboard request, BTC has already updated before the
stationboard request begins.

## Data And Error Handling

The stationboard request remains filtered and asks for the configured visible
limit plus five reserve rows, capped at 15. The complete API response is too
large for the ESP32 JSON memory budget. The display shows only the configured
visible limit.

HTTP errors, connection timeouts, read timeouts, oversized responses, JSON
errors, and invalid data follow the same retry state. Failed work cannot
replace a valid snapshot. The transport dot shows transport state only.

## Tests

Tests must verify normal failure to long retry, long retry failure to cooldown,
successful reset to normal, and cooldown behavior. They must verify that BTC
continues during cooldown, retains a valid price after failures, and stays
blank before its first success. They must verify the 15/20 and 60/90 request
limits and cached rows during all retry modes.
