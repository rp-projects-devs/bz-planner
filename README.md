# bz-planner

[![CI](https://github.com/rp-projects-devs/bz-planner/actions/workflows/ci.yml/badge.svg)](https://github.com/rp-projects-devs/bz-planner/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

A command-line planner for **craft flipping** on the Hypixel SkyBlock Bazaar: find which items are
worth crafting, through which recipe path, and exactly which Bazaar orders to place within your
order slots, budget and the market's liquidity.

> **Status:** v1.0. Every command works; craft paths and plans are checked against the Python
> prototype on the full NEU repository (see [Correctness](#correctness)).

## Features

- **Automatic data import**: recipes from the [NotEnoughUpdates repository][neu], live prices from
  the public Hypixel Bazaar API. Both are cached locally in a compact form, so a command reads
  two small files instead of thousands of NEU files and a multi-megabyte API response.
- **Item graph**: crafting table recipes, linked in both directions ("what is X made of" /
  "what uses X"), cycles included. Only Bazaar prices are used: NPC shop offers, forge recipes
  and other acquisition methods are ignored, exactly like the Python reference.
- **Trading strategies**: buy ingredients instantly or with buy orders, sell instantly or with sell
  offers, with an optional Bazaar sales tax.
- **Ranked craft paths**: every way to craft an item down to Bazaar purchases, a few recipe
  levels deep, cheapest first.
- **Constrained planning**: the largest quantity you can craft within your order slots, budget and
  a share of the item's weekly volume, with an order-by-order breakdown.
- **Global scan**: the most profitable crafts across the whole Bazaar, filtered by volume, margin
  and cost, in about 13 ms for a full command.

## Requirements

| Tool         | Minimum version                         |
|--------------|-----------------------------------------|
| C++ compiler | GCC 13, Clang 17 or MSVC 19.29 (C++20)  |
| CMake        | 3.25                                    |
| OpenSSL      | 3.0 (HTTPS client)                      |
| OpenMP       | optional, enabled by default (NEU parsing) |

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

### Data and caches

| File                            | Written by                  | Content                        |
|---------------------------------|-----------------------------|--------------------------------|
| `data/neu/`                     | configuration, `neu-update` | the NEU repository             |
| `data/cache/neu-recipes.json`   | `neu-update`, `bz neu-cache`| crafting recipes, by output id |
| `data/cache/bazaar-prices.json` | `bz update`                 | the four price fields in use   |

Recipes only change with the NEU repository, so they are parsed once into
`neu-recipes.json`: `neu-update` downloads NEU then rebuilds the cache, and every command reads
the cache in a few milliseconds. The cache is also rebuilt automatically when it is missing,
unreadable or written by a build with another parser version (`kRecipeCacheFormat` in
`core/include/bz/neu.hpp`, to bump whenever the NEU parsing rules change). After editing the NEU
files by hand, run `bz neu-cache`.

## Usage

```sh
bz update                                   # download fresh Bazaar prices (only network access)
bz item ENCHANTED_COCOA                     # prices, recipe tree and ranked craft paths
bz plan ENCHANTED_COCOA --slots 21 --budget 50m --liquidity 10
bz plan ENCHANTED_COCOA --budget 50m        # budget only; 21 slots if no limit at all
bz scan --min-volume 500000 --min-margin 5 --top 20
bz neu-cache                                # rebuild the recipe cache from data/neu
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

| Command option     | Commands           | Default | Description                                 |
|--------------------|--------------------|---------|---------------------------------------------|
| `--depth N`        | `item` `plan` `scan` | `3`   | Recipe levels explored below the target     |
| `--paths N`        | `item` `plan` `scan` | `10`  | Paths kept per item                         |
| `--slots N`        | `plan`             |         | Bazaar order slots (21 if no limit is given) |
| `--budget AMOUNT`  | `plan`             |         | Coins available for ingredients             |
| `--liquidity PCT`  | `plan`             |         | Max share of the item's weekly volume       |
| `--min-volume N`   | `scan`             | `0`     | Minimum weekly volume bought of the item    |
| `--min-margin PCT` | `scan`             | `0`     | Minimum profit margin                       |
| `--max-cost AMOUNT`| `scan`             |         | Maximum cost of one craft                   |
| `--top N`          | `scan`             | `20`    | Number of results                           |

### Example

```text
$ bz scan --min-volume 500000 --min-margin 5 --top 5
   #  Item                                       Cost    Revenue     Profit   Margin
   1  ENCHANTED_SUGAR_CANE                     17.92k    102.50k     84.58k   472.0%
   2  ENCHANTED_GOLD_BLOCK                     12.80k     78.16k     65.36k   510.6%
   3  ENCHANTED_BAKED_POTATO                   20.48k     77.24k     56.76k   277.2%
   4  ENCHANTED_DIAMOND_BLOCK                 158.94k    204.90k     45.96k    28.9%
   5  ENCHANTED_GOLDEN_CARROT                  36.57k     77.32k     40.75k   111.4%

$ bz plan ENCHANTED_COOKIE --slots 21 --budget 500m --liquidity 5
Quantity       6760
Orders used    16
Total cost     499.97M
Total revenue  563.28M
Profit         63.31M
Limited by     budget

Orders
  ENCHANTED_COCOA                       1081600 units   16 orders  @      462.3  =    499.97M
```

The cheapest path for an enchanted cookie buys 25 600 cocoa beans, but at 71 680 units per order
it fills the slots after a few dozen crafts. `plan` sizes every ranked path and keeps the one
with the highest total profit, here buying enchanted cocoa instead.

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

## How it works

1. **Item graph.** Bazaar products and crafting recipes become dense item and recipe indices,
   linked in both directions. Tarjan's algorithm groups crafting cycles (an item and its
   compacted block) into strongly connected components, numbered so that ingredients come
   before the items made from them.
2. **Minimal costs** (`compute_min_costs`). Components are processed in that order: each item
   costs the minimum of its Bazaar price and of its recipes; cycles are relaxed until stable.
3. **Craft paths** (`best_paths`). A depth-limited search expands the target's recipes, buying
   or crafting every ingredient, never following a cycle. Only the `--paths` cheapest options
   of every item and partial recipe are kept: costs add up, so this pruning is exact.
4. **Plans** (`make_plan`). The largest quantity that fits the limits is found by binary search,
   with ingredient quantities rounded up to whole units and split into 71 680-unit orders.
5. **Scan** (`scan`). Every craftable Bazaar item is ranked by the unit profit of its cheapest
   path.

## Correctness

The Python prototype in `reference/` is the behavioral oracle. On the full NEU repository and a
real Bazaar snapshot (369 craftable items):

- the 10 cheapest path costs of every item match the prototype's exhaustive enumeration
  exactly, at depths 1 to 4;
- maximal quantities, order slots and total costs match on 3 424 path × constraint cases.

Unit tests cover parsing, the graph, pricing and every algorithm on a small synthetic economy.

## Performance

Measured with [hyperfine](https://github.com/sharkdp/hyperfine) on an 8-thread machine; raw
results in `bench/results/`:

| Benchmark                                         | 1 thread | 8 threads | Speedup |
|---------------------------------------------------|---------:|----------:|--------:|
| Parsing every NEU file (`load-threads.md`)        | 171 ms   | 86 ms     | ×1.99   |
| Full `bz scan` from the caches (`scan-threads.md`)| 13.0 ms  | 12.6 ms   | none    |

A full `scan` spends most of its time reading the two JSON caches; the graph work itself takes
under 1 ms. Parallelizing the scan with OpenMP was measured and brought no gain, so only the NEU
parser is parallel.

## Project layout

```
core/      bzcore library: model, data loading, pricing and algorithms (no network access)
app/       the `bz` executable: CLI, terminal output and the HTTP client
tests/     Catch2 unit tests and JSON fixtures
cmake/     NEU repository download and recipe cache refresh
reference/ the original Python prototype, used as a test oracle
```

The core never touches the network, so every algorithm is testable with fixtures. Only
`app/http.cpp` talks to the Hypixel API.

## Development

```sh
# Debug build with AddressSanitizer and UBSan, warnings as errors (like CI)
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBZ_ENABLE_SANITIZERS=ON \
      -DBZ_WARNINGS_AS_ERRORS=ON -DBZ_FETCH_NEU=OFF
cmake --build build-asan && ctest --test-dir build-asan

# One group of tests: [util] [neu] [bazaar] [catalog] [pricing] [solver] [planner] [scanner]
./build/tests/bz_tests "[solver]"

# API documentation (requires Doxygen), output in build/html
cmake -S . -B build -DBZ_BUILD_DOCS=ON && cmake --build build --target docs

# Formatting, with the same clang-format version as CI
find core app tests \( -name '*.cpp' -o -name '*.hpp' \) | xargs clang-format-18 -i
```

| CMake option            | Default | Description                              |
|-------------------------|---------|------------------------------------------|
| `BZ_BUILD_TESTS`        | `ON`    | Build the unit tests                     |
| `BZ_ENABLE_OPENMP`      | `ON`    | Parallelize NEU parsing                  |
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
- [x] Minimal acquisition cost of every item (relaxation over strongly connected components)
- [x] Ranked craft paths with top-k pruning
- [x] Constrained planner: order slots, budget, liquidity, order breakdown
- [x] Global scan, with benchmarks (OpenMP measured, kept for NEU parsing only)

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
