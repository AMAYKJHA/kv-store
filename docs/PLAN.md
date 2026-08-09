# rkv — build plan (revised)

A phased roadmap for building `rkv`, a single-threaded, in-memory,
`epoll`-driven key-value store speaking a subset of RESP2. This revision
folds in the systems requirements: explicit connection lifecycle, a
robust three-state RESP parser, backpressure, monotonic-clock TTLs,
`signalfd` shutdown, integration + load testing, and a directory layout
that avoids a God `server.c`. See [`DESIGN.md`](DESIGN.md) for the
architecture and the list of **frozen decisions** — settle those first.

Modules are built bottom-up (leaves before their users) so each phase is
independently testable and the tree always builds. Every phase ends with:
`make` clean (no warnings), unit tests green under **ASan/UBSan**, and a
commit.

## Dependency order

```
util (buf,str,alloc,log,clock) ─┬─► store (dict, ttl) ─┐
                                └─► resp (reader,writer)─┼─► server (loop,
                                    net (socket) ────────┘   connection, command)
                                                              │
                                                              └─► cli, integration, bench
```

---

## Phase 0 — Baseline, first commit, test wiring

Lock in the starting point and prove the test harness end-to-end before
writing real code.

**Tasks**
- Initial git commit of the scaffold + revised docs (`git add -A`).
- Confirm `build/` and `bin/` are ignored, not tracked.
- Add `tests/test_smoke.c` (trivial assert) so `make test` runs something.
- Add Makefile targets that later phases need (can start as stubs):
  `make integration` (runs `tests/integration`), `make bench` (runs
  `tests/bench`). Unit `make test` stays as-is (compile each `test_*.c`,
  link lib objects).

**Acceptance** — `git log` shows a commit; `make && make test` both green.

---

## Phase 1 — `util/`: buffers, strings, clock, logging, alloc

Foundations, all pure and trivially unit-testable.

**Deliverables**
- `util/alloc.c` — checked `rkv_malloc/realloc/free`, abort on OOM.
- `util/buf.c` — `rkv_buf` byte buffer: init, ensure-capacity (with a max
  cap argument), append, consume-from-front (compaction), free.
- `util/str.c` — `rkv_str` length-prefixed, **binary-safe** string:
  create-from-bytes, dup, free, compare, `printf`-format, int64↔string
  (strict parse for `INCR`, and formatting).
- `util/clock.c` — `now_ms()` on `CLOCK_MONOTONIC`, plus an **injectable**
  clock hook for tests (`rkv_clock_set_test_now`).
- `util/log.c` — leveled logging to stderr with runtime threshold.
- `tests/test_str.c`, `tests/test_buf.c`.

**Acceptance** — append growth, embedded-NUL handling, buffer compaction,
strict int64 parse (reject `" 1"`, `"1a"`, `"+1"`, `"01"`, overflow), and
the injectable clock all covered; clean under ASan.

---

## Phase 2 — `store/`: hash table + tagged values (no networking, TTL-ready)

The in-memory core, testable without a socket.

**Deliverables**
- `store/dict.c` — separate-chaining hash table: FNV-1a + per-process
  seed, `get`/`set`/`del`/`exists`/`iterate`/`size`/`flush`, grow at load
  factor ≥ 1.0 (full rehash), O(1) `nused` counter for `DBSIZE`.
- `store/store.c` — store API over the dict: tagged `rkv_val` (string
  now), **ownership = store copies keys/values it retains** (argv slices
  are borrowed). `expire_at` field present in the entry now, unused yet.
- `tests/test_dict.c` — insert/lookup/delete at scale, forced collisions,
  growth/rehash, no leaks across create→churn→free.

**Acceptance** — dict tests pass, DBSIZE counter correct across
set/del/overwrite, clean under ASan.

---

## Phase 3 — `resp/`: RESP2 parser + writer (byte fixtures)

Protocol translation with the full three-state contract, tested over raw
byte strings — still no sockets.

**Deliverables**
- `resp/reader.c` — restartable parser returning
  `RESP_COMPLETE | RESP_NEED_MORE | RESP_PROTOCOL_ERROR`, producing
  borrowed `argv` slices + `consumed` byte count. Enforces `MAX_ARGS` and
  `MAX_BULK_SIZE`; **never allocates on client-declared lengths**.
- `resp/writer.c` — encode simple string, error, integer, bulk string,
  nil bulk (`$-1`), and array.
- `tests/test_resp.c` — cover, at minimum:
  - fragmented command across two chunks → `NEED_MORE`, nothing consumed
  - two commands in one buffer → two `COMPLETE`, exact `consumed` each
  - complete followed by partial → one dispatched, remainder retained
  - malformed frames → `PROTOCOL_ERROR`
  - binary-safe bulk (embedded NUL and `\r\n` in payload)
  - oversized arg count / bulk length → `PROTOCOL_ERROR`, no allocation

**Acceptance** — all of the above pass; parser never reads past a frame
boundary; clean under ASan.

---

## Phase 4 — `net/` + event loop: echo server first

Prove the socket + `epoll` machinery as a plain echo server before any
command logic touches it. This is where partial read/write and the
`EPOLLOUT` lifecycle are validated.

**Deliverables**
- `net/socket.c` — non-blocking listener (`SO_REUSEADDR`, bind, listen),
  accept-until-`EAGAIN`, `O_NONBLOCK` on accepted fds, `epoll_add/mod/del`
  wrappers.
- `server/connection.c` — `conn` lifecycle, `on_readable` (drain to
  `EAGAIN`, append to `in`), `on_writable` (drain `out`, disarm
  `EPOLLOUT` when empty, honour `close_after_write`), `close_conn`
  (unlink, close fd, free buffers).
- `server/server.c` — `server_t`, the **level-triggered** epoll loop with
  a cron timeout, plus `SIGPIPE` ignore. Wire `main.c` to run it as an
  **echo** server (echo `in` back into `out`).
- Enforce `MAX_INPUT_BUF` / `MAX_OUTPUT_BUF` here.

**Acceptance** — `redis-cli --pipe` / `nc` echoes back; a request split
across TCP segments still echoes whole; many concurrent clients work; a
client disconnecting mid-stream neither crashes nor leaks (ASan);
`EPOLLOUT` is not left armed on an empty buffer (no CPU spin at idle).

---

## Phase 5 — `server/command.c`: dispatch + core commands

Bring it together: parse → dispatch → store → serialize. First real
Redis-like behaviour.

**Deliverables**
- `server/command.c` — command table (name → handler + arity), dispatch
  loop that consumes every complete frame in `in`, arity/unknown-command
  errors.
- Handlers: `PING`, `ECHO`, `SET`, `GET`, `DEL`, `EXISTS`, `INCR`,
  `DECR`, `KEYS`, `DBSIZE`, `FLUSHDB`, `COMMAND` (minimal), `QUIT`.
  Semantics and reply types exactly per `DESIGN.md` (esp. the full
  `INCR`/`DECR` spec: missing key, strict format, overflow).
- Replace the echo path with real dispatch.
- `tests/test_command.c` — drive commands through the table against an
  in-memory store (no socket), including `INCR` overflow / bad value.

**Acceptance** — real `redis-cli` can `SET`/`GET`/`DEL`/`INCR`/`KEYS`/
`PING`; arity and unknown-command errors correct; `INCR` on a
non-integer and at `INT64_MAX` return the specified errors.

---

## Phase 6 — TTL / expiry

Add TTLs on the monotonic clock: lazy on access + bounded active sweep.

**Deliverables**
- Wire `expire_at` into `store` lookups (lazy expiry) so `GET`/`EXISTS`/
  `DEL`/`INCR`/`DECR`/`KEYS`/`TTL` treat expired keys as absent.
- Bounded active-expiry cron in the loop (~10 Hz, sample ≤ N, capped
  rounds) — **must not** scan the whole DB or block the loop.
- Commands `EXPIRE`, `TTL`, `PERSIST` with the exact return values from
  `DESIGN.md` (`TTL` → `-2` missing, `-1` no-TTL; `EXPIRE seconds<=0`
  deletes).
- `tests/test_ttl.c` — using the **injectable clock** (no `sleep`): lazy
  deletion, `TTL` countdown, `-1`/`-2` cases, `PERSIST`, and that the
  active sweep evicts without touching untouched non-expiring keys.

**Acceptance** — TTL unit tests pass with the fake clock; no busy-scan.

---

## Phase 7 — `cli.c`: thin client

A small manual-testing/demo client (redis-cli already covers conformance).

**Deliverables**
- `src/cli.c` — connect, read a line, send as a RESP array, print the
  decoded reply; reuses `resp/` reader + writer.

**Acceptance** — `rkv-cli` runs a `SET`/`GET`/`EXPIRE`/`TTL` session
against `rkv-server`.

---

## Phase 8 — Integration tests over real TCP

Unit tests alone can't prove the socket/framing behaviour. A Python
harness (`tests/integration/`, run via `make integration`) spawns the real
`rkv-server` on an ephemeral port and drives it over real sockets.

**Must cover**
- multiple concurrent clients
- a request fragmented across several `send()`s (with small sleeps)
- pipelined requests (many commands in one `send()`, all replies returned)
- partial writes / large replies drained correctly (e.g. big `KEYS`)
- client disconnect mid-request (server survives, no leak)
- malformed RESP → error reply + connection closed
- a large-but-legal request (near `MAX_BULK_SIZE`)
- output-buffer limit: a client that stops reading while flooding →
  server closes it, stays up for others
- TTL behaviour through the real server (`EXPIRE` then observe expiry)

**Acceptance** — `make integration` passes; running the suite under an
ASan build reports no leaks/errors.

---

## Phase 9 — Load / performance testing

A simple benchmark (`tests/bench/`, `make bench`) — Python or C, need not
be sophisticated.

**Measure**
- many concurrent clients doing repeated `SET`/`GET`
- pipelined vs non-pipelined throughput
- connection churn (rapid connect/command/disconnect)
- report **requests/sec**, **latency** (avg + p99), and rough **memory**
  behaviour (RSS before/after, and stable under churn)
- optional: compare against `redis-benchmark` pointed at `rkv`.

**Acceptance** — `make bench` produces a short report; numbers recorded in
`docs/` or the README; RSS is stable across connection churn (no fd/mem
leak).

---

## Phase 10 — Hardening & polish (optional, time permitting)

Stretch goals that improve robustness without expanding scope.

- Incremental (non-blocking) rehash to remove the resize stall.
- Config via flags/file: bind addr, port, log level, limits.
- Graceful shutdown already lands in Phase 4/5; verify LSan-clean teardown.
- `INFO`-style stats (uptime, clients, keys, memory).
- `SET` options (`EX`/`PX`/`NX`) if useful for the demo.
- `make valgrind` target as a second sanitizer pass.

---

## What changed from the original plan, and why

- **Signals/shutdown moved from "optional polish" into the core** (folded
  into Phase 4/5) — a `signalfd`-driven clean teardown is central to the
  systems story and to LSan-clean tests.
- **Integration (Phase 8) and load testing (Phase 9) are now first-class
  phases** — unit tests can't exercise partial reads/writes, pipelining,
  disconnects, or backpressure over a real socket.
- **TTL pinned to a monotonic, injectable clock** (was just "injectable")
  — wall-clock durations are a latent bug.
- **Server split into `server.c` / `connection.c` / `command.c`** and a
  single `net/socket.c`, replacing the original single `server.c` God file
  and the confusing duplicate `server.c` names.
- **Backpressure, protocol limits, and the three-state parser contract
  are explicit deliverables** with their own acceptance criteria, rather
  than being implied.
- **`INCR`/`DECR`, `TTL`, and `EXPIRE` edge cases are frozen** so they're
  implemented once, correctly, instead of discovered during testing.
