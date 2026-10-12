# bz-planner

[![CI](https://github.com/rp-projects-devs/bz-planner/actions/workflows/ci.yml/badge.svg)](https://github.com/rp-projects-devs/bz-planner/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

A command-line planner for **craft flipping** on the Hypixel SkyBlock Bazaar: find which items are
worth crafting, through which recipe path, and exactly which Bazaar orders to place within your
order slots, budget and the market's liquidity.

> **Status:** v1.0. Every command works; craft paths and plans are checked against an exhaustive
> enumeration and the Python prototype on the full NEU repository (see [Correctness](#correctness)).

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
- **Constrained planning**: the most profitable quantity and craft path within your order slots,
  budget and a share of the item's weekly sales volume, with the exact orders and crafts.
- **Global scan**: every craft of the Bazaar ranked by profit per unit (13 ms) or, within your
  limits, by the total profit of its best plan (94 ms on 8 threads with OpenMP).

## Requirements

| Tool         | Minimum version                         |
|--------------|-----------------------------------------|
| C++ compiler | GCC 13, Clang 17 or MSVC 19.29 (C++20)  |
| CMake        | 3.25                                    |
| OpenSSL      | 3.0 (HTTPS client)                      |
| OpenMP       | optional, enabled by default (NEU parsing, sized scan) |

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

### What should I craft? A complete session

With 21 free order slots and 200M coins, from a fresh clone, ready to copy and paste:

```sh
# Once: build (also downloads the NEU recipes into data/neu)
cmake -S . -B build
cmake --build build -j

# Every session: fresh prices, the best crafts within your limits, then the orders for one of them
./build/app/bz update
./build/app/bz scan --slots 21 --budget 200m --liquidity 10 --min-volume 10000 --top 10
./build/app/bz plan SILVER_FANG --slots 21 --budget 200m --liquidity 10

# From time to time: new NEU recipes
cmake --build build --target neu-update
```

Replace `SILVER_FANG` with an item from your scan. The same session with its output:

```text
$ cmake -S . -B build                  # also downloads the NEU repository (~20 MB) into data/neu
-- Downloading NEU repository from https://github.com/NotEnoughUpdates/NotEnoughUpdates-REPO/...
-- NEU repository ready in data/neu
$ cmake --build build -j

$ ./build/app/bz update                # fresh Bazaar prices; the only command using the network
Bazaar: 2201 products, saved to data/cache/bazaar-prices.json
NEU:    2562 recipes from 8937 files (0 skipped)
NEU recipes are refreshed with: cmake --build <build-dir> --target neu-update

$ ./build/app/bz scan --slots 21 --budget 200m --liquidity 10 --min-volume 10000 --top 10
   #  Item                                  Quantity Orders   Invested     Profit   Margin  Limited by
   1  SILVER_FANG                              35321     13     92.96M    253.32M   272.5%  liquidity
   2  SPIKED_BAIT                             527314     16    134.44M    236.54M   175.9%  liquidity
   3  SUPER_COMPACTOR_3000                      2315     11    199.93M    216.46M   108.3%  budget
   4  ENCHANTED_SHARK_FIN                       3418      8     42.77M    210.17M   491.4%  liquidity
   5  FUEL_GABAGOOL                            10039      5    111.58M    189.59M   169.9%  liquidity
   6  WHALE_BAIT                               48716      3    200.00M    171.81M    85.9%  budget
   7  ENCHANTED_TENDER_WOOD                     1254      3     28.08M    160.02M   570.0%  liquidity
   8  ENCHANTED_EYE_OF_ENDER                   31360     21    145.03M    138.95M    95.8%  order slots
   9  ENCHANTED_FERMENTED_SPIDER_EYE            3929     12    199.96M    131.86M    65.9%  budget
  10  REVENANT_VISCERA                          6753     17    199.99M    113.42M    56.7%  budget

$ ./build/app/bz plan SILVER_FANG --slots 21 --budget 200m --liquidity 10
Quantity       35321
Orders used    13
Total cost     92.96M
Total revenue  346.28M
Profit         253.32M
Limited by     liquidity

Orders
  ENCHANTED_GHAST_TEAR                   883025 units   13 orders  @      105.3  =     92.96M

Crafts
  SILVER_FANG                             35321 crafts  → 35321 units
```

With limits, `scan` sizes a plan for every craftable item and ranks them by the total profit you
can make in one go; `plan` then gives the exact orders and crafts. Prices are a snapshot of
October 2026: run `bz update` before trusting any number.

- **Always pass `--liquidity`** (10 = sell at most 10 % of the item's weekly sales volume).
  Without it, `scan` assumes the market buys everything you craft and ranks items with a single
  overpriced sell offer and no real volume first; `bz` prints a note to remind you.
- **`--min-volume`** drops thin markets whose prices move with a single order.
- **Cheapest per unit is not best under limits.** Crafting enchanted ghast tears yourself is
  cheaper per fang, but needs 125 ghast tears per fang: 62 orders for the same quantity. Within 21
  slots that path earns about 90M; buying enchanted ghast tears earns 253M. `plan` and a sized
  `scan` search the craft tree for such paths (see [How it works](#how-it-works)).
- Without limits, `bz scan` ranks crafts by profit per unit, and `bz item ID` shows an item's
  prices, recipe tree and cheapest paths.

### Commands and options

```sh
bz update                                   # download fresh Bazaar prices (only network access)
bz scan --slots 21 --budget 200m --liquidity 10   # best crafts within your limits
bz scan --min-volume 500000 --min-margin 5        # best profit per unit, no sizing
bz plan ENCHANTED_COOKIE --budget 50m       # budget only; 21 slots if no limit at all
bz item ENCHANTED_COOKIE                    # prices, recipe tree and ranked craft paths
bz neu-cache                                # rebuild the recipe cache from data/neu
cmake --build build --target neu-update     # download the latest NEU recipes
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

| Command option      | Commands             | Default | Description                                  |
|---------------------|----------------------|---------|----------------------------------------------|
| `--slots N`         | `plan` `scan`        |         | Bazaar order slots (`plan`: 21 if no limit)  |
| `--budget AMOUNT`   | `plan` `scan`        |         | Coins available for ingredients              |
| `--liquidity PCT`   | `plan` `scan`        |         | Max share of the item's weekly sales volume  |
| `--min-volume N`    | `scan`               | `0`     | Minimum weekly sales volume of the item      |
| `--min-margin PCT`  | `scan`               | `0`     | Minimum profit margin                        |
| `--max-cost AMOUNT` | `scan`               |         | Maximum cost of one craft                    |
| `--top N`           | `scan`               | `20`    | Number of results                            |
| `--depth N`         | `item` `plan` `scan` | `5`     | Recipe levels explored below the target      |
| `--paths N`         | `item` `plan` `scan` | `10`    | Cheapest paths listed / kept per item        |

The defaults of `--depth` and `--paths` never hide a better choice on the real data: see
[Correctness](#correctness).

The weekly sales volume depends on how you sell: sell offers are filled by players buying
instantly (`buyMovingWeek`), instant sales fill buy orders (`sellMovingWeek`).

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
   or crafting every ingredient. An item is never needed again inside its own recipe tree
   (buying it to craft it never beats buying it), so cycles are never followed. Only the
   `--paths` cheapest options of every item and partial recipe are kept: costs add up, so this
   pruning keeps the cheapest paths. Each path records its tree of crafts.
4. **Plans** (`make_plan`). For one path, the largest quantity that fits the limits is found by
   binary search. Quantities follow the craft tree: a recipe yielding 3 units and needed for 4
   runs twice, so leftovers are paid for. Purchases are then split into 71 680-unit orders.
5. **Choosing the path to plan** (`plan_paths`, `best_plan`). Under order slots the cheapest path
   per unit is often not the most profitable, and the right one can rank beyond the hundredth.
   A second search works on whole quantities: for a quantity, it keeps for every item the craft
   options whose orders fit the limits and that no cheaper option buying no more of any item
   beats (so purchases shared by two branches are counted once). Run on a geometric grid of
   quantities, it yields the paths worth sizing; each is planned and the most profitable wins.
6. **Scan** (`scan`). Without limits, every craftable Bazaar item is ranked by the unit profit
   of its cheapest path. With limits, by the profit of its best plan, items in parallel.

## Correctness

Checked on the full NEU repository and real Bazaar snapshots, independently of the code under
test:

- **Craft paths**: for each of the 2 513 craftable items, the 10 cheapest paths equal those of
  an exhaustive enumeration without pruning, at depths 1 to 5 (items over 200 000
  combinations are skipped: 2 at depth 4, 14 at depth 5). Every path's purchases match its craft
  tree, no path buys an item inside its own tree, and none is cheaper than `compute_min_costs`.
- **Plan sizing**: on 14 012 path × constraint cases, the quantity is feasible, one more unit is
  not, and orders, slots and costs match a separate simulation of the craft tree.
- **Path choice under limits**: for the 369 craftable Bazaar items × 7 sets of limits × 2 pricing
  modes, on two snapshots, the plan chosen by `bz` is as profitable as the best plan over every
  path of the exhaustive enumeration, whenever that plan makes a profit (5 152 cases whose
  enumeration has fewer than 5 000 distinct paths).
  To stay fast, the search keeps at most 256 options per item and quantity; on real data this
  cap trims 0.6 % of them without changing any checked result, so it is a measured heuristic,
  not a proof.
- **Defaults**: `--depth 5` gives the same cheapest paths and the same best plans as depth 8
  (depth 4 misses some), and the chosen plan is identical with `--paths` 1, 10 or 1 000.
  Choosing only among the cheapest paths per unit, as the prototype does, missed a better plan
  in 126 of 3 690 cases with depth 3 and 10 paths, and still in 6 with depth 8 and 1 000 paths.
- **Python prototype** (`reference/`): parsing and pricing match it. Its planner rounds each
  purchase once over the whole quantity (`ceil(amount × q)`), which underestimates nested
  multi-output recipes; this happens in 155 of the plans above, up to 6 % of the cost. `bz` rounds
  every recipe to whole crafts instead. It also skips the prototype's paths that buy an item to
  craft that same item.

Unit tests cover parsing, the graph, pricing and every algorithm on small synthetic economies.

## Performance

Measured with [hyperfine](https://github.com/sharkdp/hyperfine) on an 8-thread machine; raw
results in `bench/results/`:

| Benchmark (`bench/results/`)                       | 1 thread | 8 threads | Speedup |
|----------------------------------------------------|---------:|----------:|--------:|
| Parsing every NEU file (`load-threads.md`)         | 171 ms   | 86 ms     | ×1.99   |
| `bz scan --slots 21 --budget 200m --liquidity 10`  | 464 ms   | 94 ms     | ×4.95   |
| `bz scan`, ranking by unit profit (`scan-threads.md`) | 13.0 ms | 13.4 ms  | none    |

A scan without limits spends most of its time reading the two JSON caches; the graph work
takes under 1 ms, so it stays sequential. Sizing a plan for every item is where the time goes,
so that scan runs items in parallel with OpenMP.

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
| `BZ_ENABLE_OPENMP`      | `ON`    | Parallelize NEU parsing and sized scans  |
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
- [x] Global scan ranked by unit profit or by the profit of a sized plan, parallelized with OpenMP
      where measured useful

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
