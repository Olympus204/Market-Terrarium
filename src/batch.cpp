#include "simulation.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <atomic>
#include <mutex>
#include <thread>
#include <exception>
#include <functional>
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
};
struct LiveRunState
{
    bool active = false;
    std::string experiment_type;
    std::uint64_t seed = 0;
    int random_count = 0;
    int mean_count = 0;
    int rebalancer_count = 0;
    int neural_count = 0;
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
    std::array<double, 4> prices{0.0, 0.0, 0.0, 0.0};
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
RunResult run_experiment(
    std::uint64_t seed,
    int random_count,
    int mean_count,
    int rebalancer_count,
    int neural_count,
    int cost_frequency,
    std::int64_t cost_amount,
    int ticks,
    int sample_every,
    const ProgressCallback& progress_callback
)
{
    auto start_time =
        std::chrono::steady_clock::now();
    // 200 traders start with 1000 each. 250000 leaves a 50000 cash
    // buffer for replacement timing while keeping the bank scale similar
    // to the larger GUI experiments.
    constexpr std::int64_t initial_bank_cash = 250000;
    Simulation sim{seed, static_cast<int>(initial_bank_cash)};
    sim.add_instrument(1, "ALPHA", 100);
    sim.add_instrument(2, "BETA", 100);
    sim.add_instrument(3, "GAMMA", 100);
    sim.add_instrument(4, "DELTA", 100);
    sim.set_starting_amount(1000);
    sim.set_recurring_costs(cost_frequency, cost_amount);
    // Scale the old 300-share / \~500-trader setup down to roughly the
    // same shares-per-trader ratio.
    sim.introduce_holdings(1, 120);
    sim.introduce_holdings(2, 120);
    sim.introduce_holdings(3, 120);
    sim.introduce_holdings(4, 120);
    // Disable redistribution without blocking trader replacement.
    // reserve_target is also used by the replacement affordability check,
    // so it must stay at zero here.
    sim.set_bank_recycling(
        0,
        1,
        0.0
    );
    // Build the initial population directly.  The replacement queue is for
    // traders that die during the simulation; using it for initialisation
    // makes batch startup depend on the queue-processing semantics in tick().
    auto add_initial_traders = [&](int count, TraderType type, const char* label)
    {
        for (int i = 0; i < count; ++i)
        {
            if (!sim.add_trader(1000, type))
            {
                std::ostringstream message;
                message
                    << "failed to create initial " << label
                    << " trader " << (i + 1) << " / " << count;
                throw std::logic_error(message.str());
            }
        }
    };
    add_initial_traders(random_count, TraderType::random, "random");
    add_initial_traders(mean_count, TraderType::mean_value, "mean-value");
    add_initial_traders(
        rebalancer_count,
        TraderType::portfolio_rebalancer,
        "rebalancer"
    );
    add_initial_traders(
        neural_count,
        TraderType::neural_evolution,
        "neural"
    );
    // Sanity-check immediately, before the first simulated tick.
    {
        const SimulationSnapshot startup = sim.get_snapshot();
        const int requested_population =
            random_count + mean_count + rebalancer_count + neural_count;
        if (
            startup.active_total_traders != requested_population ||
            startup.random != random_count ||
            startup.mean_reversion != mean_count ||
            startup.portfolio_rebalancer != rebalancer_count ||
            startup.neural_evolution != neural_count
        )
        {
            std::ostringstream message;
            message
                << "initial population mismatch: requested total="
                << requested_population
                << " R=" << random_count
                << " M=" << mean_count
                << " P=" << rebalancer_count
                << " N=" << neural_count
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
    result.longest_neural_lifespan = initial.longest_neural_lifespan;
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
    for (int tick = 1; tick <= ticks; ++tick)
    {
        sim.tick();
        int current_trade_count = sim.get_total_trades();
        if (current_trade_count > previous_trade_count)
        {
            result.last_trade_tick = tick;
            ++result.active_trade_ticks;
        }
        if (tick == ticks - 1000)
        {
            trade_count_1000_ticks_before_end =
                current_trade_count;
        }
        previous_trade_count = current_trade_count;
        if (tick % sample_every == 0)
        {
            SimulationSnapshot snapshot =
                sim.get_snapshot();
            active_orders_sum += snapshot.active_orders;
            ++active_orders_samples;
            result.longest_neural_lifespan = std::max(
                result.longest_neural_lifespan,
                snapshot.longest_neural_lifespan
            );
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
            auto now = std::chrono::steady_clock::now();
            if (
                tick == ticks ||
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
    result.longest_neural_lifespan = std::max(
        result.longest_neural_lifespan,
        result.final_snapshot.longest_neural_lifespan
    );
    result.trades_last_1000_ticks =
        result.final_snapshot.total_trades
        - trade_count_1000_ticks_before_end;
    if (active_orders_samples > 0)
    {
        result.mean_active_orders =
            active_orders_sum
            / static_cast<double>(active_orders_samples);
    }
    if (ticks % sample_every != 0)
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
    auto end_time =
        std::chrono::steady_clock::now();
    result.runtime_seconds =
        std::chrono::duration<double>(
            end_time - start_time
        ).count();
    return result;
}
void write_header(std::ofstream& csv)
{
    csv
        << "experiment_type,"
        << "seed,"
        << "cost_frequency,"
        << "cost_amount,"
        << "mean_cost_per_tick,"
        << "ticks_requested,"
        << "final_tick,"
        << "random_count,"
        << "mean_count,"
        << "rebalancer_count,"
        << "neural_count,"
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
        << "oldest_living_neural_alpha,"
        << "oldest_living_neural_beta,"
        << "oldest_living_neural_gamma,"
        << "oldest_living_neural_delta,"
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
        << "max_relative_spread,";
    const std::vector<std::string> names{
        "alpha",
        "beta",
        "gamma",
        "delta"
    };
    for (const auto& name : names)
    {
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
    const std::string& experiment_type,
    std::uint64_t seed,
    int cost_frequency,
    std::int64_t cost_amount,
    int ticks,
    int random_count,
    int mean_count,
    int rebalancer_count,
    int neural_count,
    const RunResult& result
)
{
    const SimulationSnapshot& snapshot =
        result.final_snapshot;

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
    csv
        << experiment_type << ','
        << seed << ','
        << cost_frequency << ','
        << cost_amount << ','
        << (static_cast<double>(cost_amount) / static_cast<double>(cost_frequency)) << ','
        << ticks << ','
        << snapshot.tick << ','
        << random_count << ','
        << mean_count << ','
        << rebalancer_count << ','
        << neural_count << ','
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
        << holding_or_zero(1) << ','
        << holding_or_zero(2) << ','
        << holding_or_zero(3) << ','
        << holding_or_zero(4) << ','
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
        << result.max_relative_spread << ',';
    for (int id = 1; id <= 4; ++id)
    {
        const PriceStats& stats =
            result.price_stats.at(id);
        int trades = 0;
        auto trade_it =
            snapshot.total_trades_per_instrument.find(id);
        if (trade_it !=
            snapshot.total_trades_per_instrument.end())
        {
            trades = trade_it->second;
        }
        csv
            << snapshot.instrument_reference_price.at(id).current_price
            << ','
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
            )
            << ','
            << percentage_or_zero(
                snapshot.mean_reversion_percentage_of_each_instrument,
                id
            )
            << ','
            << percentage_or_zero(
                snapshot.portfolio_rebalancer_percentage_of_each_instrument,
                id
            )
            << ','
            << percentage_or_zero(
                snapshot.neural_evolution_percentage_of_each_instrument,
                id
            )
            << ','
            << percentage_or_zero(
                snapshot.bank_percentage_of_each_instrument,
                id
            )
            << ',';
    }
    csv << result.runtime_seconds << '\n';
    csv.flush();
}
struct Experiment
{
    std::string experiment_type;
    std::uint64_t seed;
    int random_count;
    int mean_count;
    int rebalancer_count;
    int neural_count;
    int cost_frequency;
    std::int64_t cost_amount;
};
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
void write_progress(
    const std::filesystem::path& progress_path,
    std::size_t completed,
    std::size_t total,
    std::size_t failed,
    std::chrono::steady_clock::time_point start_time,
    const std::vector<LiveRunState>& live_states,
    const LiveRunState* latest_completed,
    const std::string& status
)
{
    auto now =
        std::chrono::steady_clock::now();
    double elapsed =
        std::chrono::duration<double>(
            now - start_time
        ).count();
    double effective_completed =
        static_cast<double>(completed);
    for (const auto& state : live_states)
    {
        if (state.active && state.total_ticks > 0)
        {
            effective_completed +=
                static_cast<double>(state.tick)
                / static_cast<double>(state.total_ticks);
        }
    }
    double runs_per_second = 0.0;
    double eta_seconds = 0.0;
    if (elapsed > 0.0 && effective_completed > 0.0)
    {
        runs_per_second = effective_completed / elapsed;
        eta_seconds =
            static_cast<double>(total) > effective_completed
                ? (static_cast<double>(total) - effective_completed)
                    / runs_per_second
                : 0.0;
    }
    auto temporary_path = progress_path;
    temporary_path += ".tmp";
    std::ofstream progress(temporary_path);
    if (!progress)
    {
        return;
    }
    double percentage =
        total > 0
            ? 100.0 * effective_completed
                / static_cast<double>(total)
            : 0.0;
    progress
        << "Market Terrarium random-trader sweep\n"
        << "========================================\n\n"
        << "Status: " << status << '\n'
        << "Completed: "
        << completed
        << " / "
        << total
        << " ("
        << std::fixed
        << std::setprecision(1)
        << percentage
        << "% including live progress)\n"
        << "Failed: "
        << failed
        << '\n'
        << "Elapsed: "
        << format_duration(elapsed)
        << '\n';
    if (runs_per_second > 0.0 && completed < total)
    {
        progress
            << "ETA: "
            << format_duration(eta_seconds)
            << '\n'
            << "Effective throughput: "
            << std::setprecision(2)
            << runs_per_second * 60.0
            << " runs/min\n";
    }
    progress
        << "\nActive runs\n"
        << "-----------\n";
    bool any_active = false;
    for (std::size_t i = 0; i < live_states.size(); ++i)
    {
        const auto& state = live_states[i];
        if (!state.active)
        {
            continue;
        }
        any_active = true;
        double run_percentage =
            state.total_ticks > 0
                ? 100.0 * static_cast<double>(state.tick)
                    / static_cast<double>(state.total_ticks)
                : 0.0;
        progress
            << "Worker " << i << ": "
            << state.experiment_type
            << " | seed=" << state.seed
            << " | target R=" << state.random_count
            << " M=" << state.mean_count
            << " P=" << state.rebalancer_count
            << " N=" << state.neural_count
            << " | charge=" << state.cost_amount
            << " every " << state.cost_frequency
            << " ticks ("
            << std::setprecision(2)
            << (state.cost_frequency > 0
                ? static_cast<double>(state.cost_amount) / static_cast<double>(state.cost_frequency)
                : 0.0)
            << "/tick)"
            << '\n'
            << "  Tick: "
            << state.tick
            << " / "
            << state.total_ticks
            << " ("
            << std::setprecision(1)
            << run_percentage
            << "%)"
            << " | runtime "
            << format_duration(state.runtime_seconds)
            << '\n'
            << "  Trades: "
            << state.total_trades
            << " | last trade tick: "
            << state.last_trade_tick
            << " | active orders: "
            << state.active_orders
            << '\n'
            << "  Active traders: "
            << state.active_traders
            << " | R: "
            << state.random_active
            << " | M: "
            << state.mean_active
            << " | P: "
            << state.rebalancer_active
            << " | N: "
            << state.neural_active
            << '\n'
            << "  Deaths: "
            << state.total_deaths
            << " | neural deaths: "
            << state.neural_deaths
            << " | longest completed neural: "
            << state.longest_neural_lifespan
            << " | oldest living neural: "
            << state.oldest_living_neural_age
            << " (id=" << state.oldest_living_neural_id << ')'
            << " | bank cash: "
            << state.bank_cash
            << '\n'
            << "  Prices: A="
            << std::setprecision(4)
            << state.prices[0]
            << " B=" << state.prices[1]
            << " G=" << state.prices[2]
            << " D=" << state.prices[3]
            << "\n\n";
    }
    if (!any_active)
    {
        progress << "None\n";
    }
    progress
        << "\nLatest completed run\n"
        << "--------------------\n";
    if (latest_completed != nullptr)
    {
        progress
            << "Experiment: "
            << latest_completed->experiment_type
            << '\n'
            << "Seed: "
            << latest_completed->seed
            << '\n'
            << "Target composition: R="
            << latest_completed->random_count
            << " M="
            << latest_completed->mean_count
            << " P="
            << latest_completed->rebalancer_count
            << " N="
            << latest_completed->neural_count
            << '\n'
            << "Charge: "
            << latest_completed->cost_amount
            << " every "
            << latest_completed->cost_frequency
            << " ticks ("
            << std::setprecision(2)
            << (latest_completed->cost_frequency > 0
                ? static_cast<double>(latest_completed->cost_amount)
                    / static_cast<double>(latest_completed->cost_frequency)
                : 0.0)
            << "/tick)\n"
            << "Trades: "
            << latest_completed->total_trades
            << " | last trade tick: "
            << latest_completed->last_trade_tick
            << '\n'
            << "Deaths: "
            << latest_completed->total_deaths
            << " | neural deaths: "
            << latest_completed->neural_deaths
            << '\n'
            << "Longest completed neural lifespan: "
            << latest_completed->longest_neural_lifespan
            << '\n'
            << "Oldest living neural: "
            << latest_completed->oldest_living_neural_age
            << " (id=" << latest_completed->oldest_living_neural_id << ")"
            << " | cash=" << latest_completed->oldest_living_neural_cash
            << " | wealth=" << latest_completed->oldest_living_neural_wealth
            << '\n'
            << "Bank cash: "
            << latest_completed->bank_cash
            << " | runtime: "
            << format_duration(latest_completed->runtime_seconds)
            << '\n';
    }
    else
    {
        progress << "None yet\n";
    }
    progress.close();
    std::error_code error;
    std::filesystem::rename(
        temporary_path,
        progress_path,
        error
    );
    if (error)
    {
        std::filesystem::remove(progress_path, error);
        error.clear();
        std::filesystem::rename(
            temporary_path,
            progress_path,
            error
        );
    }
}
int main()
{
    constexpr int ticks_per_run = 250000;
    constexpr int sample_every = 100;
    constexpr auto progress_write_interval = std::chrono::seconds(2);
    // Random-trader sweep. Keep total population fixed at 200 so runtime and
    // population size are controlled; random traders replace neural traders.
    // All conditions use the current smooth recurring cost: 2 cash every tick.
    // Five seeds x eleven conditions x 30000 ticks makes this a screening run
    // rather than another overnight act of computational self-harm.
    const std::vector<std::uint64_t> seeds{
        1001,
        1002,
        1003,
        1004,
        1005
    };
    namespace fs = std::filesystem;
    fs::path results_directory =
        fs::path(MARKET_TERRARIUM_SOURCE_DIR) / "results";
    fs::path progress_path =
        results_directory / "progress.txt";
    fs::create_directories(results_directory);
    auto now = std::chrono::system_clock::now();
    std::time_t now_time =
        std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);
    std::ostringstream timestamp;
    timestamp << std::put_time(
        local_time,
        "%Y-%m-%d_%H-%M-%S"
    );
    fs::path output_path =
        results_directory /
        (
            "random_trader_sweep_"
            + timestamp.str()
            + ".csv"
        );
    std::ofstream csv(output_path);
    if (!csv)
    {
        std::cerr
            << "Failed to open "
            << output_path
            << '\n';
        return 1;
    }
    csv << std::setprecision(10);
    write_header(csv);
    fs::path error_path =
        results_directory /
        (
            "random_trader_sweep_"
            + timestamp.str()
            + "_errors.log"
        );
    std::ofstream errors(error_path);
    constexpr int total_traders = 200;
    constexpr int cost_frequency = 1;
    constexpr std::int64_t cost_amount = 2;
    // Fine resolution around the 0.5-5% region where the GUI runs have looked
    // strongest, with a broader tail to check whether the optimum has moved.
    const std::vector<int> random_counts{
        0, 40, 70, 80, 120, 160, 180, 190
    };
    std::vector<Experiment> conditions;
    conditions.reserve(random_counts.size());
    for (int random_count : random_counts)
    {
        const int neural_count = total_traders - random_count;
        std::ostringstream label;
        label << "R" << random_count << "_N" << neural_count;
        conditions.push_back(
            Experiment{
                label.str(),
                0,
                random_count,
                0,
                0,
                neural_count,
                cost_frequency,
                cost_amount
            }
        );
    }
    std::vector<Experiment> experiments;
    experiments.reserve(seeds.size() * conditions.size());
    for (std::uint64_t seed : seeds)
    {
        for (Experiment condition : conditions)
        {
            condition.seed = seed;
            experiments.push_back(condition);
        }
    }
    const std::size_t total_runs = experiments.size();
    unsigned int hardware_threads =
        std::thread::hardware_concurrency();
    if (hardware_threads == 0)
    {
        hardware_threads = 4;
    }
    // Leave two logical CPUs free so the desktop remains usable while the
    // synthetic financiers consume the rest.
    unsigned int worker_count =
        hardware_threads > 2
            ? hardware_threads - 2
            : 1;
    worker_count = std::min(
        worker_count,
        static_cast<unsigned int>(total_runs)
    );
    std::cout
        << "Running "
        << total_runs
        << " random-trader sweep experiments with "
        << worker_count
        << " worker threads ("
        << hardware_threads
        << " logical CPUs detected).\n";
    std::atomic<std::size_t> next_experiment{0};
    std::atomic<std::size_t> completed_runs{0};
    std::atomic<std::size_t> failed_runs{0};
    std::mutex output_mutex;
    std::vector<LiveRunState> live_states(worker_count);
    LiveRunState latest_completed;
    bool has_latest_completed = false;
    auto experiment_start =
        std::chrono::steady_clock::now();
    auto last_progress_write = experiment_start;
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        write_progress(
            progress_path,
            0,
            total_runs,
            0,
            experiment_start,
            live_states,
            nullptr,
            "Starting"
        );
    }
    auto worker = [&](unsigned int worker_index)
    {
        while (true)
        {
            std::size_t index =
                next_experiment.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            if (index >= total_runs)
            {
                break;
            }
            const Experiment experiment =
                experiments[index];
            auto run_start = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                LiveRunState& state = live_states[worker_index];
                state = LiveRunState{};
                state.active = true;
                state.experiment_type = experiment.experiment_type;
                state.seed = experiment.seed;
                state.random_count = experiment.random_count;
                state.mean_count = experiment.mean_count;
                state.rebalancer_count = experiment.rebalancer_count;
                state.neural_count = experiment.neural_count;
                state.cost_frequency = experiment.cost_frequency;
                state.cost_amount = experiment.cost_amount;
                state.total_ticks = ticks_per_run;
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
                    state.active_traders = snapshot.active_total_traders;
                    state.active_orders = snapshot.active_orders;
                    state.random_active = snapshot.random;
                    state.mean_active = snapshot.mean_reversion;
                    state.rebalancer_active = snapshot.portfolio_rebalancer;
                    state.neural_active = snapshot.neural_evolution;
                    state.total_deaths = snapshot.total_deaths;
                    state.neural_deaths = snapshot.neural_evolution_deaths;
                    state.longest_neural_lifespan = snapshot.longest_neural_lifespan;
                    if (!snapshot.oldest_neural_traders.empty())
                    {
                        const auto& oldest = snapshot.oldest_neural_traders.front();
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
                    state.bank_cash = snapshot.bank_cash;
                    state.runtime_seconds =
                        std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - run_start
                        ).count();
                    for (int id = 1; id <= 4; ++id)
                    {
                        auto price_it =
                            snapshot.instrument_reference_price.find(id);
                        if (price_it !=
                            snapshot.instrument_reference_price.end())
                        {
                            state.prices[static_cast<std::size_t>(id - 1)] =
                                price_it->second.current_price;
                        }
                    }
                    auto progress_now =
                        std::chrono::steady_clock::now();
                    if (
                        progress_now - last_progress_write
                        >= progress_write_interval
                    )
                    {
                        write_progress(
                            progress_path,
                            completed_runs.load(),
                            total_runs,
                            failed_runs.load(),
                            experiment_start,
                            live_states,
                            has_latest_completed
                                ? &latest_completed
                                : nullptr,
                            "Running"
                        );
                        last_progress_write = progress_now;
                    }
                };
                RunResult result =
                    run_experiment(
                        experiment.seed,
                        experiment.random_count,
                        experiment.mean_count,
                        experiment.rebalancer_count,
                        experiment.neural_count,
                        experiment.cost_frequency,
                        experiment.cost_amount,
                        ticks_per_run,
                        sample_every,
                        callback
                    );
                std::size_t completed =
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
                    state.active_traders = snapshot.active_total_traders;
                    state.active_orders = snapshot.active_orders;
                    state.random_active = snapshot.random;
                    state.mean_active = snapshot.mean_reversion;
                    state.rebalancer_active = snapshot.portfolio_rebalancer;
                    state.neural_active = snapshot.neural_evolution;
                    state.total_deaths = snapshot.total_deaths;
                    state.neural_deaths = snapshot.neural_evolution_deaths;
                    state.longest_neural_lifespan = snapshot.longest_neural_lifespan;
                    state.bank_cash = snapshot.bank_cash;
                    state.runtime_seconds = result.runtime_seconds;
                    state.longest_neural_lifespan = result.longest_neural_lifespan;
                    if (!snapshot.oldest_neural_traders.empty())
                    {
                        const auto& oldest = snapshot.oldest_neural_traders.front();
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
                    for (int id = 1; id <= 4; ++id)
                    {
                        state.prices[static_cast<std::size_t>(id - 1)] =
                            snapshot.instrument_reference_price.at(id)
                                .current_price;
                    }
                    latest_completed = state;
                    latest_completed.active = false;
                    has_latest_completed = true;
                    state.active = false;
                    write_result(
                        csv,
                        experiment.experiment_type,
                        experiment.seed,
                        experiment.cost_frequency,
                        experiment.cost_amount,
                        ticks_per_run,
                        experiment.random_count,
                        experiment.mean_count,
                        experiment.rebalancer_count,
                        experiment.neural_count,
                        result
                    );
                    write_progress(
                        progress_path,
                        completed,
                        total_runs,
                        failed_runs.load(),
                        experiment_start,
                        live_states,
                        &latest_completed,
                        "Running"
                    );
                    last_progress_write =
                        std::chrono::steady_clock::now();
                    std::cout
                        << '['
                        << completed
                        << '/'
                        << total_runs
                        << "] "
                        << experiment.experiment_type
                        << " seed="
                        << experiment.seed
                        << " | trades="
                        << result.final_snapshot.total_trades
                        << " last_trade="
                        << result.last_trade_tick
                        << " deaths="
                        << result.final_snapshot.total_deaths
                        << " neural_deaths="
                        << result.final_snapshot.neural_evolution_deaths
                        << " | "
                        << std::fixed
                        << std::setprecision(2)
                        << result.runtime_seconds
                        << "s\n"
                        << std::defaultfloat;
                }
            }
            catch (const std::exception& error)
            {
                std::size_t failures =
                    failed_runs.fetch_add(
                        1,
                        std::memory_order_relaxed
                    ) + 1;
                std::size_t completed =
                    completed_runs.fetch_add(
                        1,
                        std::memory_order_relaxed
                    ) + 1;
                std::lock_guard<std::mutex> lock(output_mutex);
                live_states[worker_index].active = false;
                write_progress(
                    progress_path,
                    completed,
                    total_runs,
                    failures,
                    experiment_start,
                    live_states,
                    has_latest_completed
                        ? &latest_completed
                        : nullptr,
                    "Running - latest run failed"
                );
                last_progress_write =
                    std::chrono::steady_clock::now();
                std::cerr
                    << '['
                    << completed
                    << '/'
                    << total_runs
                    << "] FAILED: "
                    << experiment.experiment_type
                    << " seed="
                    << experiment.seed
                    << " | "
                    << error.what()
                    << '\n';
                if (errors)
                {
                    errors
                        << "experiment="
                        << experiment.experiment_type
                        << " seed="
                        << experiment.seed
                        << " random="
                        << experiment.random_count
                        << " mean="
                        << experiment.mean_count
                        << " rebalancer="
                        << experiment.rebalancer_count
                        << " neural="
                        << experiment.neural_count
                        << " error="
                        << error.what()
                        << '\n';
                    errors.flush();
                }
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
    auto experiment_end =
        std::chrono::steady_clock::now();
    double total_seconds =
        std::chrono::duration<double>(
            experiment_end - experiment_start
        ).count();
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        write_progress(
            progress_path,
            total_runs,
            total_runs,
            failed_runs.load(),
            experiment_start,
            live_states,
            has_latest_completed
                ? &latest_completed
                : nullptr,
            "Complete"
        );
    }
    std::cout
        << "\nFinished "
        << total_runs
        << " runs in "
        << total_seconds
        << " seconds.\n"
        << "Results written to "
        << output_path
        << '\n';
    return 0;
}
