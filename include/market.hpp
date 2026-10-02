#pragma once

#include "order_book.hpp"
#include "trades.hpp"

#include <vector>
#include <map>
#include <unordered_map>
#include <cmath>
#include <string>
#include <deque>
#include <utility>

struct Instrument
{
    std::string name;
    double last_price;
    OrderBook book;
};

struct InstrumentStats
{
    double last_price = 0.0;
    int total_trades = 0;

    std::deque<double> returns;

    double return_sum = 0.0;
    double return_square_sum = 0.0;

    std::deque<std::pair<int, int>> recent_trade_ticks;
    int trades_last_100_ticks_cached = 0;

    static constexpr std::size_t volatility_window = 100;

    void update(double price)
    {
        if (last_price > 0.0)
        {
            double price_return =
                (price - last_price) / last_price;

            returns.push_back(price_return);

            return_sum += price_return;
            return_square_sum +=
                price_return * price_return;

            if (returns.size() > volatility_window)
            {
                double old_return =
                    returns.front();

                returns.pop_front();

                return_sum -= old_return;
                return_square_sum -=
                    old_return * old_return;
            }
        }

        last_price = price;
    }

    double volatility() const
    {
        if (returns.size() < 2)
        {
            return 0.0;
        }

        double n =
            static_cast<double>(returns.size());

        double mean =
            return_sum / n;

        double variance =
            return_square_sum / n
            - mean * mean;

        return std::sqrt(
            std::max(0.0, variance)
        );
    }

    void record_trade(int tick)
    {
        ++total_trades;

        if (!recent_trade_ticks.empty() && recent_trade_ticks.back().first == tick)
        {
            recent_trade_ticks.back().second += 1;
        }
        else
        {
            recent_trade_ticks.emplace_back(tick, 1);
        }

        trades_last_100_ticks_cached += 1;
        prune_old_trades(tick);
    }

    void prune_old_trades(int current_tick)
    {
        const int cutoff = current_tick - 99;

        while (!recent_trade_ticks.empty() && recent_trade_ticks.front().first < cutoff)
        {
            trades_last_100_ticks_cached -= recent_trade_ticks.front().second;
            recent_trade_ticks.pop_front();
        }
    }

    int trades_last_100_ticks(int current_tick)
    {
        prune_old_trades(current_tick);
        return trades_last_100_ticks_cached;
    }

};

struct ActiveOrder
{
    int instrument_id;
    int trader_id;
    Side side;
    int limit_price;
    int remaining_quantity;
};

struct Observation
{
    double current_price;
    std::optional<int> best_buy;
    std::optional<int> best_sell;
};

struct Market
{
public:
    int submit_order(int instrument_id, int trader_id, Side side, int price, int quantity);
    void add_instrument(int id, std::string name, double starting_price);
    const std::vector<Trade>& get_history() const;
    const std::unordered_map<int, ActiveOrder>& get_active_orders() const;
    std::optional<ActiveOrder> cancel_order(int order_id, int trader_id);
    std::unordered_map<int,Observation> get_last_prices();
    std::map<int, std::string> get_instrument_names();
    std::map<int,int> get_total_trades();

private:
    std::map<int, Instrument> instruments;
    std::vector<Trade> history;
    int next_trade_id{0};
    int next_order_id{0};
    double alpha = 0.1;
    std::unordered_map<int, ActiveOrder> active_orders;
    std::map<int,int> total_trades;
};