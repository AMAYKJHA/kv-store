# rkv

A minimal Redis-like in-memory key-value store, written in C11.

`rkv` is a single-process, single-threaded server that speaks a subset of
the RESP2 protocol — enough that the real `redis-cli` can talk to it. It's
a learning project for a networks & systems course, focused on the core
mechanics: an `epoll` event loop, incremental protocol parsing, and a hash
table with key expiry. No clustering, replication, pub/sub, or persistence.

## Status

Project scaffolding only — no implementation yet. Both binaries build and
run, but print a placeholder. See [`docs/PLAN.md`](docs/PLAN.md) for the
phased build roadmap and [`docs/DESIGN.md`](docs/DESIGN.md) for the
architecture.

## Prerequisites

- **GCC** (or Clang) with C11 support
- **GNU Make**
- A Linux host — the networking layer will use `epoll`
- Optional: `redis-cli` for manual testing, `clang-format` for `make
  format`

## Getting started

Clone the repo and build the debug binaries:

```sh
make
```

This produces two executables in `bin/`:

```sh
./bin/rkv-server    # the key-value server
./bin/rkv-cli       # a thin test client
```

The default `make` target builds in **debug** mode with AddressSanitizer
and UndefinedBehaviorSanitizer enabled and no optimization — use this while
developing. For an optimized build with asserts compiled out:

```sh
make MODE=release
```

Run the unit tests (built and executed one binary per `tests/test_*.c`):

```sh
make test
```

Remove all build output (`build/` and `bin/`):

```sh
make clean
```

### All make targets

| Target | What it does |
| --- | --- |
| `make` / `make all` | Build `rkv-server` and `rkv-cli` (debug) |
| `make server` | Build only the server |
| `make cli` | Build only the client |
| `make MODE=release` | Optimized build, `-O2 -DNDEBUG`, no sanitizers |
| `make test` | Build and run all unit tests |
| `make format` | Reformat sources with `clang-format` |
| `make tidy` | Run `clang-tidy` if configured |
| `make clean` | Delete `build/` and `bin/` |

Binaries land in `bin/`; object and dependency files go in
`build/<mode>/`. Both directories are git-ignored.

## Layout

```
include/rkv/     public headers (one per module)
src/
  main.c         server entry point
  cli.c          client entry point
  net/           event loop, sockets, connection state
  resp/          RESP protocol parse + serialize
  store/         hash table, key expiry, value types
  util/          sds-style strings, logging, allocation helpers
tests/           one test binary per file (test_*.c)
docs/DESIGN.md   architecture notes and decisions
docs/PLAN.md     phased build roadmap
```

## Target commands (v1)

Once implemented, `rkv` will support:

- **strings**: `SET`, `GET`, `DEL`, `EXISTS`, `INCR`, `DECR`
- **expiry**: `EXPIRE`, `TTL`, `PERSIST`
- **keyspace**: `KEYS`, `DBSIZE`, `FLUSHDB`
- **misc**: `PING`, `ECHO`, `COMMAND`, `QUIT`

## Conventions

- Public symbols are prefixed `rkv_`; each `src/<mod>/x.c` pairs with
  `include/rkv/<mod>_x.h` where it exposes an API.
- Functions returning `int` use `0` for success and a negative `RKV_E*`
  code for failure. Functions returning pointers use `NULL`.
- No global mutable state outside `src/server.c`.
- Code is formatted with `clang-format` (see `.clang-format`); run
  `make format` before committing.
