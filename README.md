# bz-planner

[![CI](https://github.com/<user>/bz-planner/actions/workflows/ci.yml/badge.svg)](https://github.com/<user>/bz-planner/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

A command-line planner for **craft flipping** on the Hypixel SkyBlock Bazaar: find which items are
worth crafting, through which recipe path, and exactly which Bazaar orders to place within your
order slots, budget and the market's liquidity.

> **Status:** v0.1 skeleton. Data loading, the item graph, pricing and the CLI work; the solver,
> planner and scanner are specified and tested but not implemented yet (see [Roadmap](#roadmap)).

## Features

- **Automatic data import**: recipes from the [NotEnoughUpdates repository][neu], live prices from
  the public Hypixel Bazaar API, cached locally by `bz update` in a compact form.
- **Item graph**: crafting table recipes, linked in both directions ("what is X made of" /
  "what uses X"), cycles included. Only Bazaar prices are used: NPC shop offers, forge recipes
  and other acquisition methods are ignored, exactly like the Python reference.
- **Trading strategies**: buy ingredients instantly or with buy orders, sell instantly or with sell
  offers, with an optional Bazaar sales tax.
- **Constrained planning**: the largest quantity you can craft within your order slots, budget and
  a share of the item's weekly volume, with an order-by-order breakdown.
- **Global scan**: the most profitable crafts across the whole Bazaar, parallelized with OpenMP.

## Requirements

| Tool         | Minimum version                         |
|--------------|-----------------------------------------|
| C++ compiler | GCC 13, Clang 17 or MSVC 19.29 (C++20)  |
| CMake        | 3.25                                    |
| OpenSSL      | 3.0 (HTTPS client)                      |
| OpenMP       | optional, enabled by default            |

The other dependencies ([nlohmann/json][json], [CLI11][cli11], [cpp-httplib][httplib] and
[Catch2][catch2]) are found on the system or fetched automatically at pinned versions.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The first configuration downloads the NEU repository (~20 MB) into `data/neu`. Refresh it later
with:

```sh
cmake --build build --target neu-update
```

## Usage

```sh
bz update                                   # download fresh Bazaar prices (only network access)
bz item ENCHANTED_COCOA                     # prices, recipe tree and ranked craft paths
bz plan ENCHANTED_COCOA --slots 21 --budget 50m --liquidity 10
bz plan ENCHANTED_COCOA --budget 50m        # budget only; 21 slots if no limit at all
bz scan --min-volume 500000 --min-margin 5 --top 20
```

Global options apply to every command and go before it:

| Option            | Default  | Description                                    |
|-------------------|----------|------------------------------------------------|
| `--buy MODE`      | `order`  | `instant` or `order` for ingredients           |
| `--sell MODE`     | `offer`  | `instant` or `offer` for the crafted item      |
| `--tax RATE`      | `0`      | Bazaar sales tax, as a fraction (in game: `0.0125`) |
| `--cache-age SEC` | `600`    | Warn when cached prices are older than this    |
| `--data-dir DIR`  | `data/`  | Location of NEU data and caches                |
| `--no-color`      |          | Plain output (the `NO_COLOR` variable also works) |

Coin amounts accept suffixes: `750k`, `50m`, `2.5b`. Run `bz <command> --help` for every option.

### How prices are chosen

The Hypixel API names prices from the order book's point of view, which is easy to get backwards:

| Strategy       | Price used                       | API field   |
|----------------|----------------------------------|-------------|
| Instant buy    | lowest sell offer                | `buyPrice`  |
| Buy order      | highest buy order                | `sellPrice` |
| Instant sell   | highest buy order, minus tax     | `sellPrice` |
| Sell offer     | lowest sell offer, minus tax     | `buyPrice`  |

The defaults (`--buy order --sell offer --tax 0`) reproduce the Python reference, whose
`"buy"` side reads `sellPrice` and `"sell"` side reads `buyPrice`, without tax.

## Project layout

```
core/      bzcore library: model, data loading, pricing and algorithms (no network access)
app/       the `bz` executable: CLI, terminal output and the HTTP client
tests/     Catch2 unit tests and JSON fixtures
cmake/     NEU repository download
reference/ the original Python prototype, used as a test oracle
```

The core never touches the network, so every algorithm is testable with fixtures. Only
`app/http.cpp` talks to the Hypixel API.

## Development

```sh
# Debug build with AddressSanitizer and UBSan, warnings as errors
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBZ_ENABLE_SANITIZERS=ON -DBZ_WARNINGS_AS_ERRORS=ON
cmake --build build-asan && ctest --test-dir build-asan

# Specification tests of features not implemented yet
./build/tests/bz_tests "[todo]"

# API documentation (requires Doxygen), output in build/html
cmake -S . -B build -DBZ_BUILD_DOCS=ON && cmake --build build --target docs

# Formatting
find core app tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

| CMake option            | Default | Description                              |
|-------------------------|---------|------------------------------------------|
| `BZ_BUILD_TESTS`        | `ON`    | Build the unit tests                     |
| `BZ_ENABLE_OPENMP`      | `ON`    | Parallelize the scanner                  |
| `BZ_ENABLE_SANITIZERS`  | `OFF`   | ASan + UBSan instrumentation             |
| `BZ_WARNINGS_AS_ERRORS` | `OFF`   | Fail the build on warnings               |
| `BZ_FETCH_NEU`          | `ON`    | Download NEU data at configure time      |
| `BZ_BUILD_DOCS`         | `OFF`   | Add the `docs` target                    |

Continuous integration builds with GCC and Clang, runs the tests under sanitizers and checks
formatting on every push and pull request.

## Roadmap

**v1**
- [x] NEU and Bazaar import (crafting recipes only, checked against the Python reference on the
      full NEU repository), item graph, pricing modes and tax, CLI
- [ ] Minimal acquisition cost of every item (fixed-point relaxation over a cyclic graph)
- [ ] Ranked craft paths with top-k pruning
- [ ] Constrained planner: order slots, budget, liquidity, order breakdown
- [ ] Global scan parallelized with OpenMP, with speedup benchmarks

**Later**
- Forge recipes ranked by profit per hour
- NPC sell prices for NPC flips
- JSON output and a configuration file
- Detection of manipulated prices
- Web API and front end

## Data sources and disclaimer

- Recipes come from the [NotEnoughUpdates repository][neu] (MIT license), downloaded at build time
  and not redistributed with this project.
- Prices come from the public Hypixel API.

This project is not affiliated with or endorsed by Hypixel Inc. or Mojang Studios.

## License

[MIT](LICENSE)

[neu]: https://github.com/NotEnoughUpdates/NotEnoughUpdates-REPO
[json]: https://github.com/nlohmann/json
[cli11]: https://github.com/CLIUtils/CLI11
[httplib]: https://github.com/yhirose/cpp-httplib
[catch2]: https://github.com/catchorg/Catch2
