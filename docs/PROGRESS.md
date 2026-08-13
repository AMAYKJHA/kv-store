# rkv — progress tracker

Phased status of the `rkv` build. Each phase maps to [`PLAN.md`](PLAN.md).
Use this file to record what is complete, what is in progress, and blockers.

Legend: `[-]` not started / `[~]` in progress / `[x]` complete

---

## Phase 0 — Baseline, first commit, test wiring

- [-] Initial git commit of scaffold + revised docs
- [-] Confirm `build/` and `bin/` are ignored
- [-] Add `tests/test_smoke.c`
- [-] Makefile targets: `make integration`, `make bench`

**Status:** [-]

---

## Phase 1 — `util/`: buffers, strings, clock, logging, alloc

- [x] `util/alloc.c` — checked malloc/realloc/free
- [x] `util/buf.c` — init, ensure, append, consume, free
- [x] `util/str.c` — binary-safe strings + int64 parse/format
- [x] `util/clock.c` — monotonic `now_ms()` + injectable test clock
- [-] `util/log.c` — leveled logging
- [x] `tests/test_str.c`
- [x] `tests/test_buf.c`

**Status:** [~]

---

## Phase 2 — `store/`: hash table + tagged values

- [-] `store/dict.c`
- [-] `store/store.c`
- [-] `tests/test_dict.c`

**Status:** [-]

---

## Phase 3 — `resp/`: RESP2 parser + writer

- [-] `resp/reader.c`
- [-] `resp/writer.c`
- [-] `tests/test_resp.c`

**Status:** [-]

---

## Phase 4 — `net/` + event loop: echo server

- [-] `net/socket.c`
- [-] `server/connection.c`
- [-] `server/server.c`
- [-] Echo server wired to `main.c`

**Status:** [-]

---

## Phase 5 — `server/command.c`: dispatch + core commands

- [-] Command table + dispatch
- [-] Handlers: `PING`, `ECHO`, `SET`, `GET`, `DEL`, `EXISTS`, `INCR`, `DECR`, `KEYS`, `DBSIZE`, `FLUSHDB`, `COMMAND`, `QUIT`
- [-] `tests/test_command.c`

**Status:** [-]

---

## Phase 6 — TTL / expiry

- [-] Lazy expiry in lookups
- [-] Bounded active-expiry cron
- [-] Commands: `EXPIRE`, `TTL`, `PERSIST`
- [-] `tests/test_ttl.c`

**Status:** [-]

---

## Phase 7 — `cli.c`: thin client

- [-] `src/cli.c`

**Status:** [-]

---

## Phase 8 — Integration tests over real TCP

- [-] Python harness in `tests/integration/`
- [-] `make integration` target

**Status:** [-]

---

## Phase 9 — Load / benchmark scripts

- [-] `tests/bench/` scripts
- [-] `make bench` target

**Status:** [-]

---

## Notes / blockers

- 2026-08-13: Project scaffold created; `DESIGN.md` and `PLAN.md` in place.