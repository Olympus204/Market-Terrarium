# Market Terrarium

Market Terrarium is a C++20 agent-based market simulator built around limit order books.

The project models a closed market with finite cash and shares, then lets autonomous trader populations interact through buy, sell, cancel and wait decisions. The aim is not to reproduce a specific real market, but to create a small experimental environment where different trading behaviours can produce emergent price, ownership, wealth and survival dynamics.

The simulation includes a live Dear ImGui / ImPlot dashboard for inspecting the market while it runs.

## Current features

- Limit order books with price-time priority
- Multiple instruments with independent order books
- Seeded simulation runs for reproducible experiments
- Finite trader cash and share holdings
- Cash and share reservation for outstanding orders
- Immediate trade settlement
- Recurring trader costs
- Trader failure, liquidation and replacement
- A central bank/issuer that introduces shares and recycles excess cash
- Smoothed reference prices derived from executed trades
- Live controls for pausing, stepping and changing simulation speed
- Dockable diagnostic windows showing:
  - price history
  - trader population
  - average cash and wealth by strategy
  - ownership by strategy
  - deaths by strategy
  - bank state
  - trade and order activity

## Trader strategies

### Random

Random traders choose stochastically between valid actions. They select instruments uniformly, quote around the currently observed price, and choose quantities within their available cash or holdings.

### Mean value

Mean-value traders compare the current observed price with a rolling historical mean. Their buy, sell and cancellation weights increase as the current price or resting order price moves further from that mean.

### Portfolio rebalancer

Portfolio rebalancers attempt to keep their wealth distributed evenly between cash and the available instruments. They account for outstanding orders when deciding what to buy, sell or cancel, and size orders toward the portfolio allocation they are trying to reach.

## Example experiment

The default configuration in `src/main.cpp` creates four instruments with identical starting prices and supplies, then introduces equal populations of random, mean-value and portfolio-rebalancing traders.

Because the instruments begin symmetrically, any later divergence in price and ownership is produced by the sequence of trades and strategy interactions rather than by different initial fundamentals.

A fixed seed is supplied to the simulation's `std::mt19937_64` engine so identical starting conditions can be replayed deterministically.

## Building

### Requirements

- A C++20 compiler
- CMake 3.20 or newer
- OpenGL
- GLFW 3
- Ninja is optional, but used in the examples below

Dear ImGui and ImPlot are vendored in `external/`.

### Configure and build

```bash
cmake -S . -B build -G Ninja
cmake --build build
```
### Run the simulator
```bash
./build/market_terrarium
```
### Run the tests
```bash
./build/tests
```

## Configuring and experiment

Experiments are currently configured in ``` src/main.cpp ```
for example
``` bash
Simulation sim{1001, 150000};

sim.add_instrument(1, "ALPHA", 100);
sim.add_instrument(2, "BETA", 100);
sim.add_instrument(3, "GAMMA", 100);
sim.add_instrument(4, "DELTA", 100);

sim.set_recurring_costs(100, 100);

sim.introduce_holdings(1, 200);
sim.introduce_holdings(2, 200);
sim.introduce_holdings(3, 200);
sim.introduce_holdings(4, 200);

sim.set_bank_recycling(5000, 1, 0.25);

for (int i = 0; i < 30; ++i)
{
    sim.queue_trader(TraderType::random);
    sim.queue_trader(TraderType::mean_value);
    sim.queue_trader(TraderType::portfolio_rebalancer);
}
```
The first simulation argument is the random seed. Keeping the seed and all starting conditions unchanged should reproduce the same run.

## Project structure
``` bash
include/     Public headers for the market, traders, simulation and decisions
src/         Core implementation and GUI entry point
tests/       Order book, market, trader and simulation tests
external/    Vendored Dear ImGui and ImPlot sources
```

the core simulation is built as the ```bash market_core``` library. The GUI is kept in a separate ```bash gui_support``` target, while the tests link only against the core simulation.

## Current limitations
Market Terrarium is intentionally simplified. In the current version:

- there are no company fundamentals or external news processes
- traders cannot short sell
- there are no market-maker or momentum strategies yet
- the bank is an artificial liquidity mechanism rather than a realistic financial institution
- reference prices are smoothed simulation values rather than an exchange-style official last price
- experiment configuration is still compiled into src/main.cpp rather than loaded from a config file

These are useful constraints for the current project because they make it easier to isolate the behaviour created by the trader strategies themselves.

### Planned directions
Future experiments may include momentum and market-making agents, company events, configurable latency costs, multiple markets, and populations of evolving or neural-network traders.

The longer-term goal is to use the terrarium as a controlled environment for asking questions such as how different strategy populations affect volatility, ownership, survival and the emergence of higher-frequency behaviour.
