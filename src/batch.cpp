#include "simulation.hpp"

#include <algorithm>
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

struct PriceStats
{
    double sum = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();

    double return_sum = 0.0;
    double return_square_sum = 0.0;

    double previous_price = 0.0;

    int samples = 0;
    int return_samples = 0;

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
};

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
    int ticks,
    int sample_every
)
{
    auto start_time =
        std::chrono::steady_clock::now();

    // TODO:
    // Move this into a shared experiment initializer later.
    Simulation sim{seed, 225000};

    sim.add_instrument(1, "ALPHA", 100);
    sim.add_instrument(2, "BETA", 100);
    sim.add_instrument(3, "GAMMA", 100);
    sim.add_instrument(4, "DELTA", 100);

    sim.set_starting_amount(1000);
    sim.set_recurring_costs(100, 100);

    sim.introduce_holdings(1, 300);
    sim.introduce_holdings(2, 300);
    sim.introduce_holdings(3, 300);
    sim.introduce_holdings(4, 300);

    sim.set_bank_recycling(
        7500,
        1,
        0.25
    );

    for (int i = 0; i < random_count; ++i)
    {
        sim.queue_trader(TraderType::random);
    }

    for (int i = 0; i < mean_count; ++i)
    {
        sim.queue_trader(TraderType::mean_value);
    }

    for (int i = 0; i < rebalancer_count; ++i)
    {
        sim.queue_trader(
            TraderType::portfolio_rebalancer
        );
    }

    RunResult result;

    double spread_sum = 0.0;
    double spread_max = 0.0;

    double relative_spread_sum = 0.0;
    double relative_spread_max = 0.0;

    int spread_samples = 0;

    SimulationSnapshot initial = sim.get_snapshot();

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
    }

    result.final_snapshot = sim.get_snapshot();

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
        << "ticks_requested,"
        << "final_tick,"
        << "random_count,"
        << "mean_count,"
        << "rebalancer_count,"
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
            << name << "_trades,"
            << name << "_random_ownership,"
            << name << "_mean_ownership,"
            << name << "_rebalancer_ownership,"
            << name << "_bank_ownership,";
    }

    csv << "runtime_seconds\n";
}

void write_result(
    std::ofstream& csv,
    const std::string& experiment_type,
    std::uint64_t seed,
    int ticks,
    int random_count,
    int mean_count,
    int rebalancer_count,
    const RunResult& result
)
{
    const SimulationSnapshot& snapshot =
        result.final_snapshot;

    csv
        << experiment_type << ','
        << seed << ','
        << ticks << ','
        << snapshot.tick << ','
        << random_count << ','
        << mean_count << ','
        << rebalancer_count << ','
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
    const Experiment& latest,
    const RunResult* result,
    const std::string& status
)
{
    auto now =
        std::chrono::steady_clock::now();

    double elapsed =
        std::chrono::duration<double>(
            now - start_time
        ).count();

    double runs_per_second = 0.0;
    double eta_seconds = 0.0;

    if (elapsed > 0.0 && completed > 0)
    {
        runs_per_second =
            static_cast<double>(completed)
            / elapsed;

        eta_seconds =
            static_cast<double>(
                total - completed
            ) / runs_per_second;
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
            ? 100.0 *
                static_cast<double>(completed)
                / static_cast<double>(total)
            : 0.0;

    progress
        << "Market Terrarium batch experiment\n"
        << "=================================\n\n"

        << "Status: " << status << '\n'
        << "Completed: "
        << completed
        << " / "
        << total
        << " ("
        << std::fixed
        << std::setprecision(1)
        << percentage
        << "%)\n"

        << "Failed: "
        << failed
        << '\n'

        << "Elapsed: "
        << format_duration(elapsed)
        << '\n';

    if (completed > 0 &&
        completed < total &&
        runs_per_second > 0.0)
    {
        progress
            << "ETA: "
            << format_duration(eta_seconds)
            << '\n';
    }

    if (elapsed > 0.0)
    {
        progress
            << "Throughput: "
            << std::setprecision(2)
            << (
                static_cast<double>(completed)
                / elapsed * 60.0
            )
            << " runs/min\n";
    }

    progress
        << "\nLatest completed run\n"
        << "--------------------\n"
        << "Experiment: "
        << latest.experiment_type
        << '\n'
        << "Seed: "
        << latest.seed
        << '\n'
        << "Random: "
        << latest.random_count
        << '\n'
        << "Mean: "
        << latest.mean_count
        << '\n'
        << "Rebalancer: "
        << latest.rebalancer_count
        << '\n';

    if (result != nullptr)
    {
        progress
            << "Trades: "
            << result->final_snapshot.total_trades
            << '\n'
            << "Last trade tick: "
            << result->last_trade_tick
            << '\n'
            << "Deaths: "
            << result->final_snapshot.total_deaths
            << '\n'
            << "Runtime: "
            << std::setprecision(2)
            << result->runtime_seconds
            << " s\n";
    }

    progress.close();

    std::filesystem::rename(
        temporary_path,
        progress_path
    );
}


int main()
{
    constexpr int total_traders = 135;

    constexpr int max_random_rebalancer = 10;
    constexpr int ticks_per_run = 25000;

    constexpr int sample_every = 10;

    const std::vector<std::uint64_t> seeds{
        1001,
        1002,
        1003,
        1004,
        1005,
        1006,
        1007,
        1008,
        1009,
        1010,
        1011,
        1012,
        1013,
        1014,
        1015,
        1016,
        1017,
        1018,
        1019,
        1020,
        1021,
        1022,
        1023,
        1024,
        1025,
        1026,
        1027,
        1028,
        1029,
        1030,
        1031,
        1032,
        1033,
        1034,
        1035,
        1036,
        1037,
        1038,
        1039,
        1040,
        1041,
        1042,
        1043,
        1044,
        1045,
        1046,
        1047,
        1048,
        1049,
        1050,
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
            "strategy_transition_"
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
            "strategy_transition_"
            + timestamp.str()
            + "_errors.log"
        );

    std::ofstream errors(error_path);

    std::vector<Experiment> experiments;

    // Experiment A:
    // Random traders + portfolio rebalancers.
    for (
        int random_count = 0;
        random_count <= max_random_rebalancer;
        ++random_count
    )
    {
        int remaining =
            total_traders - random_count;

        for (std::uint64_t seed : seeds)
        {
            experiments.push_back(
                Experiment{
                    "random_rebalancer",
                    seed,
                    random_count,
                    0,
                    remaining
                }
            );
        }
    }


    const std::size_t total_runs = experiments.size();

    unsigned int hardware_threads =
        std::thread::hardware_concurrency();

    if (hardware_threads == 0)
    {
        hardware_threads = 4;
    }

    unsigned int worker_count =
        hardware_threads > 1
            ? hardware_threads - 1
            : 1;

    worker_count = std::min(
        worker_count,
        static_cast<unsigned int>(total_runs)
    );

    std::cout
        << "Running "
        << total_runs
        << " experiments with "
        << worker_count
        << " worker threads ("
        << hardware_threads
        << " logical CPUs detected).\n";

    std::atomic<std::size_t> next_experiment{0};
    std::atomic<std::size_t> completed_runs{0};
    std::atomic<std::size_t> failed_runs{0};

    std::mutex output_mutex;

    auto experiment_start =
        std::chrono::steady_clock::now();


    {
    std::ofstream progress(progress_path);

        progress
            << "Market Terrarium batch experiment\n"
            << "=================================\n\n"
            << "Status: Starting\n"
            << "Completed: 0 / "
            << total_runs
            << '\n';
    }

    auto worker = [&]()
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

            try
            {
                RunResult result =
                    run_experiment(
                        experiment.seed,
                        experiment.random_count,
                        experiment.mean_count,
                        experiment.rebalancer_count,
                        ticks_per_run,
                        sample_every
                    );

                std::size_t completed =
                    completed_runs.fetch_add(
                        1,
                        std::memory_order_relaxed
                    ) + 1;

                {
                    std::lock_guard<std::mutex> lock(
                        output_mutex
                    );

                    write_result(
                        csv,
                        experiment.experiment_type,
                        experiment.seed,
                        ticks_per_run,
                        experiment.random_count,
                        experiment.mean_count,
                        experiment.rebalancer_count,
                        result
                    );

                    write_progress(
                        progress_path,
                        completed,
                        total_runs,
                        failed_runs.load(),
                        experiment_start,
                        experiment,
                        &result,
                        "Running"
                    );

                    std::cout
                        << '['
                        << completed
                        << '/'
                        << total_runs
                        << "] "
                        << experiment.experiment_type
                        << " "
                        << "seed="
                        << experiment.seed
                        << " random="
                        << experiment.random_count
                        << " mean="
                        << experiment.mean_count
                        << " rebalancer="
                        << experiment.rebalancer_count
                        << " | trades="
                        << result.final_snapshot.total_trades
                        << " last_trade="
                        << result.last_trade_tick
                        << " deaths="
                        << result.final_snapshot.total_deaths
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

                std::lock_guard<std::mutex> lock(
                    output_mutex
                );

                write_progress(
                    progress_path,
                    completed,
                    total_runs,
                    failures,
                    experiment_start,
                    experiment,
                    nullptr,
                    "Running - latest run failed"
                );

                std::cerr
                    << '['
                    << completed
                    << '/'
                    << total_runs
                    << "] FAILED: "
                    << experiment.experiment_type
                    << " "
                    << "seed="
                    << experiment.seed
                    << " random="
                    << experiment.random_count
                    << " mean="
                    << experiment.mean_count
                    << " rebalancer="
                    << experiment.rebalancer_count
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
        workers.emplace_back(worker);
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
            std::ofstream progress(progress_path);

            progress
                << "Market Terrarium batch experiment\n"
                << "=================================\n\n"
                << "Status: Complete\n"
                << "Completed: "
                << total_runs
                << " / "
                << total_runs
                << '\n'
                << "Failed: "
                << failed_runs.load()
                << '\n'
                << "Total time: "
                << format_duration(total_seconds)
                << '\n'
                << "Results: "
                << output_path.filename().string()
                << '\n';
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
