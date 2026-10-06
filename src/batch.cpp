#include "simulation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

struct PriceStats
{
    double sum = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    double return_sum = 0.0;
    double return_square_sum = 0.0;
    double absolute_return_sum = 0.0;
    double previous_price = 0.0;
    int samples = 0;
    int return_samples = 0;
    int changed_samples = 0;
    void add(double price)
    {
        sum += price;
        minimum = std::min(minimum, price);
        maximum = std::max(maximum, price);
        if (samples > 0 && previous_price > 0.0)
        {
            double price_return =
                (price - previous_price) / previous_price;
            return_sum += price_return;
            return_square_sum += price_return * price_return;
            absolute_return_sum += std::abs(price_return);
            if (price != previous_price)
            {
                ++changed_samples;
            }
            ++return_samples;
        }
        previous_price = price;
        ++samples;
    }
    double mean() const
    {
        if (samples == 0)
        {
            return 0.0;
        }
        return sum / static_cast<double>(samples);
    }
    double return_volatility() const
    {
        if (return_samples == 0)
        {
            return 0.0;
        }
        double mean_return =
            return_sum / static_cast<double>(return_samples);
        double mean_square =
            return_square_sum /
            static_cast<double>(return_samples);
        double variance =
            mean_square - mean_return * mean_return;
        return std::sqrt(std::max(0.0, variance));
    }
    double mean_absolute_return() const
    {
        if (return_samples == 0)
        {
            return 0.0;
        }
        return absolute_return_sum /
            static_cast<double>(return_samples);
    }
    double price_change_fraction() const
    {
        if (return_samples == 0)
        {
            return 0.0;
        }
        return static_cast<double>(changed_samples) /
            static_cast<double>(return_samples);
    }
};

enum class TaxWindowPhase
{
    none,
    pre,
    post
};

struct TaxWindowPosition
{
    TaxWindowPhase phase = TaxWindowPhase::none;
    int event_tick = 0;
};

TaxWindowPosition get_tax_window_position(
    int tick,
    int total_ticks,
    bool taxation_enabled,
    int tax_season_length,
    int tax_window_ticks
)
{
    if (
        !taxation_enabled ||
        tick <= 0 ||
        tax_season_length <= 0 ||
        tax_window_ticks <= 0
    )
    {
        return {};
    }

    const int phase = tick % tax_season_length;

    if (phase == 0)
    {
        const int event_tick = tick;
        if (event_tick + tax_window_ticks - 1 <= total_ticks)
        {
            return {TaxWindowPhase::post, event_tick};
        }
        return {};
    }

    const int previous_event = tick - phase;
    const int next_event = previous_event + tax_season_length;

    if (
        phase >= tax_season_length - tax_window_ticks &&
        next_event + tax_window_ticks - 1 <= total_ticks
    )
    {
        return {TaxWindowPhase::pre, next_event};
    }

    if (
        phase < tax_window_ticks &&
        previous_event >= tax_season_length &&
        previous_event + tax_window_ticks - 1 <= total_ticks
    )
    {
        return {TaxWindowPhase::post, previous_event};
    }

    return {};
}

struct TaxWindowStats
{
    int ticks = 0;
    std::int64_t trades = 0;
    int snapshot_samples = 0;
    double neural_cash_fraction_sum = 0.0;
    double neural_ownership_sum = 0.0;
    double instrument_volatility_sum = 0.0;
    int instrument_volatility_samples = 0;
    double return_sum = 0.0;
    double absolute_return_sum = 0.0;
    int return_samples = 0;
    int current_event_tick = 0;
    std::map<int, double> previous_prices;
    void add_trades(int trades_this_tick)
    {
        ++ticks;
        trades += trades_this_tick;
    }
    void add_snapshot(
        const SimulationSnapshot& snapshot,
        int event_tick
    )
    {
        ++snapshot_samples;
        neural_cash_fraction_sum +=
            snapshot.neural_evolution_cash_fraction;
        double ownership_sum = 0.0;
        int ownership_samples = 0;
        for (int id : snapshot.instrument_ids)
        {
            auto ownership_it =
                snapshot.neural_evolution_percentage_of_each_instrument.find(id);
            if (
                ownership_it !=
                snapshot.neural_evolution_percentage_of_each_instrument.end()
            )
            {
                ownership_sum += ownership_it->second;
                ++ownership_samples;
            }
            auto volatility_it =
                snapshot.instrument_volatility.find(id);
            if (
                volatility_it !=
                snapshot.instrument_volatility.end()
            )
            {
                instrument_volatility_sum +=
                    volatility_it->second;
                ++instrument_volatility_samples;
            }
        }
        if (ownership_samples > 0)
        {
            neural_ownership_sum +=
                ownership_sum /
                static_cast<double>(ownership_samples);
        }
        if (current_event_tick != event_tick)
        {
            current_event_tick = event_tick;
            previous_prices.clear();
        }
        for (int id : snapshot.instrument_ids)
        {
            const double price =
                snapshot.instrument_reference_price.at(id).current_price;
            auto previous_it =
                previous_prices.find(id);
            if (
                previous_it != previous_prices.end() &&
                previous_it->second > 0.0
            )
            {
                const double price_return =
                    (price - previous_it->second) /
                    previous_it->second;
                return_sum += price_return;
                absolute_return_sum += std::abs(price_return);
                ++return_samples;
            }
            previous_prices[id] = price;
        }
    }
    double mean_trades_per_tick() const
    {
        return ticks > 0
            ? static_cast<double>(trades) /
                static_cast<double>(ticks)
            : 0.0;
    }
    double mean_neural_cash_fraction() const
    {
        return snapshot_samples > 0
            ? neural_cash_fraction_sum /
                static_cast<double>(snapshot_samples)
            : 0.0;
    }
    double mean_neural_ownership() const
    {
        return snapshot_samples > 0
            ? neural_ownership_sum /
                static_cast<double>(snapshot_samples)
            : 0.0;
    }
    double mean_instrument_volatility() const
    {
        return instrument_volatility_samples > 0
            ? instrument_volatility_sum /
                static_cast<double>(instrument_volatility_samples)
            : 0.0;
    }
    double mean_return() const
    {
        return return_samples > 0
            ? return_sum /
                static_cast<double>(return_samples)
            : 0.0;
    }
    double mean_absolute_return() const
    {
        return return_samples > 0
            ? absolute_return_sum /
                static_cast<double>(return_samples)
            : 0.0;
    }
};

struct RunResult
{
    SimulationSnapshot final_snapshot;
    std::map<int, PriceStats> price_stats;
    double mean_price_spread = 0.0;
    double max_price_spread = 0.0;
    double mean_relative_spread = 0.0;
    double max_relative_spread = 0.0;
    double runtime_seconds = 0.0;
    int last_trade_tick = 0;
    int active_trade_ticks = 0;
    int trades_last_1000_ticks = 0;
    double mean_active_orders = 0.0;
    int longest_neural_lifespan = 0;
    int tax_events_observed = 0;
    TaxWindowStats pre_tax;
    TaxWindowStats post_tax;
};

std::string instrument_name(int id)
{
    static const std::vector<std::string> names{
        "ALPHA",
        "BETA",
        "GAMMA",
        "DELTA",
        "EPSILON",
        "ZETA",
        "ETA",
        "THETA",
        "IOTA",
        "KAPPA",
        "LAMBDA",
        "MU"
    };

    if (id >= 1 && static_cast<std::size_t>(id) <= names.size())
    {
        return names[static_cast<std::size_t>(id - 1)];
    }

    return "INSTRUMENT_" + std::to_string(id);
}

std::string instrument_column_name(int id)
{
    static const std::vector<std::string> names{
        "alpha",
        "beta",
        "gamma",
        "delta",
        "epsilon",
        "zeta",
        "eta",
        "theta",
        "iota",
        "kappa",
        "lambda",
        "mu"
    };

    if (id >= 1 && static_cast<std::size_t>(id) <= names.size())
    {
        return names[static_cast<std::size_t>(id - 1)];
    }

    return "instrument_" + std::to_string(id);
}

struct Experiment
{
    std::string experiment_name;
    std::size_t condition_id = 0;

    std::uint64_t seed = 1001;
    int ticks = 250000;
    int sample_every = 100;

    int random_count = 120;
    int mean_count = 40;
    int rebalancer_count = 0;
    int neural_count = 40;

    int mean_memory = 20;

    int instrument_count = 4;
    int shares_per_instrument = 120;
    int starting_price = 100;

    std::int64_t initial_bank_cash = 250000;
    std::int64_t starting_cash = 1000;

    int cost_frequency = 1;
    std::int64_t cost_amount = 2;

    bool tax_enabled = true;
    int tax_season_length = 10000;
    int tax_window_ticks = 500;

    bool living_parent_selection = false;

    std::int64_t bank_recycle_target = 0;
    int bank_recycle_frequency = 1;
    double bank_recycle_fraction = 0.0;
};

struct SweepConfig
{
    std::string name = "experiment";

    std::vector<std::uint64_t> seeds{
        1001, 1002, 1003, 1004, 1005
    };
    std::vector<int> ticks{250000};
    std::vector<int> sample_every{100};

    std::vector<int> random_counts{120};
    std::vector<int> mean_counts{40};
    std::vector<int> rebalancer_counts{0};
    std::vector<int> neural_counts{40};

    std::vector<int> mean_memories{20};

    std::vector<int> instrument_counts{4};
    std::vector<int> shares_per_instrument{120};
    std::vector<int> starting_prices{100};

    std::vector<std::int64_t> initial_bank_cash{250000};
    std::vector<std::int64_t> starting_cash{1000};

    std::vector<int> cost_frequencies{1};
    std::vector<std::int64_t> cost_amounts{2};

    std::vector<bool> tax_enabled{true};
    std::vector<int> tax_season_lengths{10000};
    std::vector<int> tax_window_ticks{500};

    std::vector<bool> living_parent_selection{false};

    std::vector<std::int64_t> bank_recycle_targets{0};
    std::vector<int> bank_recycle_frequencies{1};
    std::vector<double> bank_recycle_fractions{0.0};

    // Constraints are ANDed together after the Cartesian product is built.
    // Example:
    // random+mean+rebalancer+neural==200
    std::vector<std::string> constraints;

    // 0 means automatic: hardware_concurrency - 2, minimum 1.
    unsigned int worker_threads = 0;
};

struct LiveRunState
{
    bool active = false;
    std::string experiment_name;
    std::size_t condition_id = 0;
    std::uint64_t seed = 0;

    int random_count = 0;
    int mean_count = 0;
    int rebalancer_count = 0;
    int neural_count = 0;
    int mean_memory = 0;
    int instrument_count = 0;
    int shares_per_instrument = 0;
    int starting_price = 0;

    bool tax_enabled = false;
    int tax_season_length = 0;
    int cost_frequency = 0;
    std::int64_t cost_amount = 0;

    int tick = 0;
    int total_ticks = 0;
    int total_trades = 0;
    int last_trade_tick = 0;
    int active_traders = 0;
    int active_orders = 0;
    int random_active = 0;
    int mean_active = 0;
    int rebalancer_active = 0;
    int neural_active = 0;
    int total_deaths = 0;
    int neural_deaths = 0;
    int longest_neural_lifespan = 0;
    int oldest_living_neural_id = 0;
    int oldest_living_neural_age = 0;
    std::int64_t oldest_living_neural_cash = 0;
    std::int64_t oldest_living_neural_wealth = 0;
    std::int64_t bank_cash = 0;
    std::vector<double> prices;
    double runtime_seconds = 0.0;
};

using ProgressCallback =
    std::function<void(int, const SimulationSnapshot&, int)>;

int percentage_or_zero(
    const std::map<int, int>& values,
    int instrument_id
)
{
    auto it = values.find(instrument_id);
    if (it == values.end())
    {
        return 0;
    }
    return it->second;
}

double ecology_diversity(
    int random_count,
    int mean_count,
    int rebalancer_count
)
{
    const int total =
        random_count + mean_count + rebalancer_count;

    if (total <= 0)
    {
        return 0.0;
    }

    const double r =
        static_cast<double>(random_count) /
        static_cast<double>(total);
    const double m =
        static_cast<double>(mean_count) /
        static_cast<double>(total);
    const double p =
        static_cast<double>(rebalancer_count) /
        static_cast<double>(total);

    return 1.0 - (r * r + m * m + p * p);
}

void sample_prices(
    const SimulationSnapshot& snapshot,
    std::map<int, PriceStats>& stats,
    double& spread_sum,
    double& spread_max,
    double& relative_spread_sum,
    double& relative_spread_max,
    int& spread_samples
)
{
    if (snapshot.instrument_ids.empty())
    {
        return;
    }
    double minimum =
        std::numeric_limits<double>::max();
    double maximum =
        std::numeric_limits<double>::lowest();
    double total_price = 0.0;
    for (int id : snapshot.instrument_ids)
    {
        double price =
            snapshot.instrument_reference_price.at(id).current_price;
        stats[id].add(price);
        minimum = std::min(minimum, price);
        maximum = std::max(maximum, price);
        total_price += price;
    }
    double spread = maximum - minimum;
    double mean_price =
        total_price /
        static_cast<double>(
            snapshot.instrument_ids.size()
        );
    double relative_spread = 0.0;
    if (mean_price > 0.0)
    {
        relative_spread = spread / mean_price;
    }
    spread_sum += spread;
    spread_max = std::max(spread_max, spread);
    relative_spread_sum += relative_spread;
    relative_spread_max =
        std::max(relative_spread_max, relative_spread);
    ++spread_samples;
}

void validate_experiment(const Experiment& e)
{
    if (e.ticks <= 0)
    {
        throw std::logic_error("ticks must be positive");
    }
    if (e.sample_every <= 0)
    {
        throw std::logic_error("sample_every must be positive");
    }
    if (
        e.random_count < 0 ||
        e.mean_count < 0 ||
        e.rebalancer_count < 0 ||
        e.neural_count < 0
    )
    {
        throw std::logic_error("trader counts cannot be negative");
    }

    const std::int64_t trader_count =
        static_cast<std::int64_t>(e.random_count) +
        static_cast<std::int64_t>(e.mean_count) +
        static_cast<std::int64_t>(e.rebalancer_count) +
        static_cast<std::int64_t>(e.neural_count);

    if (trader_count <= 0)
    {
        throw std::logic_error("at least one trader is required");
    }
    if (e.mean_memory <= 1)
    {
        throw std::logic_error("mean-value memory must be greater than 1");
    }
    if (e.instrument_count <= 0)
    {
        throw std::logic_error("instrument_count must be positive");
    }
    if (e.shares_per_instrument <= 0)
    {
        throw std::logic_error("shares_per_instrument must be positive");
    }
    if (e.starting_price <= 0)
    {
        throw std::logic_error("starting_price must be positive");
    }
    if (e.starting_cash <= 0)
    {
        throw std::logic_error("starting_cash must be positive");
    }
    if (e.initial_bank_cash <= 0)
    {
        throw std::logic_error("initial_bank_cash must be positive");
    }
    if (e.initial_bank_cash > std::numeric_limits<int>::max())
    {
        throw std::logic_error(
            "initial_bank_cash exceeds Simulation constructor int range"
        );
    }
    if (e.initial_bank_cash < trader_count * e.starting_cash)
    {
        throw std::logic_error(
            "initial bank cash is insufficient to fund the initial population"
        );
    }
    if (e.cost_frequency <= 0)
    {
        throw std::logic_error("cost_frequency must be positive");
    }
    if (e.cost_amount < 0)
    {
        throw std::logic_error("cost_amount cannot be negative");
    }
    if (e.tax_enabled)
    {
        if (e.tax_season_length <= 0)
        {
            throw std::logic_error("tax_season_length must be positive");
        }
        if (
            e.tax_window_ticks <= 0 ||
            e.tax_window_ticks > e.tax_season_length
        )
        {
            throw std::logic_error(
                "tax_window_ticks must be in [1, tax_season_length]"
            );
        }
    }
    if (e.bank_recycle_target < 0)
    {
        throw std::logic_error("bank_recycle_target cannot be negative");
    }
    if (e.bank_recycle_frequency <= 0)
    {
        throw std::logic_error("bank_recycle_frequency must be positive");
    }
    if (
        e.bank_recycle_fraction < 0.0 ||
        e.bank_recycle_fraction > 1.0
    )
    {
        throw std::logic_error(
            "bank_recycle_fraction must be between 0 and 1"
        );
    }
}

RunResult run_experiment(
    const Experiment& e,
    const ProgressCallback& progress_callback
)
{
    validate_experiment(e);

    auto start_time =
        std::chrono::steady_clock::now();

    Simulation sim{
        e.seed,
        static_cast<int>(e.initial_bank_cash)
    };

    for (int id = 1; id <= e.instrument_count; ++id)
    {
        if (!sim.add_instrument(id, instrument_name(id), e.starting_price))
        {
            throw std::logic_error(
                "failed to add instrument " + std::to_string(id)
            );
        }
    }

    sim.set_starting_amount(e.starting_cash);
    sim.set_recurring_costs(e.cost_frequency, e.cost_amount);
    sim.set_tax_frequency(e.tax_enabled, e.tax_season_length);
    sim.set_living_parent_selection(e.living_parent_selection);

    // This setting is stored by Simulation and applied only to mean-value
    // traders, including replacement traders created later in the run.
    sim.set_trader_memory_length(e.mean_memory);

    sim.set_bank_recycling(
        e.bank_recycle_target,
        e.bank_recycle_frequency,
        e.bank_recycle_fraction
    );

    for (int id = 1; id <= e.instrument_count; ++id)
    {
        if (!sim.introduce_holdings(id, e.shares_per_instrument))
        {
            throw std::logic_error(
                "failed to introduce holdings for instrument " +
                std::to_string(id)
            );
        }
    }

    auto add_initial_traders =
        [&](int count, TraderType type, const char* label)
        {
            for (int i = 0; i < count; ++i)
            {
                if (!sim.add_trader(e.starting_cash, type))
                {
                    std::ostringstream message;
                    message
                        << "failed to create initial " << label
                        << " trader " << (i + 1) << " / " << count;
                    throw std::logic_error(message.str());
                }
            }
        };

    add_initial_traders(e.random_count, TraderType::random, "random");
    add_initial_traders(e.mean_count, TraderType::mean_value, "mean-value");
    add_initial_traders(
        e.rebalancer_count,
        TraderType::portfolio_rebalancer,
        "rebalancer"
    );
    add_initial_traders(
        e.neural_count,
        TraderType::neural_evolution,
        "neural"
    );

    {
        const SimulationSnapshot startup = sim.get_snapshot();
        const int requested_population =
            e.random_count +
            e.mean_count +
            e.rebalancer_count +
            e.neural_count;

        if (
            startup.active_total_traders != requested_population ||
            startup.random != e.random_count ||
            startup.mean_reversion != e.mean_count ||
            startup.portfolio_rebalancer != e.rebalancer_count ||
            startup.neural_evolution != e.neural_count
        )
        {
            std::ostringstream message;
            message
                << "initial population mismatch: requested total="
                << requested_population
                << " R=" << e.random_count
                << " M=" << e.mean_count
                << " P=" << e.rebalancer_count
                << " N=" << e.neural_count
                << " but got total="
                << startup.active_total_traders
                << " R=" << startup.random
                << " M=" << startup.mean_reversion
                << " P=" << startup.portfolio_rebalancer
                << " N=" << startup.neural_evolution;
            throw std::logic_error(message.str());
        }
    }

    RunResult result;
    double spread_sum = 0.0;
    double spread_max = 0.0;
    double relative_spread_sum = 0.0;
    double relative_spread_max = 0.0;
    int spread_samples = 0;

    SimulationSnapshot initial = sim.get_snapshot();
    result.longest_neural_lifespan =
        initial.longest_neural_lifespan;

    sample_prices(
        initial,
        result.price_stats,
        spread_sum,
        spread_max,
        relative_spread_sum,
        relative_spread_max,
        spread_samples
    );

    int previous_trade_count = sim.get_total_trades();
    int trade_count_1000_ticks_before_end = previous_trade_count;
    double active_orders_sum = 0.0;
    int active_orders_samples = 0;
    auto last_progress_update = start_time;

    for (int tick = 1; tick <= e.ticks; ++tick)
    {
        sim.tick();

        const int current_trade_count = sim.get_total_trades();
        const int trades_this_tick =
            current_trade_count - previous_trade_count;

        if (current_trade_count > previous_trade_count)
        {
            result.last_trade_tick = tick;
            ++result.active_trade_ticks;
        }

        const TaxWindowPosition tax_window =
            get_tax_window_position(
                tick,
                e.ticks,
                e.tax_enabled,
                e.tax_season_length,
                e.tax_window_ticks
            );

        if (tax_window.phase == TaxWindowPhase::pre)
        {
            result.pre_tax.add_trades(trades_this_tick);
        }
        else if (tax_window.phase == TaxWindowPhase::post)
        {
            result.post_tax.add_trades(trades_this_tick);
        }

        if (tick == e.ticks - 1000)
        {
            trade_count_1000_ticks_before_end =
                current_trade_count;
        }

        previous_trade_count = current_trade_count;

        if (tick % e.sample_every == 0)
        {
            SimulationSnapshot snapshot = sim.get_snapshot();
            active_orders_sum += snapshot.active_orders;
            ++active_orders_samples;

            result.longest_neural_lifespan = std::max(
                result.longest_neural_lifespan,
                snapshot.longest_neural_lifespan
            );

            if (tax_window.phase == TaxWindowPhase::pre)
            {
                result.pre_tax.add_snapshot(
                    snapshot,
                    tax_window.event_tick
                );
            }
            else if (tax_window.phase == TaxWindowPhase::post)
            {
                result.post_tax.add_snapshot(
                    snapshot,
                    tax_window.event_tick
                );
            }

            sample_prices(
                snapshot,
                result.price_stats,
                spread_sum,
                spread_max,
                relative_spread_sum,
                relative_spread_max,
                spread_samples
            );
        }

        if (progress_callback)
        {
            const auto now = std::chrono::steady_clock::now();
            if (
                tick == e.ticks ||
                now - last_progress_update >= std::chrono::seconds(2)
            )
            {
                SimulationSnapshot progress_snapshot = sim.get_snapshot();
                result.longest_neural_lifespan = std::max(
                    result.longest_neural_lifespan,
                    progress_snapshot.longest_neural_lifespan
                );

                progress_callback(
                    tick,
                    progress_snapshot,
                    result.last_trade_tick
                );
                last_progress_update = now;
            }
        }
    }

    result.final_snapshot = sim.get_snapshot();

    if (
        e.tax_enabled &&
        e.ticks >= e.tax_season_length + e.tax_window_ticks - 1
    )
    {
        result.tax_events_observed =
            (e.ticks - e.tax_window_ticks + 1) /
            e.tax_season_length;
    }

    result.longest_neural_lifespan = std::max(
        result.longest_neural_lifespan,
        result.final_snapshot.longest_neural_lifespan
    );

    result.trades_last_1000_ticks =
        result.final_snapshot.total_trades -
        trade_count_1000_ticks_before_end;

    if (active_orders_samples > 0)
    {
        result.mean_active_orders =
            active_orders_sum /
            static_cast<double>(active_orders_samples);
    }

    if (e.ticks % e.sample_every != 0)
    {
        sample_prices(
            result.final_snapshot,
            result.price_stats,
            spread_sum,
            spread_max,
            relative_spread_sum,
            relative_spread_max,
            spread_samples
        );
    }

    if (spread_samples > 0)
    {
        result.mean_price_spread =
            spread_sum /
            static_cast<double>(spread_samples);
        result.mean_relative_spread =
            relative_spread_sum /
            static_cast<double>(spread_samples);
    }

    result.max_price_spread = spread_max;
    result.max_relative_spread = relative_spread_max;

    const auto end_time =
        std::chrono::steady_clock::now();
    result.runtime_seconds =
        std::chrono::duration<double>(
            end_time - start_time
        ).count();

    return result;
}

void write_header(
    std::ofstream& csv,
    int max_instrument_count
)
{
    csv
        << "experiment_name,"
        << "condition_id,"
        << "seed,"
        << "ticks_requested,"
        << "sample_every,"
        << "random_count,"
        << "mean_count,"
        << "rebalancer_count,"
        << "neural_count,"
        << "non_neural_ecology_diversity,"
        << "mean_memory,"
        << "instrument_count,"
        << "shares_per_instrument,"
        << "starting_price,"
        << "initial_asset_value_per_instrument,"
        << "total_initial_asset_value,"
        << "starting_cash,"
        << "initial_bank_cash,"
        << "cost_frequency,"
        << "cost_amount,"
        << "mean_cost_per_tick,"
        << "tax_enabled,"
        << "tax_season_length,"
        << "tax_window_ticks,"
        << "living_parent_selection,"
        << "bank_recycle_target,"
        << "bank_recycle_frequency,"
        << "bank_recycle_fraction,"
        << "tax_events_observed,"
        << "final_tick,"
        << "final_active_traders,"
        << "final_random_active,"
        << "final_mean_active,"
        << "final_rebalancer_active,"
        << "final_neural_active,"
        << "total_trades,"
        << "last_trade_tick,"
        << "active_trade_ticks,"
        << "trades_last_1000_ticks,"
        << "mean_active_orders,"
        << "active_orders,"
        << "total_deaths,"
        << "random_deaths,"
        << "mean_deaths,"
        << "rebalancer_deaths,"
        << "neural_deaths,"
        << "longest_neural_lifespan,"
        << "oldest_living_neural_id,"
        << "oldest_living_neural_age,"
        << "oldest_living_neural_cash,"
        << "oldest_living_neural_wealth,"
        << "oldest_to_mean_neural_wealth_ratio,"
        << "oldest_share_of_neural_wealth_percent,"
        << "oldest_max_single_instrument_supply_percent,"
        << "max_neural_instrument_ownership_percent,"
        << "mean_neural_instrument_ownership_percent,";

    for (int id = 1; id <= max_instrument_count; ++id)
    {
        csv
            << "oldest_living_neural_"
            << instrument_column_name(id)
            << ',';
    }

    csv
        << "bank_cash,"
        << "random_avg_cash,"
        << "random_avg_wealth,"
        << "random_cash_percent,"
        << "mean_avg_cash,"
        << "mean_avg_wealth,"
        << "mean_cash_percent,"
        << "rebalancer_avg_cash,"
        << "rebalancer_avg_wealth,"
        << "rebalancer_cash_percent,"
        << "neural_avg_cash,"
        << "neural_avg_wealth,"
        << "neural_cash_percent,"
        << "mean_price_spread,"
        << "max_price_spread,"
        << "mean_relative_spread,"
        << "max_relative_spread,"
        << "pre_tax_ticks,"
        << "post_tax_ticks,"
        << "pre_tax_trades,"
        << "post_tax_trades,"
        << "pre_tax_mean_trades_per_tick,"
        << "post_tax_mean_trades_per_tick,"
        << "pre_tax_mean_neural_cash_percent,"
        << "post_tax_mean_neural_cash_percent,"
        << "pre_tax_mean_neural_ownership_percent,"
        << "post_tax_mean_neural_ownership_percent,"
        << "pre_tax_mean_sampled_return,"
        << "post_tax_mean_sampled_return,"
        << "pre_tax_mean_abs_sampled_return,"
        << "post_tax_mean_abs_sampled_return,"
        << "pre_tax_mean_100t_volatility,"
        << "post_tax_mean_100t_volatility,"
        << "pre_tax_snapshot_samples,"
        << "post_tax_snapshot_samples,";

    for (int id = 1; id <= max_instrument_count; ++id)
    {
        const std::string name = instrument_column_name(id);
        csv
            << name << "_final_price,"
            << name << "_mean_price,"
            << name << "_min_price,"
            << name << "_max_price,"
            << name << "_return_volatility,"
            << name << "_sampled_mean_absolute_return,"
            << name << "_sampled_price_change_fraction,"
            << name << "_trades,"
            << name << "_random_ownership,"
            << name << "_mean_ownership,"
            << name << "_rebalancer_ownership,"
            << name << "_neural_ownership,"
            << name << "_bank_ownership,";
    }

    csv << "runtime_seconds\n";
}

void write_result(
    std::ofstream& csv,
    const Experiment& e,
    const RunResult& result,
    int max_instrument_count
)
{
    const SimulationSnapshot& snapshot = result.final_snapshot;

    OldTraderSnapshot oldest_living;
    if (!snapshot.oldest_neural_traders.empty())
    {
        oldest_living = snapshot.oldest_neural_traders.front();
    }

    auto holding_or_zero =
        [&](int instrument_id)
        {
            auto it = oldest_living.holdings.find(instrument_id);
            return it != oldest_living.holdings.end()
                ? it->second
                : 0;
        };

    const std::int64_t initial_asset_value_per_instrument =
        static_cast<std::int64_t>(e.shares_per_instrument) *
        static_cast<std::int64_t>(e.starting_price);

    const std::int64_t total_initial_asset_value =
        initial_asset_value_per_instrument *
        static_cast<std::int64_t>(e.instrument_count);

    double oldest_to_mean_neural_wealth_ratio = 0.0;
    if (
        oldest_living.trader_id != 0 &&
        snapshot.neural_evolution_portfolio_value > 0
    )
    {
        oldest_to_mean_neural_wealth_ratio =
            static_cast<double>(oldest_living.wealth) /
            static_cast<double>(snapshot.neural_evolution_portfolio_value);
    }

    double oldest_share_of_neural_wealth_percent = 0.0;
    if (
        oldest_living.trader_id != 0 &&
        snapshot.neural_evolution > 0 &&
        snapshot.neural_evolution_portfolio_value > 0
    )
    {
        const double estimated_total_neural_wealth =
            static_cast<double>(snapshot.neural_evolution_portfolio_value) *
            static_cast<double>(snapshot.neural_evolution);

        oldest_share_of_neural_wealth_percent =
            100.0 *
            static_cast<double>(oldest_living.wealth) /
            estimated_total_neural_wealth;
    }

    double oldest_max_single_instrument_supply_percent = 0.0;
    for (int id : snapshot.instrument_ids)
    {
        oldest_max_single_instrument_supply_percent =
            std::max(
                oldest_max_single_instrument_supply_percent,
                100.0 *
                static_cast<double>(holding_or_zero(id)) /
                static_cast<double>(e.shares_per_instrument)
            );
    }

    double max_neural_instrument_ownership_percent = 0.0;
    double mean_neural_instrument_ownership_percent = 0.0;
    int neural_ownership_samples = 0;

    for (int id : snapshot.instrument_ids)
    {
        auto it =
            snapshot.neural_evolution_percentage_of_each_instrument.find(id);

        if (
            it ==
            snapshot.neural_evolution_percentage_of_each_instrument.end()
        )
        {
            continue;
        }

        const double ownership = static_cast<double>(it->second);
        max_neural_instrument_ownership_percent =
            std::max(
                max_neural_instrument_ownership_percent,
                ownership
            );
        mean_neural_instrument_ownership_percent += ownership;
        ++neural_ownership_samples;
    }

    if (neural_ownership_samples > 0)
    {
        mean_neural_instrument_ownership_percent /=
            static_cast<double>(neural_ownership_samples);
    }

    csv
        << e.experiment_name << ','
        << e.condition_id << ','
        << e.seed << ','
        << e.ticks << ','
        << e.sample_every << ','
        << e.random_count << ','
        << e.mean_count << ','
        << e.rebalancer_count << ','
        << e.neural_count << ','
        << ecology_diversity(
            e.random_count,
            e.mean_count,
            e.rebalancer_count
        ) << ','
        << e.mean_memory << ','
        << e.instrument_count << ','
        << e.shares_per_instrument << ','
        << e.starting_price << ','
        << initial_asset_value_per_instrument << ','
        << total_initial_asset_value << ','
        << e.starting_cash << ','
        << e.initial_bank_cash << ','
        << e.cost_frequency << ','
        << e.cost_amount << ','
        << (static_cast<double>(e.cost_amount) /
            static_cast<double>(e.cost_frequency)) << ','
        << (e.tax_enabled ? 1 : 0) << ','
        << e.tax_season_length << ','
        << e.tax_window_ticks << ','
        << (e.living_parent_selection ? 1 : 0) << ','
        << e.bank_recycle_target << ','
        << e.bank_recycle_frequency << ','
        << e.bank_recycle_fraction << ','
        << result.tax_events_observed << ','
        << snapshot.tick << ','
        << snapshot.active_total_traders << ','
        << snapshot.random << ','
        << snapshot.mean_reversion << ','
        << snapshot.portfolio_rebalancer << ','
        << snapshot.neural_evolution << ','
        << snapshot.total_trades << ','
        << result.last_trade_tick << ','
        << result.active_trade_ticks << ','
        << result.trades_last_1000_ticks << ','
        << result.mean_active_orders << ','
        << snapshot.active_orders << ','
        << snapshot.total_deaths << ','
        << snapshot.random_deaths << ','
        << snapshot.mean_reversion_deaths << ','
        << snapshot.portfolio_rebalancer_deaths << ','
        << snapshot.neural_evolution_deaths << ','
        << result.longest_neural_lifespan << ','
        << oldest_living.trader_id << ','
        << oldest_living.age << ','
        << oldest_living.cash << ','
        << oldest_living.wealth << ','
        << oldest_to_mean_neural_wealth_ratio << ','
        << oldest_share_of_neural_wealth_percent << ','
        << oldest_max_single_instrument_supply_percent << ','
        << max_neural_instrument_ownership_percent << ','
        << mean_neural_instrument_ownership_percent << ',';

    for (int id = 1; id <= max_instrument_count; ++id)
    {
        if (id <= e.instrument_count)
        {
            csv << holding_or_zero(id);
        }
        csv << ',';
    }

    csv
        << snapshot.bank_cash << ','
        << snapshot.random_cash << ','
        << snapshot.random_portfolio_value << ','
        << snapshot.random_cash_fraction << ','
        << snapshot.mean_reversion_cash << ','
        << snapshot.mean_reversion_portfolio_value << ','
        << snapshot.mean_reversion_cash_fraction << ','
        << snapshot.portfolio_rebalancer_cash << ','
        << snapshot.portfolio_rebalancer_portfolio_value << ','
        << snapshot.portfolio_rebalancer_cash_fraction << ','
        << snapshot.neural_evolution_cash << ','
        << snapshot.neural_evolution_portfolio_value << ','
        << snapshot.neural_evolution_cash_fraction << ','
        << result.mean_price_spread << ','
        << result.max_price_spread << ','
        << result.mean_relative_spread << ','
        << result.max_relative_spread << ','
        << result.pre_tax.ticks << ','
        << result.post_tax.ticks << ','
        << result.pre_tax.trades << ','
        << result.post_tax.trades << ','
        << result.pre_tax.mean_trades_per_tick() << ','
        << result.post_tax.mean_trades_per_tick() << ','
        << result.pre_tax.mean_neural_cash_fraction() << ','
        << result.post_tax.mean_neural_cash_fraction() << ','
        << result.pre_tax.mean_neural_ownership() << ','
        << result.post_tax.mean_neural_ownership() << ','
        << result.pre_tax.mean_return() << ','
        << result.post_tax.mean_return() << ','
        << result.pre_tax.mean_absolute_return() << ','
        << result.post_tax.mean_absolute_return() << ','
        << result.pre_tax.mean_instrument_volatility() << ','
        << result.post_tax.mean_instrument_volatility() << ','
        << result.pre_tax.snapshot_samples << ','
        << result.post_tax.snapshot_samples << ',';

    constexpr int instrument_metric_columns = 13;

    for (int id = 1; id <= max_instrument_count; ++id)
    {
        if (id > e.instrument_count)
        {
            for (int column = 0; column < instrument_metric_columns; ++column)
            {
                csv << ',';
            }
            continue;
        }

        const PriceStats& stats = result.price_stats.at(id);

        int trades = 0;
        auto trade_it = snapshot.total_trades_per_instrument.find(id);
        if (trade_it != snapshot.total_trades_per_instrument.end())
        {
            trades = trade_it->second;
        }

        csv
            << snapshot.instrument_reference_price.at(id).current_price << ','
            << stats.mean() << ','
            << stats.minimum << ','
            << stats.maximum << ','
            << stats.return_volatility() << ','
            << stats.mean_absolute_return() << ','
            << stats.price_change_fraction() << ','
            << trades << ','
            << percentage_or_zero(
                snapshot.random_percentage_of_each_instrument,
                id
            ) << ','
            << percentage_or_zero(
                snapshot.mean_reversion_percentage_of_each_instrument,
                id
            ) << ','
            << percentage_or_zero(
                snapshot.portfolio_rebalancer_percentage_of_each_instrument,
                id
            ) << ','
            << percentage_or_zero(
                snapshot.neural_evolution_percentage_of_each_instrument,
                id
            ) << ','
            << percentage_or_zero(
                snapshot.bank_percentage_of_each_instrument,
                id
            ) << ',';
    }

    csv << result.runtime_seconds << '\n';
    csv.flush();
}

std::string format_duration(double seconds)
{
    int total_seconds =
        static_cast<int>(seconds);
    int hours = total_seconds / 3600;
    int minutes =
        (total_seconds % 3600) / 60;
    int secs =
        total_seconds % 60;
    std::ostringstream output;
    output
        << std::setfill('0')
        << std::setw(2) << hours << ':'
        << std::setw(2) << minutes << ':'
        << std::setw(2) << secs;
    return output.str();
}

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split(
    const std::string& value,
    char delimiter
)
{
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter))
    {
        parts.push_back(trim(part));
    }
    return parts;
}

std::string lower_copy(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );
    return value;
}

template <typename T>
T parse_integer(const std::string& text)
{
    static_assert(std::is_integral_v<T>);

    std::size_t consumed = 0;

    if constexpr (std::is_unsigned_v<T>)
    {
        const unsigned long long value = std::stoull(text, &consumed);
        if (consumed != text.size())
        {
            throw std::invalid_argument("invalid integer: " + text);
        }
        if (value > std::numeric_limits<T>::max())
        {
            throw std::out_of_range("integer out of range: " + text);
        }
        return static_cast<T>(value);
    }
    else
    {
        const long long value = std::stoll(text, &consumed);
        if (consumed != text.size())
        {
            throw std::invalid_argument("invalid integer: " + text);
        }
        if (
            value < std::numeric_limits<T>::min() ||
            value > std::numeric_limits<T>::max()
        )
        {
            throw std::out_of_range("integer out of range: " + text);
        }
        return static_cast<T>(value);
    }
}

template <typename T>
std::vector<T> parse_integer_list(const std::string& text)
{
    static_assert(std::is_integral_v<T>);

    std::vector<T> values;

    for (const std::string& raw_token : split(text, ','))
    {
        const std::string token = trim(raw_token);
        if (token.empty())
        {
            continue;
        }

        const auto range = split(token, ':');
        if (range.size() == 1)
        {
            values.push_back(parse_integer<T>(range[0]));
            continue;
        }

        if (range.size() != 2 && range.size() != 3)
        {
            throw std::invalid_argument(
                "invalid range syntax: " + token
            );
        }

        if constexpr (std::is_unsigned_v<T>)
        {
            const T start = parse_integer<T>(range[0]);
            const T end = parse_integer<T>(range[1]);
            const T step = range.size() == 3
                ? parse_integer<T>(range[2])
                : static_cast<T>(1);

            if (step == 0)
            {
                throw std::invalid_argument("range step cannot be zero");
            }
            if (start > end)
            {
                throw std::invalid_argument(
                    "descending ranges are not supported for unsigned values"
                );
            }

            for (T value = start; value <= end;)
            {
                values.push_back(value);
                if (end - value < step)
                {
                    break;
                }
                value += step;
            }
        }
        else
        {
            const T start = parse_integer<T>(range[0]);
            const T end = parse_integer<T>(range[1]);
            T step = range.size() == 3
                ? parse_integer<T>(range[2])
                : static_cast<T>(start <= end ? 1 : -1);

            if (step == 0)
            {
                throw std::invalid_argument("range step cannot be zero");
            }
            if ((start < end && step < 0) || (start > end && step > 0))
            {
                throw std::invalid_argument(
                    "range step points away from the end value"
                );
            }

            if (step > 0)
            {
                for (T value = start; value <= end; value += step)
                {
                    values.push_back(value);
                    if (end - value < step)
                    {
                        break;
                    }
                }
            }
            else
            {
                for (T value = start; value >= end; value += step)
                {
                    values.push_back(value);
                    if (value - end < -step)
                    {
                        break;
                    }
                }
            }
        }
    }

    if (values.empty())
    {
        throw std::invalid_argument("list cannot be empty");
    }

    return values;
}

std::vector<double> parse_double_list(const std::string& text)
{
    std::vector<double> values;
    for (const std::string& token : split(text, ','))
    {
        if (token.empty())
        {
            continue;
        }
        std::size_t consumed = 0;
        const double value = std::stod(token, &consumed);
        if (consumed != token.size())
        {
            throw std::invalid_argument("invalid number: " + token);
        }
        values.push_back(value);
    }
    if (values.empty())
    {
        throw std::invalid_argument("list cannot be empty");
    }
    return values;
}

bool parse_bool(const std::string& text)
{
    const std::string value = lower_copy(trim(text));
    if (
        value == "true" || value == "1" ||
        value == "yes" || value == "y" || value == "on"
    )
    {
        return true;
    }
    if (
        value == "false" || value == "0" ||
        value == "no" || value == "n" || value == "off"
    )
    {
        return false;
    }
    throw std::invalid_argument("invalid boolean: " + text);
}

std::vector<bool> parse_bool_list(const std::string& text)
{
    std::vector<bool> values;
    for (const std::string& token : split(text, ','))
    {
        if (!token.empty())
        {
            values.push_back(parse_bool(token));
        }
    }
    if (values.empty())
    {
        throw std::invalid_argument("boolean list cannot be empty");
    }
    return values;
}

template <typename T>
std::string join_values(const std::vector<T>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        out << values[i];
    }
    return out.str();
}

template <>
std::string join_values<bool>(const std::vector<bool>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        out << (values[i] ? "true" : "false");
    }
    return out.str();
}

std::string prompt_value(
    const std::string& label,
    const std::string& default_value
)
{
    std::cout
        << label
        << " [" << default_value << "]:\n> ";

    std::string input;
    std::getline(std::cin, input);
    input = trim(input);
    return input.empty() ? default_value : input;
}

bool any_positive(const std::vector<int>& values)
{
    return std::any_of(
        values.begin(),
        values.end(),
        [](int value) { return value > 0; }
    );
}

bool any_true(const std::vector<bool>& values)
{
    return std::any_of(
        values.begin(),
        values.end(),
        [](bool value) { return value; }
    );
}

void apply_setting(
    SweepConfig& config,
    const std::string& raw_key,
    const std::string& raw_value
)
{
    const std::string key = lower_copy(trim(raw_key));
    const std::string value = trim(raw_value);

    if (key == "name")
    {
        config.name = value;
    }
    else if (key == "seeds")
    {
        config.seeds = parse_integer_list<std::uint64_t>(value);
    }
    else if (key == "ticks")
    {
        config.ticks = parse_integer_list<int>(value);
    }
    else if (key == "sample_every")
    {
        config.sample_every = parse_integer_list<int>(value);
    }
    else if (key == "random")
    {
        config.random_counts = parse_integer_list<int>(value);
    }
    else if (key == "mean")
    {
        config.mean_counts = parse_integer_list<int>(value);
    }
    else if (key == "rebalancer")
    {
        config.rebalancer_counts = parse_integer_list<int>(value);
    }
    else if (key == "neural")
    {
        config.neural_counts = parse_integer_list<int>(value);
    }
    else if (key == "mean_memory")
    {
        config.mean_memories = parse_integer_list<int>(value);
    }
    else if (key == "instrument_count" || key == "instruments")
    {
        config.instrument_counts = parse_integer_list<int>(value);
    }
    else if (key == "shares")
    {
        config.shares_per_instrument = parse_integer_list<int>(value);
    }
    else if (key == "starting_price")
    {
        config.starting_prices = parse_integer_list<int>(value);
    }
    else if (key == "initial_bank_cash")
    {
        config.initial_bank_cash =
            parse_integer_list<std::int64_t>(value);
    }
    else if (key == "starting_cash")
    {
        config.starting_cash =
            parse_integer_list<std::int64_t>(value);
    }
    else if (key == "cost_frequency")
    {
        config.cost_frequencies = parse_integer_list<int>(value);
    }
    else if (key == "cost_amount")
    {
        config.cost_amounts =
            parse_integer_list<std::int64_t>(value);
    }
    else if (key == "tax_enabled")
    {
        config.tax_enabled = parse_bool_list(value);
    }
    else if (key == "tax_season")
    {
        config.tax_season_lengths = parse_integer_list<int>(value);
    }
    else if (key == "tax_window")
    {
        config.tax_window_ticks = parse_integer_list<int>(value);
    }
    else if (key == "living_parent_selection")
    {
        config.living_parent_selection = parse_bool_list(value);
    }
    else if (key == "bank_recycle_target")
    {
        config.bank_recycle_targets =
            parse_integer_list<std::int64_t>(value);
    }
    else if (key == "bank_recycle_frequency")
    {
        config.bank_recycle_frequencies =
            parse_integer_list<int>(value);
    }
    else if (key == "bank_recycle_fraction")
    {
        config.bank_recycle_fractions = parse_double_list(value);
    }
    else if (key == "constraint")
    {
        if (!value.empty())
        {
            config.constraints.push_back(value);
        }
    }
    else if (key == "constraints")
    {
        for (const std::string& constraint : split(value, ';'))
        {
            if (!constraint.empty())
            {
                config.constraints.push_back(constraint);
            }
        }
    }
    else if (key == "worker_threads")
    {
        const std::string lowered = lower_copy(value);
        if (lowered == "auto" || lowered == "0")
        {
            config.worker_threads = 0;
        }
        else
        {
            config.worker_threads = parse_integer<unsigned int>(value);
        }
    }
    else
    {
        throw std::invalid_argument("unknown config key: " + raw_key);
    }
}

SweepConfig read_config_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error(
            "could not open config file: " + path.string()
        );
    }

    SweepConfig config;
    std::string line;
    int line_number = 0;

    while (std::getline(input, line))
    {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos)
        {
            throw std::runtime_error(
                "invalid config line " +
                std::to_string(line_number) +
                ": expected key=value"
            );
        }

        try
        {
            apply_setting(
                config,
                line.substr(0, equals),
                line.substr(equals + 1)
            );
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(
                "config line " +
                std::to_string(line_number) +
                ": " + error.what()
            );
        }
    }

    return config;
}

SweepConfig read_interactive_config()
{
    SweepConfig config;

    std::cout
        << "Market Terrarium Experiment Runner\n"
        << "==================================\n\n"
        << "Lists use commas. Integer ranges use start:end[:step].\n"
        << "Examples: 20,50,100 or 1001:1010 or 0:160:20.\n"
        << "Press Enter to keep the displayed default.\n\n";

    config.name = prompt_value("Experiment name", config.name);

    config.ticks = parse_integer_list<int>(
        prompt_value("Ticks per run", join_values(config.ticks))
    );
    config.seeds = parse_integer_list<std::uint64_t>(
        prompt_value("Seeds", join_values(config.seeds))
    );

    config.random_counts = parse_integer_list<int>(
        prompt_value("Random traders", join_values(config.random_counts))
    );
    config.mean_counts = parse_integer_list<int>(
        prompt_value("Mean-value traders", join_values(config.mean_counts))
    );
    config.rebalancer_counts = parse_integer_list<int>(
        prompt_value("Rebalancers", join_values(config.rebalancer_counts))
    );
    config.neural_counts = parse_integer_list<int>(
        prompt_value("Neural traders", join_values(config.neural_counts))
    );

    if (any_positive(config.mean_counts))
    {
        config.mean_memories = parse_integer_list<int>(
            prompt_value(
                "Mean-value memory",
                join_values(config.mean_memories)
            )
        );
    }
    else
    {
        config.mean_memories = {20};
    }

    config.instrument_counts = parse_integer_list<int>(
        prompt_value(
            "Number of instruments",
            join_values(config.instrument_counts)
        )
    );

    config.shares_per_instrument = parse_integer_list<int>(
        prompt_value(
            "Shares per instrument",
            join_values(config.shares_per_instrument)
        )
    );
    config.starting_prices = parse_integer_list<int>(
        prompt_value("Starting price", join_values(config.starting_prices))
    );

    config.starting_cash = parse_integer_list<std::int64_t>(
        prompt_value("Starting cash per trader", join_values(config.starting_cash))
    );
    config.initial_bank_cash = parse_integer_list<std::int64_t>(
        prompt_value("Initial bank cash", join_values(config.initial_bank_cash))
    );

    config.cost_amounts = parse_integer_list<std::int64_t>(
        prompt_value("Recurring cost amount", join_values(config.cost_amounts))
    );
    config.cost_frequencies = parse_integer_list<int>(
        prompt_value(
            "Recurring cost frequency",
            join_values(config.cost_frequencies)
        )
    );

    config.tax_enabled = parse_bool_list(
        prompt_value("Tax enabled", join_values(config.tax_enabled))
    );

    if (any_true(config.tax_enabled))
    {
        config.tax_season_lengths = parse_integer_list<int>(
            prompt_value(
                "Tax season length",
                join_values(config.tax_season_lengths)
            )
        );
        config.tax_window_ticks = parse_integer_list<int>(
            prompt_value(
                "Tax analysis window",
                join_values(config.tax_window_ticks)
            )
        );
    }

    config.living_parent_selection = parse_bool_list(
        prompt_value(
            "Living-parent selection",
            join_values(config.living_parent_selection)
        )
    );

    const bool advanced = parse_bool(
        prompt_value("Configure advanced runner settings", "false")
    );

    if (advanced)
    {
        config.sample_every = parse_integer_list<int>(
            prompt_value(
                "Snapshot sample interval",
                join_values(config.sample_every)
            )
        );
        config.bank_recycle_targets = parse_integer_list<std::int64_t>(
            prompt_value(
                "Bank recycle target",
                join_values(config.bank_recycle_targets)
            )
        );
        config.bank_recycle_frequencies = parse_integer_list<int>(
            prompt_value(
                "Bank recycle frequency",
                join_values(config.bank_recycle_frequencies)
            )
        );
        config.bank_recycle_fractions = parse_double_list(
            prompt_value(
                "Bank recycle fraction",
                join_values(config.bank_recycle_fractions)
            )
        );

        const std::string workers = prompt_value(
            "Worker threads (auto or integer)",
            config.worker_threads == 0
                ? "auto"
                : std::to_string(config.worker_threads)
        );
        if (lower_copy(workers) == "auto" || workers == "0")
        {
            config.worker_threads = 0;
        }
        else
        {
            config.worker_threads =
                parse_integer<unsigned int>(workers);
        }

        const std::string constraint_text = prompt_value(
            "Constraints (semicolon-separated; blank for none)",
            ""
        );

        for (const std::string& constraint : split(constraint_text, ';'))
        {
            if (!constraint.empty())
            {
                config.constraints.push_back(constraint);
            }
        }
    }

    return config;
}

double constraint_variable(
    const Experiment& e,
    const std::string& raw_name
)
{
    const std::string name = lower_copy(raw_name);

    if (name == "seed") return static_cast<double>(e.seed);
    if (name == "ticks") return e.ticks;
    if (name == "sample_every") return e.sample_every;

    if (name == "random") return e.random_count;
    if (name == "mean") return e.mean_count;
    if (name == "rebalancer") return e.rebalancer_count;
    if (name == "neural") return e.neural_count;
    if (name == "total_traders")
    {
        return
            e.random_count +
            e.mean_count +
            e.rebalancer_count +
            e.neural_count;
    }

    if (name == "mean_memory") return e.mean_memory;
    if (name == "instrument_count" || name == "instruments")
    {
        return e.instrument_count;
    }
    if (name == "shares") return e.shares_per_instrument;
    if (name == "starting_price") return e.starting_price;

    if (name == "starting_cash")
    {
        return static_cast<double>(e.starting_cash);
    }
    if (name == "initial_bank_cash")
    {
        return static_cast<double>(e.initial_bank_cash);
    }

    if (name == "cost_frequency") return e.cost_frequency;
    if (name == "cost_amount")
    {
        return static_cast<double>(e.cost_amount);
    }

    if (name == "tax_enabled") return e.tax_enabled ? 1.0 : 0.0;
    if (name == "tax_season") return e.tax_season_length;
    if (name == "tax_window") return e.tax_window_ticks;

    if (name == "living_parent_selection")
    {
        return e.living_parent_selection ? 1.0 : 0.0;
    }

    if (name == "bank_recycle_target")
    {
        return static_cast<double>(e.bank_recycle_target);
    }
    if (name == "bank_recycle_frequency")
    {
        return e.bank_recycle_frequency;
    }
    if (name == "bank_recycle_fraction")
    {
        return e.bank_recycle_fraction;
    }

    if (name == "initial_asset_value_per_instrument")
    {
        return
            static_cast<double>(e.shares_per_instrument) *
            static_cast<double>(e.starting_price);
    }

    if (name == "total_initial_asset_value")
    {
        return
            static_cast<double>(e.instrument_count) *
            static_cast<double>(e.shares_per_instrument) *
            static_cast<double>(e.starting_price);
    }

    throw std::invalid_argument(
        "unknown constraint variable: " + raw_name
    );
}

class ConstraintParser
{
public:
    ConstraintParser(
        const std::string& expression,
        const Experiment& experiment
    )
        : text(expression),
          experiment(experiment)
    {
    }

    bool evaluate()
    {
        const double lhs = parse_expression();
        skip_spaces();

        const std::string op = parse_comparison_operator();
        if (op.empty())
        {
            throw std::invalid_argument(
                "constraint requires one comparison operator "
                "(==, !=, <=, >=, <, >)"
            );
        }

        const double rhs = parse_expression();
        skip_spaces();

        if (position != text.size())
        {
            throw std::invalid_argument(
                "unexpected text near: " + text.substr(position)
            );
        }

        const double scale = std::max(
            1.0,
            std::max(std::abs(lhs), std::abs(rhs))
        );
        const bool equal =
            std::abs(lhs - rhs) <= 1e-9 * scale;

        if (op == "==") return equal;
        if (op == "!=") return !equal;
        if (op == "<=") return lhs < rhs || equal;
        if (op == ">=") return lhs > rhs || equal;
        if (op == "<") return lhs < rhs && !equal;
        if (op == ">") return lhs > rhs && !equal;

        throw std::logic_error("unreachable comparison operator");
    }

private:
    const std::string& text;
    const Experiment& experiment;
    std::size_t position = 0;

    void skip_spaces()
    {
        while (
            position < text.size() &&
            std::isspace(
                static_cast<unsigned char>(text[position])
            )
        )
        {
            ++position;
        }
    }

    bool consume(char c)
    {
        skip_spaces();
        if (position < text.size() && text[position] == c)
        {
            ++position;
            return true;
        }
        return false;
    }

    double parse_expression()
    {
        double value = parse_term();

        while (true)
        {
            if (consume('+'))
            {
                value += parse_term();
            }
            else if (consume('-'))
            {
                value -= parse_term();
            }
            else
            {
                break;
            }
        }

        return value;
    }

    double parse_term()
    {
        double value = parse_factor();

        while (true)
        {
            if (consume('*'))
            {
                value *= parse_factor();
            }
            else if (consume('/'))
            {
                const double divisor = parse_factor();
                if (divisor == 0.0)
                {
                    throw std::invalid_argument(
                        "division by zero in constraint"
                    );
                }
                value /= divisor;
            }
            else
            {
                break;
            }
        }

        return value;
    }

    double parse_factor()
    {
        skip_spaces();

        if (consume('+'))
        {
            return parse_factor();
        }

        if (consume('-'))
        {
            return -parse_factor();
        }

        if (consume('('))
        {
            const double value = parse_expression();
            if (!consume(')'))
            {
                throw std::invalid_argument(
                    "missing ')' in constraint"
                );
            }
            return value;
        }

        if (position >= text.size())
        {
            throw std::invalid_argument(
                "unexpected end of constraint"
            );
        }

        const unsigned char current =
            static_cast<unsigned char>(text[position]);

        if (std::isdigit(current) || text[position] == '.')
        {
            std::size_t consumed = 0;
            const double value =
                std::stod(text.substr(position), &consumed);
            position += consumed;
            return value;
        }

        if (std::isalpha(current) || text[position] == '_')
        {
            const std::size_t start = position;
            while (
                position < text.size() &&
                (
                    std::isalnum(
                        static_cast<unsigned char>(text[position])
                    ) ||
                    text[position] == '_'
                )
            )
            {
                ++position;
            }

            return constraint_variable(
                experiment,
                text.substr(start, position - start)
            );
        }

        throw std::invalid_argument(
            "unexpected character in constraint: " +
            std::string(1, text[position])
        );
    }

    std::string parse_comparison_operator()
    {
        skip_spaces();

        static const std::vector<std::string> operators{
            "==", "!=", "<=", ">=", "<", ">"
        };

        for (const std::string& op : operators)
        {
            if (text.compare(position, op.size(), op) == 0)
            {
                position += op.size();
                return op;
            }
        }

        return "";
    }
};

bool constraint_matches(
    const Experiment& experiment,
    const std::string& constraint
)
{
    try
    {
        return ConstraintParser(
            constraint,
            experiment
        ).evaluate();
    }
    catch (const std::exception& error)
    {
        throw std::invalid_argument(
            "invalid constraint '" + constraint + "': " +
            error.what()
        );
    }
}

template <typename T, typename Setter>
void expand_axis(
    std::vector<Experiment>& experiments,
    const std::vector<T>& values,
    Setter setter
)
{
    std::vector<Experiment> expanded;
    expanded.reserve(experiments.size() * values.size());

    for (const auto& experiment : experiments)
    {
        for (const auto& value : values)
        {
            Experiment next = experiment;
            setter(next, value);
            expanded.push_back(std::move(next));
        }
    }

    experiments = std::move(expanded);
}

std::vector<Experiment> build_experiments(const SweepConfig& config)
{
    std::vector<Experiment> experiments(1);
    experiments.front().experiment_name = config.name;

    expand_axis(
        experiments,
        config.seeds,
        [](Experiment& e, std::uint64_t value) { e.seed = value; }
    );
    expand_axis(
        experiments,
        config.ticks,
        [](Experiment& e, int value) { e.ticks = value; }
    );
    expand_axis(
        experiments,
        config.sample_every,
        [](Experiment& e, int value) { e.sample_every = value; }
    );
    expand_axis(
        experiments,
        config.random_counts,
        [](Experiment& e, int value) { e.random_count = value; }
    );
    expand_axis(
        experiments,
        config.mean_counts,
        [](Experiment& e, int value) { e.mean_count = value; }
    );
    expand_axis(
        experiments,
        config.rebalancer_counts,
        [](Experiment& e, int value) { e.rebalancer_count = value; }
    );
    expand_axis(
        experiments,
        config.neural_counts,
        [](Experiment& e, int value) { e.neural_count = value; }
    );
    expand_axis(
        experiments,
        config.mean_memories,
        [](Experiment& e, int value) { e.mean_memory = value; }
    );
    expand_axis(
        experiments,
        config.instrument_counts,
        [](Experiment& e, int value) { e.instrument_count = value; }
    );
    expand_axis(
        experiments,
        config.shares_per_instrument,
        [](Experiment& e, int value) { e.shares_per_instrument = value; }
    );
    expand_axis(
        experiments,
        config.starting_prices,
        [](Experiment& e, int value) { e.starting_price = value; }
    );
    expand_axis(
        experiments,
        config.initial_bank_cash,
        [](Experiment& e, std::int64_t value) { e.initial_bank_cash = value; }
    );
    expand_axis(
        experiments,
        config.starting_cash,
        [](Experiment& e, std::int64_t value) { e.starting_cash = value; }
    );
    expand_axis(
        experiments,
        config.cost_frequencies,
        [](Experiment& e, int value) { e.cost_frequency = value; }
    );
    expand_axis(
        experiments,
        config.cost_amounts,
        [](Experiment& e, std::int64_t value) { e.cost_amount = value; }
    );
    expand_axis(
        experiments,
        config.tax_enabled,
        [](Experiment& e, bool value) { e.tax_enabled = value; }
    );
    expand_axis(
        experiments,
        config.tax_season_lengths,
        [](Experiment& e, int value) { e.tax_season_length = value; }
    );
    expand_axis(
        experiments,
        config.tax_window_ticks,
        [](Experiment& e, int value) { e.tax_window_ticks = value; }
    );
    expand_axis(
        experiments,
        config.living_parent_selection,
        [](Experiment& e, bool value) { e.living_parent_selection = value; }
    );
    expand_axis(
        experiments,
        config.bank_recycle_targets,
        [](Experiment& e, std::int64_t value) { e.bank_recycle_target = value; }
    );
    expand_axis(
        experiments,
        config.bank_recycle_frequencies,
        [](Experiment& e, int value) { e.bank_recycle_frequency = value; }
    );
    expand_axis(
        experiments,
        config.bank_recycle_fractions,
        [](Experiment& e, double value) { e.bank_recycle_fraction = value; }
    );

    if (!config.constraints.empty())
    {
        std::vector<Experiment> filtered;
        filtered.reserve(experiments.size());

        for (const Experiment& experiment : experiments)
        {
            bool keep = true;

            for (const std::string& constraint : config.constraints)
            {
                if (!constraint_matches(experiment, constraint))
                {
                    keep = false;
                    break;
                }
            }

            if (keep)
            {
                filtered.push_back(experiment);
            }
        }

        experiments = std::move(filtered);
    }

    for (std::size_t i = 0; i < experiments.size(); ++i)
    {
        experiments[i].condition_id = i + 1;
        validate_experiment(experiments[i]);
    }

    return experiments;
}

std::string safe_filename(std::string value)
{
    if (value.empty())
    {
        return "experiment";
    }

    for (char& c : value)
    {
        const bool valid =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_';
        if (!valid)
        {
            c = '_';
        }
    }
    return value;
}

std::string timestamp_now()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time =
        std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);

    std::ostringstream timestamp;
    timestamp << std::put_time(local_time, "%Y-%m-%d_%H-%M-%S");
    return timestamp.str();
}

std::string command_output(const std::string& command)
{
    std::array<char, 256> buffer{};
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
    {
        result += buffer.data();
    }
    pclose(pipe);
    return trim(result);
}

std::string shell_quote(const std::string& value)
{
    std::string quoted = "'";
    for (char c : value)
    {
        if (c == '\'')
        {
            quoted += "'\\''";
        }
        else
        {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

std::string current_git_commit()
{
    const std::string root = MARKET_TERRARIUM_SOURCE_DIR;
    return command_output(
        "git -C " + shell_quote(root) +
        " rev-parse HEAD 2>/dev/null"
    );
}

bool git_is_dirty()
{
    const std::string root = MARKET_TERRARIUM_SOURCE_DIR;
    return !command_output(
        "git -C " + shell_quote(root) +
        " status --porcelain 2>/dev/null"
    ).empty();
}

void write_config_file(
    const SweepConfig& config,
    const std::filesystem::path& path,
    std::size_t total_runs
)
{
    // Capture repository state before creating the manifest itself, so an
    // unignored results/ directory does not make every run look dirty.
    const std::string commit = current_git_commit();
    const bool dirty = git_is_dirty();

    std::ofstream out(path);
    if (!out)
    {
        throw std::runtime_error(
            "could not write config file: " + path.string()
        );
    }

    out
        << "# Market Terrarium resolved sweep config\n"
        << "# total_runs=" << total_runs << '\n'
        << "# git_commit=" << (commit.empty() ? "unknown" : commit) << '\n'
        << "# git_dirty=" << (dirty ? "true" : "false") << '\n'
        << "name=" << config.name << '\n'
        << "ticks=" << join_values(config.ticks) << '\n'
        << "sample_every=" << join_values(config.sample_every) << '\n'
        << "seeds=" << join_values(config.seeds) << '\n'
        << "random=" << join_values(config.random_counts) << '\n'
        << "mean=" << join_values(config.mean_counts) << '\n'
        << "rebalancer=" << join_values(config.rebalancer_counts) << '\n'
        << "neural=" << join_values(config.neural_counts) << '\n'
        << "mean_memory=" << join_values(config.mean_memories) << '\n'
        << "instrument_count=" << join_values(config.instrument_counts) << '\n'
        << "shares=" << join_values(config.shares_per_instrument) << '\n'
        << "starting_price=" << join_values(config.starting_prices) << '\n'
        << "starting_cash=" << join_values(config.starting_cash) << '\n'
        << "initial_bank_cash=" << join_values(config.initial_bank_cash) << '\n'
        << "cost_frequency=" << join_values(config.cost_frequencies) << '\n'
        << "cost_amount=" << join_values(config.cost_amounts) << '\n'
        << "tax_enabled=" << join_values(config.tax_enabled) << '\n'
        << "tax_season=" << join_values(config.tax_season_lengths) << '\n'
        << "tax_window=" << join_values(config.tax_window_ticks) << '\n'
        << "living_parent_selection="
        << join_values(config.living_parent_selection) << '\n'
        << "bank_recycle_target="
        << join_values(config.bank_recycle_targets) << '\n'
        << "bank_recycle_frequency="
        << join_values(config.bank_recycle_frequencies) << '\n'
        << "bank_recycle_fraction="
        << join_values(config.bank_recycle_fractions) << '\n'
        << "worker_threads="
        << (config.worker_threads == 0
            ? std::string("auto")
            : std::to_string(config.worker_threads))
        << '\n';

    for (const std::string& constraint : config.constraints)
    {
        out << "constraint=" << constraint << '\n';
    }
}

unsigned int resolve_worker_count(
    const SweepConfig& config,
    std::size_t total_runs
)
{
    unsigned int workers = config.worker_threads;

    if (workers == 0)
    {
        unsigned int hardware_threads = std::thread::hardware_concurrency();
        if (hardware_threads == 0)
        {
            hardware_threads = 4;
        }
        workers = hardware_threads > 2
            ? hardware_threads - 2
            : 1;
    }

    workers = std::max(1u, workers);
    workers = std::min(
        workers,
        static_cast<unsigned int>(total_runs)
    );
    return workers;
}

void print_summary(
    const SweepConfig& config,
    std::size_t total_runs,
    unsigned int workers
)
{
    std::cout
        << "\nExperiment: " << config.name << '\n'
        << "Total runs: " << total_runs << '\n'
        << "Worker threads: " << workers << '\n'
        << "Seeds: " << config.seeds.size() << '\n'
        << "Ticks values: " << config.ticks.size() << '\n'
        << "Random-count values: " << config.random_counts.size() << '\n'
        << "Mean-count values: " << config.mean_counts.size() << '\n'
        << "Rebalancer-count values: "
        << config.rebalancer_counts.size() << '\n'
        << "Neural-count values: " << config.neural_counts.size() << '\n'
        << "Mean-memory values: " << config.mean_memories.size() << '\n'
        << "Instrument-count values: "
        << config.instrument_counts.size() << '\n'
        << "Share-supply values: "
        << config.shares_per_instrument.size() << '\n'
        << "Starting-price values: "
        << config.starting_prices.size() << '\n'
        << "Constraints: " << config.constraints.size() << '\n'
        << '\n';
}

void write_progress(
    const std::filesystem::path& progress_path,
    std::size_t completed,
    std::size_t total,
    std::size_t failed,
    std::chrono::steady_clock::time_point start_time,
    const std::vector<LiveRunState>& live_states,
    const LiveRunState* latest_completed,
    const std::string& status,
    const std::filesystem::path& output_path,
    const std::filesystem::path& config_path
)
{
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - start_time).count();

    double effective_completed = static_cast<double>(completed);
    for (const auto& state : live_states)
    {
        if (state.active && state.total_ticks > 0)
        {
            effective_completed +=
                static_cast<double>(state.tick) /
                static_cast<double>(state.total_ticks);
        }
    }

    double runs_per_second = 0.0;
    double eta_seconds = 0.0;
    if (elapsed > 0.0 && effective_completed > 0.0)
    {
        runs_per_second = effective_completed / elapsed;
        if (static_cast<double>(total) > effective_completed)
        {
            eta_seconds =
                (static_cast<double>(total) - effective_completed) /
                runs_per_second;
        }
    }

    auto temporary_path = progress_path;
    temporary_path += ".tmp";
    std::ofstream progress(temporary_path);
    if (!progress)
    {
        return;
    }

    const double percentage = total > 0
        ? 100.0 * effective_completed / static_cast<double>(total)
        : 0.0;

    progress
        << "Market Terrarium experiment runner\n"
        << "==================================\n\n"
        << "Status: " << status << '\n'
        << "Results: " << output_path << '\n'
        << "Config: " << config_path << '\n'
        << "Completed: " << completed << " / " << total
        << " (" << std::fixed << std::setprecision(1)
        << percentage << "% including live progress)\n"
        << "Failed: " << failed << '\n'
        << "Elapsed: " << format_duration(elapsed) << '\n';

    if (runs_per_second > 0.0 && completed < total)
    {
        progress
            << "ETA: " << format_duration(eta_seconds) << '\n'
            << "Effective throughput: " << std::setprecision(2)
            << runs_per_second * 60.0 << " runs/min\n";
    }

    progress << "\nActive runs\n-----------\n";
    bool any_active = false;

    for (std::size_t i = 0; i < live_states.size(); ++i)
    {
        const auto& state = live_states[i];
        if (!state.active)
        {
            continue;
        }

        any_active = true;
        const double run_percentage = state.total_ticks > 0
            ? 100.0 * static_cast<double>(state.tick) /
                static_cast<double>(state.total_ticks)
            : 0.0;

        progress
            << "Worker " << i
            << " | condition=" << state.condition_id
            << " | seed=" << state.seed
            << " | R=" << state.random_count
            << " M=" << state.mean_count
            << " P=" << state.rebalancer_count
            << " N=" << state.neural_count
            << " | mean_memory=" << state.mean_memory
            << " | instruments=" << state.instrument_count
            << " | shares=" << state.shares_per_instrument
            << " | P0=" << state.starting_price
            << " | tax=" << (state.tax_enabled ? "on" : "off")
            << "(" << state.tax_season_length << ")\n"
            << "  Tick: " << state.tick << " / " << state.total_ticks
            << " (" << std::setprecision(1) << run_percentage << "%)"
            << " | runtime " << format_duration(state.runtime_seconds)
            << '\n'
            << "  Trades: " << state.total_trades
            << " | last trade tick: " << state.last_trade_tick
            << " | active orders: " << state.active_orders << '\n'
            << "  Active traders: " << state.active_traders
            << " | R:" << state.random_active
            << " M:" << state.mean_active
            << " P:" << state.rebalancer_active
            << " N:" << state.neural_active << '\n'
            << "  Deaths: " << state.total_deaths
            << " | neural deaths: " << state.neural_deaths
            << " | longest completed neural: "
            << state.longest_neural_lifespan
            << " | oldest living neural: "
            << state.oldest_living_neural_age
            << " (id=" << state.oldest_living_neural_id << ')'
            << " | bank cash: " << state.bank_cash << '\n'
            << "  Prices:";

        const std::size_t shown_prices =
            std::min<std::size_t>(state.prices.size(), 8);

        for (std::size_t price_index = 0;
             price_index < shown_prices;
             ++price_index)
        {
            progress
                << " I" << (price_index + 1)
                << '=' << std::setprecision(4)
                << state.prices[price_index];
        }

        if (state.prices.size() > shown_prices)
        {
            progress << " ...";
        }

        progress << "\n\n";
    }

    if (!any_active)
    {
        progress << "None\n";
    }

    progress << "\nLatest completed run\n--------------------\n";
    if (latest_completed != nullptr)
    {
        progress
            << "Condition: " << latest_completed->condition_id << '\n'
            << "Seed: " << latest_completed->seed << '\n'
            << "Composition: R=" << latest_completed->random_count
            << " M=" << latest_completed->mean_count
            << " P=" << latest_completed->rebalancer_count
            << " N=" << latest_completed->neural_count << '\n'
            << "Mean memory: " << latest_completed->mean_memory
            << " | instruments=" << latest_completed->instrument_count
            << " | shares=" << latest_completed->shares_per_instrument
            << " | P0=" << latest_completed->starting_price << '\n'
            << "Trades: " << latest_completed->total_trades
            << " | deaths: " << latest_completed->total_deaths
            << " | neural deaths: " << latest_completed->neural_deaths
            << '\n'
            << "Longest completed neural: "
            << latest_completed->longest_neural_lifespan << '\n'
            << "Oldest living neural: "
            << latest_completed->oldest_living_neural_age
            << " (id=" << latest_completed->oldest_living_neural_id << ')'
            << " | cash=" << latest_completed->oldest_living_neural_cash
            << " | wealth=" << latest_completed->oldest_living_neural_wealth
            << '\n'
            << "Runtime: "
            << format_duration(latest_completed->runtime_seconds)
            << '\n';
    }
    else
    {
        progress << "None yet\n";
    }

    progress.close();

    std::error_code error;
    std::filesystem::rename(temporary_path, progress_path, error);
    if (error)
    {
        std::filesystem::remove(progress_path, error);
        error.clear();
        std::filesystem::rename(temporary_path, progress_path, error);
    }
}

void print_help(const char* executable)
{
    std::cout
        << "Usage:\n"
        << "  " << executable << "\n"
        << "      Start interactive configuration.\n\n"
        << "  " << executable << " --config FILE [--yes]\n"
        << "      Replay a saved configuration.\n\n"
        << "  " << executable << " --config FILE --dry-run\n"
        << "      Validate and count combinations without running them.\n\n"
        << "Config constraints use arithmetic plus one comparison, e.g.:\n"
        << "  constraint=random+mean+rebalancer+neural==200\n"
        << "Multiple constraints are ANDed together.\n";
}

int main(int argc, char** argv)
{
    try
    {
        std::optional<std::filesystem::path> requested_config;
        bool assume_yes = false;
        bool dry_run = false;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h")
            {
                print_help(argv[0]);
                return 0;
            }
            if (arg == "--yes" || arg == "-y")
            {
                assume_yes = true;
                continue;
            }
            if (arg == "--dry-run")
            {
                dry_run = true;
                continue;
            }
            if (arg == "--config")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument(
                        "--config requires a file path"
                    );
                }
                requested_config = argv[++i];
                continue;
            }

            throw std::invalid_argument("unknown argument: " + arg);
        }

        SweepConfig config = requested_config.has_value()
            ? read_config_file(*requested_config)
            : read_interactive_config();

        std::vector<Experiment> experiments =
            build_experiments(config);

        if (experiments.empty())
        {
            throw std::runtime_error("no experiments were generated");
        }

        const unsigned int worker_count =
            resolve_worker_count(config, experiments.size());

        int max_instrument_count = 0;
        for (const Experiment& experiment : experiments)
        {
            max_instrument_count = std::max(
                max_instrument_count,
                experiment.instrument_count
            );
        }

        print_summary(config, experiments.size(), worker_count);

        namespace fs = std::filesystem;
        const fs::path results_directory =
            fs::path(MARKET_TERRARIUM_SOURCE_DIR) / "results";
        fs::create_directories(results_directory);

        const std::string stamp = timestamp_now();
        const std::string base =
            safe_filename(config.name) + "_" + stamp;

        const fs::path output_path =
            results_directory / (base + ".csv");
        const fs::path saved_config_path =
            results_directory / (base + ".cfg");
        const fs::path error_path =
            results_directory / (base + "_errors.log");
        const fs::path progress_path =
            results_directory / "progress.txt";

        // Save before asking to begin, so even an abandoned launch has a
        // reproducible record of the resolved configuration.
        write_config_file(
            config,
            saved_config_path,
            experiments.size()
        );

        std::cout
            << "Resolved config saved to:\n  "
            << saved_config_path << "\n";

        if (dry_run)
        {
            std::cout << "Dry run complete; nothing was executed.\n";
            return 0;
        }

        if (!assume_yes)
        {
            std::cout << "Begin experiment? [Y/n]\n> ";
            std::string answer;
            std::getline(std::cin, answer);
            answer = lower_copy(trim(answer));
            if (
                !answer.empty() &&
                answer != "y" &&
                answer != "yes"
            )
            {
                std::cout << "Experiment cancelled.\n";
                return 0;
            }
        }

        std::ofstream csv(output_path);
        if (!csv)
        {
            throw std::runtime_error(
                "failed to open output CSV: " + output_path.string()
            );
        }
        csv << std::setprecision(10);
        write_header(csv, max_instrument_count);

        std::ofstream errors(error_path);

        std::cout
            << "Running " << experiments.size()
            << " experiments with " << worker_count
            << " worker threads.\n"
            << "Results: " << output_path << '\n';

        std::atomic<std::size_t> next_experiment{0};
        std::atomic<std::size_t> completed_runs{0};
        std::atomic<std::size_t> failed_runs{0};
        std::mutex output_mutex;

        std::vector<LiveRunState> live_states(worker_count);
        LiveRunState latest_completed;
        bool has_latest_completed = false;

        const auto experiment_start =
            std::chrono::steady_clock::now();
        auto last_progress_write = experiment_start;
        constexpr auto progress_write_interval =
            std::chrono::seconds(2);

        {
            std::lock_guard<std::mutex> lock(output_mutex);
            write_progress(
                progress_path,
                0,
                experiments.size(),
                0,
                experiment_start,
                live_states,
                nullptr,
                "Starting",
                output_path,
                saved_config_path
            );
        }

        auto worker = [&](unsigned int worker_index)
        {
            while (true)
            {
                const std::size_t index =
                    next_experiment.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );

                if (index >= experiments.size())
                {
                    break;
                }

                const Experiment experiment = experiments[index];
                const auto run_start =
                    std::chrono::steady_clock::now();

                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    LiveRunState& state = live_states[worker_index];
                    state = LiveRunState{};
                    state.active = true;
                    state.experiment_name = experiment.experiment_name;
                    state.condition_id = experiment.condition_id;
                    state.seed = experiment.seed;
                    state.random_count = experiment.random_count;
                    state.mean_count = experiment.mean_count;
                    state.rebalancer_count = experiment.rebalancer_count;
                    state.neural_count = experiment.neural_count;
                    state.mean_memory = experiment.mean_memory;
                    state.instrument_count = experiment.instrument_count;
                    state.prices.assign(
                        static_cast<std::size_t>(experiment.instrument_count),
                        0.0
                    );
                    state.shares_per_instrument =
                        experiment.shares_per_instrument;
                    state.starting_price = experiment.starting_price;
                    state.tax_enabled = experiment.tax_enabled;
                    state.tax_season_length =
                        experiment.tax_season_length;
                    state.cost_frequency = experiment.cost_frequency;
                    state.cost_amount = experiment.cost_amount;
                    state.total_ticks = experiment.ticks;
                }

                try
                {
                    ProgressCallback callback =
                        [&](int tick,
                            const SimulationSnapshot& snapshot,
                            int last_trade_tick)
                        {
                            std::lock_guard<std::mutex> lock(output_mutex);
                            LiveRunState& state = live_states[worker_index];
                            state.tick = tick;
                            state.total_trades = snapshot.total_trades;
                            state.last_trade_tick = last_trade_tick;
                            state.active_traders =
                                snapshot.active_total_traders;
                            state.active_orders = snapshot.active_orders;
                            state.random_active = snapshot.random;
                            state.mean_active = snapshot.mean_reversion;
                            state.rebalancer_active =
                                snapshot.portfolio_rebalancer;
                            state.neural_active = snapshot.neural_evolution;
                            state.total_deaths = snapshot.total_deaths;
                            state.neural_deaths =
                                snapshot.neural_evolution_deaths;
                            state.longest_neural_lifespan =
                                snapshot.longest_neural_lifespan;

                            if (!snapshot.oldest_neural_traders.empty())
                            {
                                const auto& oldest =
                                    snapshot.oldest_neural_traders.front();
                                state.oldest_living_neural_id =
                                    oldest.trader_id;
                                state.oldest_living_neural_age = oldest.age;
                                state.oldest_living_neural_cash = oldest.cash;
                                state.oldest_living_neural_wealth =
                                    oldest.wealth;
                            }
                            else
                            {
                                state.oldest_living_neural_id = 0;
                                state.oldest_living_neural_age = 0;
                                state.oldest_living_neural_cash = 0;
                                state.oldest_living_neural_wealth = 0;
                            }

                            state.bank_cash = snapshot.bank_cash;
                            state.runtime_seconds =
                                std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() -
                                    run_start
                                ).count();

                            for (int id : snapshot.instrument_ids)
                            {
                                auto price_it =
                                    snapshot.instrument_reference_price.find(id);

                                if (
                                    id >= 1 &&
                                    static_cast<std::size_t>(id) <=
                                        state.prices.size() &&
                                    price_it !=
                                        snapshot.instrument_reference_price.end()
                                )
                                {
                                    state.prices[
                                        static_cast<std::size_t>(id - 1)
                                    ] = price_it->second.current_price;
                                }
                            }

                            const auto progress_now =
                                std::chrono::steady_clock::now();
                            if (
                                progress_now - last_progress_write >=
                                progress_write_interval
                            )
                            {
                                write_progress(
                                    progress_path,
                                    completed_runs.load(),
                                    experiments.size(),
                                    failed_runs.load(),
                                    experiment_start,
                                    live_states,
                                    has_latest_completed
                                        ? &latest_completed
                                        : nullptr,
                                    "Running",
                                    output_path,
                                    saved_config_path
                                );
                                last_progress_write = progress_now;
                            }
                        };

                    RunResult result =
                        run_experiment(experiment, callback);

                    const std::size_t completed =
                        completed_runs.fetch_add(
                            1,
                            std::memory_order_relaxed
                        ) + 1;

                    {
                        std::lock_guard<std::mutex> lock(output_mutex);
                        LiveRunState& state = live_states[worker_index];
                        const auto& snapshot = result.final_snapshot;

                        state.tick = snapshot.tick;
                        state.total_trades = snapshot.total_trades;
                        state.last_trade_tick = result.last_trade_tick;
                        state.active_traders =
                            snapshot.active_total_traders;
                        state.active_orders = snapshot.active_orders;
                        state.random_active = snapshot.random;
                        state.mean_active = snapshot.mean_reversion;
                        state.rebalancer_active =
                            snapshot.portfolio_rebalancer;
                        state.neural_active = snapshot.neural_evolution;
                        state.total_deaths = snapshot.total_deaths;
                        state.neural_deaths =
                            snapshot.neural_evolution_deaths;
                        state.longest_neural_lifespan =
                            result.longest_neural_lifespan;
                        state.bank_cash = snapshot.bank_cash;
                        state.runtime_seconds = result.runtime_seconds;

                        if (!snapshot.oldest_neural_traders.empty())
                        {
                            const auto& oldest =
                                snapshot.oldest_neural_traders.front();
                            state.oldest_living_neural_id = oldest.trader_id;
                            state.oldest_living_neural_age = oldest.age;
                            state.oldest_living_neural_cash = oldest.cash;
                            state.oldest_living_neural_wealth = oldest.wealth;
                        }
                        else
                        {
                            state.oldest_living_neural_id = 0;
                            state.oldest_living_neural_age = 0;
                            state.oldest_living_neural_cash = 0;
                            state.oldest_living_neural_wealth = 0;
                        }

                        for (int id : snapshot.instrument_ids)
                        {
                            if (
                                id >= 1 &&
                                static_cast<std::size_t>(id) <=
                                    state.prices.size()
                            )
                            {
                                state.prices[
                                    static_cast<std::size_t>(id - 1)
                                ] = snapshot.instrument_reference_price.at(id)
                                        .current_price;
                            }
                        }

                        latest_completed = state;
                        latest_completed.active = false;
                        has_latest_completed = true;
                        state.active = false;

                        write_result(csv, experiment, result, max_instrument_count);

                        write_progress(
                            progress_path,
                            completed,
                            experiments.size(),
                            failed_runs.load(),
                            experiment_start,
                            live_states,
                            &latest_completed,
                            "Running",
                            output_path,
                            saved_config_path
                        );
                        last_progress_write =
                            std::chrono::steady_clock::now();

                        std::cout
                            << '[' << completed << '/'
                            << experiments.size() << "] condition="
                            << experiment.condition_id
                            << " seed=" << experiment.seed
                            << " R=" << experiment.random_count
                            << " M=" << experiment.mean_count
                            << " P=" << experiment.rebalancer_count
                            << " N=" << experiment.neural_count
                            << " memory=" << experiment.mean_memory
                            << " instruments=" << experiment.instrument_count
                            << " shares=" << experiment.shares_per_instrument
                            << " | trades="
                            << result.final_snapshot.total_trades
                            << " neural_deaths="
                            << result.final_snapshot.neural_evolution_deaths
                            << " | " << std::fixed << std::setprecision(2)
                            << result.runtime_seconds << "s\n"
                            << std::defaultfloat;
                    }
                }
                catch (const std::exception& error)
                {
                    const std::size_t failures =
                        failed_runs.fetch_add(
                            1,
                            std::memory_order_relaxed
                        ) + 1;
                    const std::size_t completed =
                        completed_runs.fetch_add(
                            1,
                            std::memory_order_relaxed
                        ) + 1;

                    std::lock_guard<std::mutex> lock(output_mutex);
                    live_states[worker_index].active = false;

                    if (errors)
                    {
                        errors
                            << "condition=" << experiment.condition_id
                            << " seed=" << experiment.seed
                            << " R=" << experiment.random_count
                            << " M=" << experiment.mean_count
                            << " P=" << experiment.rebalancer_count
                            << " N=" << experiment.neural_count
                            << " mean_memory=" << experiment.mean_memory
                            << " instruments=" << experiment.instrument_count
                            << " shares=" << experiment.shares_per_instrument
                            << " starting_price=" << experiment.starting_price
                            << " error=" << error.what() << '\n';
                        errors.flush();
                    }

                    std::cerr
                        << '[' << completed << '/'
                        << experiments.size() << "] FAILED condition="
                        << experiment.condition_id
                        << " seed=" << experiment.seed
                        << " | " << error.what() << '\n';

                    write_progress(
                        progress_path,
                        completed,
                        experiments.size(),
                        failures,
                        experiment_start,
                        live_states,
                        has_latest_completed
                            ? &latest_completed
                            : nullptr,
                        "Running - latest run failed",
                        output_path,
                        saved_config_path
                    );
                    last_progress_write =
                        std::chrono::steady_clock::now();
                }
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (unsigned int i = 0; i < worker_count; ++i)
        {
            workers.emplace_back(worker, i);
        }
        for (auto& thread : workers)
        {
            thread.join();
        }

        const auto experiment_end =
            std::chrono::steady_clock::now();
        const double total_seconds =
            std::chrono::duration<double>(
                experiment_end - experiment_start
            ).count();

        {
            std::lock_guard<std::mutex> lock(output_mutex);
            write_progress(
                progress_path,
                completed_runs.load(),
                experiments.size(),
                failed_runs.load(),
                experiment_start,
                live_states,
                has_latest_completed
                    ? &latest_completed
                    : nullptr,
                "Complete",
                output_path,
                saved_config_path
            );
        }

        std::cout
            << "\nFinished " << experiments.size()
            << " runs in " << total_seconds << " seconds.\n"
            << "Results: " << output_path << '\n'
            << "Config: " << saved_config_path << '\n';

        return failed_runs.load() == 0 ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "batch runner error: " << error.what() << '\n';
        return 1;
    }
}
