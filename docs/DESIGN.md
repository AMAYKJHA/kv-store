# rkv — design notes

## Scope

A single-process, **single-threaded**, in-memory key-value server speaking
a subset of **RESP2**, so that the real `redis-cli` can talk to it. The
point of the project is to demonstrate network and systems programming:
non-blocking TCP, an `epoll` event loop, incremental protocol framing,
backpressure, hash tables, timers, and signal-driven shutdown — not to
reproduce Redis.

Target command set for v1:

- strings: `SET`, `GET`, `DEL`, `EXISTS`, `INCR`, `DECR`
- expiry: `EXPIRE`, `TTL`, `PERSIST`
- keyspace: `KEYS`, `DBSIZE`, `FLUSHDB`
- misc: `PING`, `ECHO`, `COMMAND`, `QUIT`

### Platform

**Linux only.** The event loop is built on `epoll` and `signalfd`, both
Linux-specific syscalls, so there is no portability layer. **WSL2 on
Windows is supported** — it runs a real Linux kernel, so `epoll`,
`signalfd`, sanitizers, and the test harness all behave as on native
Linux; WSL1 is not recommended, and the repo should live on the Linux
filesystem rather than a `/mnt/c` mount.

### Non-goals (explicitly out of scope)

Clustering, replication, pub/sub, transactions/`MULTI`, Lua, streams,
non-string data types, RESP3, `AUTH`, multiple databases (`SELECT`),
multithreading, and any on-disk persistence (AOF/RDB). These are not
deferred features — they are deliberately excluded to keep the project
focused and finishable.

## Architecture

```
                    ┌─────────────────── server_t (single context) ───────────────────┐
   TCP    accept()  │  epoll loop ──► connection I/O ──► resp reader ──► command        │
 client ─────────►  │      ▲                                              dispatch      │
   fd              │      │                                                 │           │
                    │  epoll_ctl(EPOLLOUT) ◄── resp writer ◄── store (dict + TTL)      │
                    │      ▲                                                             │
                    │  signalfd / cron timer (bounded expiry sweep)                     │
                    └───────────────────────────────────────────────────────────────────┘
```

One thread, one `epoll` instance. Every fd — the listener, each client,
and a `signalfd` — is a source of events. The loop never blocks on any
single client: all sockets are non-blocking, reads drain until `EAGAIN`,
writes are buffered and retried on `EPOLLOUT`. A periodic cron tick
(driven by the `epoll_wait` timeout) runs a bounded expiry sweep.

## Directory structure

```
src/
  main.c              entry: parse args, build server_t, run, teardown
  server/
    server.c          server_t lifecycle: init, epoll loop, cron, shutdown
    connection.c      conn lifecycle + buffers + readable/writable handlers
    command.c         command table + dispatch + all command handlers
  net/
    socket.c          listener socket, accept, non-blocking, TCP opts,
                      thin epoll_add/mod/del wrappers
  resp/
    reader.c          bytes -> argv (restartable parser, limits enforced)
    writer.c          replies -> bytes (RESP2 encoder)
  store/
    dict.c            hash table: chaining, hashing, resize
    store.c           store API over dict: value types, TTL, lazy+active expiry
  util/
    buf.c             byte buffer (connection in/out)
    str.c             rkv_str: length-prefixed, binary-safe string
    alloc.c           checked malloc/realloc (abort on OOM)
    log.c             leveled logging to stderr
    clock.c           monotonic now_ms(), injectable for tests
include/rkv/          one public header per module
tests/
  test_*.c            unit tests (compiled + linked against lib objects)
  integration/        Python harness driving the real TCP server
  bench/              load/benchmark scripts
docs/DESIGN.md        this file
docs/PLAN.md          phased build plan
```

Rationale for the split: the old plan funnelled everything into one
`server.c`, which becomes a God file. Responsibilities are separated so
each unit is independently testable, while staying small enough for a
student project (no premature per-command files — all handlers live in
`command.c` until it actually gets unwieldy). There is exactly one file
named `server.c`; socket primitives live in `net/socket.c`.

There is **no file-scope mutable global state**. All server state lives in
a single `server_t` passed by pointer; this keeps handlers testable and
teardown explicit.

## Core data structures

### Byte buffer (`util/buf.c`)

```c
typedef struct {
    char  *data;
    size_t len;   /* bytes used            */
    size_t cap;   /* bytes allocated       */
} rkv_buf;
```

Backs both the per-connection input and output buffers. Growth is capped
(see Limits).

### Binary-safe string (`util/str.c`)

```c
typedef struct {
    size_t len;
    size_t cap;
    char  *buf;   /* not NUL-terminated-dependent; length is authoritative */
} rkv_str;
```

Decision (was open in the old design): values and keys are length-prefixed
`rkv_str`, **not** `char*`, because RESP bulk strings are binary-safe and
may contain embedded NUL.

### Connection (`server/connection.c`)

```c
typedef enum { CONN_OPEN, CONN_CLOSING } conn_state;

typedef struct conn {
    int         fd;
    conn_state  state;

    rkv_buf     in;            /* raw bytes read from socket        */
    rkv_buf     out;           /* pending reply bytes               */
    size_t      out_sent;      /* bytes of out already written      */

    bool        want_write;    /* EPOLLOUT currently armed          */
    bool        close_after_write; /* QUIT / fatal: drain then close */

    struct conn *next;         /* intrusive list for shutdown sweep */
} conn;
```

This is the explicit per-client state the project centres on: fd, input
buffer, output buffer, read progress (implicit in `in`), write progress
(`out_sent`), and closing state.

### Value + store entry (`store/`)

```c
typedef enum { VAL_STRING /* room for VAL_LIST, VAL_HASH, ... */ } val_type;

typedef struct {
    val_type type;
    union { rkv_str *str; } as;   /* owned by the entry */
} rkv_val;

typedef struct dictEntry {
    rkv_str          *key;        /* owned */
    rkv_val           val;        /* owned */
    long long         expire_at;  /* monotonic ms; 0 = no expiry */
    struct dictEntry *next;       /* separate chaining */
} dictEntry;

typedef struct {
    dictEntry **buckets;
    size_t      nbuckets;         /* power of two */
    size_t      nused;            /* live entries; backs O(1) DBSIZE */
    uint64_t    seed;             /* per-process hash seed */
} dict;
```

`expire_at` is inline in the entry **from day one** even though Phase 2
ignores it — so adding TTL later is not a rewrite. `type` is a tagged
union so string values work now with room for future types, without
touching the entry layout.

## Event-loop flow

Trigger mode is **level-triggered** (frozen decision): simpler and more
forgiving than edge-triggered, while we still drain to `EAGAIN` for
efficiency. The one obligation LT imposes — never leave `EPOLLOUT` armed
with an empty output buffer, or the loop spins — is handled explicitly.

```
startup:
  listen_fd = create listener (SO_REUSEADDR, bind, listen), set O_NONBLOCK
  epfd = epoll_create1()
  epoll_add(listen_fd, EPOLLIN)
  sfd = signalfd(SIGINT, SIGTERM); epoll_add(sfd, EPOLLIN)
  signal(SIGPIPE, SIG_IGN)

loop while (!shutdown):
  timeout = ms_until_next_cron()          /* e.g. 100ms; bounds idle wait */
  n = epoll_wait(epfd, ev, MAXEV, timeout)
  for each ready fd:
    if fd == listen_fd:  accept_all()     /* accept() until EAGAIN */
    else if fd == sfd:   shutdown = true
    else:
      c = ev.data.ptr
      if EPOLLERR|EPOLLHUP:      close_conn(c); continue
      if EPOLLIN:                on_readable(c)
      if EPOLLOUT:               on_writable(c)
      if c->close_after_write && out_drained(c): close_conn(c)
  run_cron_if_due()                       /* bounded expiry sweep */

shutdown:
  for each conn: close_conn(c)
  store_free(); close(listen_fd); close(sfd); close(epfd)
  exit(0)
```

### `on_readable` — partial reads

One `read()` is **not** one request. We loop:

```
loop:
  ensure in has spare capacity (grow, but refuse past MAX_INPUT_BUF)
  n = read(fd, in.data + in.len, spare)
  if n > 0:  in.len += n; continue
  if n == 0: mark EOF -> close_conn after this pass; break
  if n < 0:  EAGAIN -> break;  EINTR -> continue;  else -> close; break
parse_and_dispatch(c)          /* consume every COMPLETE frame in `in` */
if c->out has bytes && !c->want_write: arm EPOLLOUT
```

Multiple complete commands buffered by one `read()` are all dispatched in
the parse loop; a trailing partial command is left in `in` for the next
`read()`. After parsing, consumed bytes are removed by compacting the
leftover to the front of `in` (leftover is at most one partial frame, so
this is cheap).

### `on_writable` — partial writes

One `write()` is **not** one response. We loop:

```
loop while out_sent < out.len:
  n = write(fd, out.data + out_sent, out.len - out_sent)   /* MSG_NOSIGNAL */
  if n > 0:  out_sent += n; continue
  if n < 0:  EAGAIN -> break (keep EPOLLOUT armed);
             EINTR -> continue;  else -> close; return
if out_sent == out.len:        /* fully drained */
  out.len = 0; out_sent = 0
  disarm EPOLLOUT              /* mandatory under level-triggered */
  if c->close_after_write: close_conn(c)
```

Unsent bytes stay in `out` with `EPOLLOUT` armed; the kernel wakes us when
the socket is writable again. `QUIT` and fatal errors set
`close_after_write` so the reply is flushed before the fd closes.

## RESP2 parser

The parser is **restartable, not resumable**: on each call it attempts to
parse one complete command from the current offset of the input buffer. If
the buffer doesn't yet hold a full frame it reports `NEED_MORE_DATA` and
consumes nothing. This is simpler and less bug-prone than a byte-by-byte
resumable state machine, and the wasted re-parse is bounded by
`MAX_INPUT_BUF`.

```c
typedef enum { RESP_COMPLETE, RESP_NEED_MORE, RESP_PROTOCOL_ERROR } resp_status;

/* On RESP_COMPLETE: argv/argc point INTO the input buffer (borrowed
   slices), and *consumed is the frame length in bytes. */
resp_status resp_parse(const char *buf, size_t len,
                       rkv_slice **argv, int *argc, size_t *consumed);
```

Handled cases:

- **fragmented** command across reads → `NEED_MORE`, bytes retained.
- **multiple** commands in one read → dispatch loop calls `resp_parse`
  repeatedly, advancing by `consumed`, until `NEED_MORE`.
- **complete + partial** → complete one dispatched, partial left buffered.
- **malformed** (bad type byte, non-numeric length, missing `\r\n`,
  length mismatch) → `RESP_PROTOCOL_ERROR` → send `-ERR Protocol error`
  and set `close_after_write`.
- **binary-safe** bulk strings: length-driven, never `strlen`; embedded
  NUL and `\r\n` inside payloads are fine.
- **exact frame boundaries**: `consumed` is precise; leftover bytes are
  never lost or double-consumed.

### argv lifetime invariant (frozen)

`argv` entries are **borrowed slices into the input buffer**, valid only
during dispatch of that one command. The store copies any key/value it
retains. The input buffer is not compacted until after dispatch returns.
Violating this is a use-after-free waiting to happen.

### Untrusted-length safety (frozen)

The parser **never allocates based on a client-declared length**. Because
argv are slices, a bulk string simply waits until that many bytes are
actually present. Before waiting, the declared length is validated:

- array element count `> MAX_ARGS` → protocol error (no allocation).
- bulk length `< -1`, or `> MAX_BULK_SIZE` → protocol error.
- if `in.len` would exceed `MAX_INPUT_BUF` while still `NEED_MORE` → the
  client is sending an oversized/garbage frame → close.

## Limits (backpressure & DoS resistance)

| Limit | Default | Enforced where | On breach |
| --- | --- | --- | --- |
| `MAX_ARGS` | 1024 | parser (array header) | protocol error, close |
| `MAX_BULK_SIZE` | 32 MiB | parser (bulk header) | protocol error, close |
| `MAX_INPUT_BUF` | 34 MiB | `on_readable` growth | close |
| `MAX_OUTPUT_BUF` | 64 MiB | after queueing a reply | close |

**Output backpressure:** a slow or malicious client that stops reading
while pipelining requests would otherwise make `out` grow without bound.
After appending any reply, if `out.len - out_sent > MAX_OUTPUT_BUF`, the
connection is logged and closed. Disconnecting is the accepted remedy for
this project. (The main legitimate large-output command, `KEYS *` on a
huge DB, is also bounded by this.)

## Command semantics

Every command's arity is checked before dispatch; wrong arity →
`-ERR wrong number of arguments for 'cmd'`. Unknown command →
`-ERR unknown command 'x'`.

| Command | Args | Semantics | RESP reply |
| --- | --- | --- | --- |
| `PING` | 0 or 1 | no arg → `PONG`; one arg → echo it | simple string / bulk |
| `ECHO` | 1 | return the argument verbatim | bulk string |
| `SET` | 2 | set key to string value (overwrites, clears any TTL) | `+OK` |
| `GET` | 1 | value if present & not expired, else nil | bulk / nil (`$-1`) |
| `DEL` | ≥1 | delete each given key that exists | integer (count deleted) |
| `EXISTS` | ≥1 | count of given keys that exist (dups count) | integer |
| `INCR` | 1 | see below | integer |
| `DECR` | 1 | see below | integer |
| `KEYS` | 1 | keys matching glob pattern (`*`, `?`, `[...]`); skips expired | array of bulk |
| `DBSIZE` | 0 | number of live keys (O(1) counter) | integer |
| `FLUSHDB` | 0 | delete all keys | `+OK` |
| `COMMAND` | 0+ | minimal: `COMMAND` / `COMMAND DOCS` → empty array; `COMMAND COUNT` → integer | array / integer |
| `QUIT` | 0 | reply `+OK`, then close after flush | `+OK` |

Note: `SET` in v1 takes no options (`EX`/`PX`/`NX` are an optional later
extension). `redis-cli` may send `HELLO` (RESP3 handshake) on connect;
it's an unknown command here and returns an error, which `redis-cli`
tolerates by falling back to RESP2.

### `INCR` / `DECR` (fully specified)

- **missing key** → treated as `0`, then incremented/decremented →
  result `1` / `-1`; the key is created.
- **valid integer format** → base-10, optional single leading `-`,
  otherwise ASCII digits only; no whitespace, no `+`, no leading zeros
  (except `"0"`), must fit in signed 64-bit. Parsed strictly.
- **negative integers** → supported (as operand and result).
- **invalid value** (key holds a non-integer string) →
  `-ERR value is not an integer or out of range`, key unchanged.
- **overflow** → if the result would exceed `INT64_MAX` or fall below
  `INT64_MIN`, `-ERR increment or decrement would overflow`, key
  unchanged.
- result is stored back as its decimal string (type stays `VAL_STRING`).

## TTL / expiry

**Time base (frozen): `CLOCK_MONOTONIC`, milliseconds.** Wall-clock time
must not be used for durations — an NTP step or manual clock change would
corrupt TTLs. All timing goes through `util/clock.c: now_ms()`, which is
**injectable**: tests install a fake clock; production reads
`CLOCK_MONOTONIC`. `expire_at` in an entry is an absolute monotonic
timestamp; `0` means no expiry.

**Lazy expiry:** the store's internal `lookup()` checks `expire_at` on
every access; a key found expired is deleted and reported as absent. So
`GET`, `EXISTS`, `DEL`, `INCR`, `DECR`, `TTL`, `KEYS` all treat expired
keys as gone.

**Active expiry (bounded):** a cron tick runs at ~10 Hz (every 100 ms),
driven by the `epoll_wait` timeout so it never blocks the loop and never
scans the whole DB. Each tick samples up to `N` (e.g. 20) random entries;
expired ones are deleted; if more than 25% of the sample was expired it
resamples, up to a small fixed cap of rounds. Total work per tick is
bounded → the loop is never stalled. (An `expires` secondary index is a
possible optimization but is **out of v1 scope**; random sampling of the
main table is simpler and sufficient.)

Commands:

- `EXPIRE key seconds` → set `expire_at = now_ms() + seconds*1000`.
  Returns `1` if the key exists, `0` if not. `seconds <= 0` deletes the
  key immediately and returns `1`. Reply: integer.
- `TTL key` → remaining seconds. Reply: integer.
  - key missing (or already expired) → `-2`
  - key exists, no TTL → `-1`
  - key exists with TTL → remaining whole seconds (≥ 0)
- `PERSIST key` → remove the TTL. Returns `1` if a TTL was removed, `0` if
  the key is missing or had no TTL. Reply: integer.

## Signals & shutdown

Signal handling is **not optional polish** — it's part of the systems
story. Approach (frozen): `SIGINT` and `SIGTERM` are delivered via a
`signalfd` registered in the epoll set, so the handler is just an ordinary
readable fd — no async-signal-safety hazards, no self-pipe. `SIGPIPE` is
ignored (`SIG_IGN`) and every socket write additionally uses
`MSG_NOSIGNAL`, so a write to a peer-closed socket returns `EPIPE` instead
of killing the process.

```
SIGINT/SIGTERM ─► signalfd readable ─► set shutdown flag
   ─► epoll loop exits
   ─► close all active connections (flush best-effort, then close fd)
   ─► free store (all keys/values), close listener + signalfd + epfd
   ─► exit(0)
```

This clean teardown also means ASan/LSan report **zero leaks** on normal
shutdown, which is part of the acceptance criteria.

## Complexity

| Command | Complexity | Blocks the loop? |
| --- | --- | --- |
| `GET` | O(1) avg | no |
| `SET` | O(1) amortized | only during a resize (see note) |
| `DEL` | O(1) avg per key | no |
| `EXISTS` | O(1) avg per key | no |
| `INCR` / `DECR` | O(1) avg | no |
| `EXPIRE` | O(1) avg | no |
| `TTL` / `PERSIST` | O(1) avg | no |
| `DBSIZE` | O(1) | no |
| `PING` / `ECHO` / `QUIT` / `COMMAND` | O(1) | no |
| `KEYS` | O(N) | **yes** — scans every key |
| `FLUSHDB` | O(N) | **yes** — frees every entry |

Notes on blocking work in a single-threaded server:

- **Resize/rehash** is a full O(N) rehash in v1, so the `SET` that
  triggers it stalls the loop proportional to table size. Acceptable at
  student scale; incremental rehash is a documented optional hardening
  step if it becomes a problem.
- **`KEYS` and `FLUSHDB`** are inherently O(N) and will pause the loop —
  this is understood and matches Redis's own warning about `KEYS` in
  production.
- **Active expiry** is deliberately bounded per tick so it is *not* O(N)
  and does not block.

## Hash table specifics

- **Function:** FNV-1a over the key bytes, Xored with a per-process random
  `seed` (from `getrandom`/time at startup). Seeding mitigates
  algorithmic-complexity (collision-flooding) attacks from untrusted
  input while staying far simpler than SipHash.
- **Collisions:** separate chaining; each bucket is a singly linked list
  of `dictEntry`.
- **Load factor:** `nused / nbuckets`. Grow (double `nbuckets`) when it
  reaches `1.0`; optionally shrink when it drops below `0.1` and the table
  is above its initial size.
- **Rehash:** allocate the new bucket array and re-link every entry
  (pointers are moved, keys/values are not reallocated). v1 does this in
  one pass (blocking, O(N)); incremental rehash is optional later.
- **Ownership:** the dict owns its `key` and `val` (and the `rkv_str`
  inside). `store_set` **copies** the caller's key/value bytes (which are
  borrowed argv slices) into owned `rkv_str`s. Freeing an entry frees its
  key and value. There is exactly one owner per allocation — no shared
  ownership, no aliasing of argv slices into the store.

## Security & robustness checklist

| Concern | Mitigation |
| --- | --- |
| Untrusted network input | strict RESP parse, three-state result, close on protocol error |
| Integer overflow | checked `INCR`/`DECR`; validated lengths before use; `size_t` math guarded |
| Allocation failure | `util/alloc.c` aborts on OOM (documented, deliberate for v1) |
| Malformed RESP | `RESP_PROTOCOL_ERROR` → error reply + close |
| Oversized requests | `MAX_ARGS`, `MAX_BULK_SIZE`, `MAX_INPUT_BUF` |
| Oversized output | `MAX_OUTPUT_BUF` → close client |
| Collision-flooding DoS | seeded hash |
| Disconnected clients | `read()==0` / `EPIPE` / `EPOLLHUP` all → `close_conn` |
| `SIGPIPE` | `SIG_IGN` + `MSG_NOSIGNAL` |
| FD leaks | every close path frees the conn and its buffers; shutdown sweep closes all |
| Use-after-free | argv-slice lifetime invariant; store copies retained data |
| Double-free | single-owner rule; `close_conn` idempotent / unlinks before free |
| Buffer overflow | length-driven buffers, capacity checks, `-Wconversion` on |
| — | ASan + UBSan on in every debug build & test run |

## Frozen decisions (settle before writing code)

1. **epoll: level-triggered**, drain to `EAGAIN`, `EPOLLOUT` armed only
   while output is pending.
2. **RESP parser: restartable** (re-parse from frame start); `argv` are
   borrowed slices valid only during dispatch; store copies retained data.
3. **Strings: length-prefixed `rkv_str`** (binary-safe) everywhere, not
   `char*`.
4. **Hash: FNV-1a + per-process seed, separate chaining**, grow at load
   factor ≥ 1.0 (double), full rehash in v1.
5. **TTL: `CLOCK_MONOTONIC` ms**, `expire_at` inline in entry, lazy +
   bounded random-sample cron at ~10 Hz; clock is injectable.
6. **Signals: `signalfd`** in epoll for SIGINT/SIGTERM; `SIGPIPE`
   ignored + `MSG_NOSIGNAL`.
7. **Limits:** `MAX_ARGS=1024`, `MAX_BULK_SIZE=32MiB`,
   `MAX_INPUT_BUF=34MiB`, `MAX_OUTPUT_BUF=64MiB` (compile-time consts).
8. **State: one `server_t` context**, no file-scope mutable globals.
9. **Single database, no auth, no `SELECT`, no RESP3.**
10. **`SET` is basic** (no `EX`/`PX`/`NX`) in v1.
11. **Default bind `127.0.0.1:6379`**, overridable by flags.
12. **Layout:** the directory structure above; exactly one `server.c`.

## Academic value — concepts demonstrated

TCP networking & socket lifecycle (create/bind/listen/accept/close) ·
non-blocking I/O · `epoll` event-driven architecture · partial reads and
writes · protocol framing & incremental parsing · pipelining · dynamic
memory management & ownership · hash tables with resizing · timers /
cron · `signalfd` and signal-driven shutdown · resource limits &
backpressure · development under ASan/UBSan and load testing.
