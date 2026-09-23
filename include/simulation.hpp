#pragma once

#include "market.hpp"
#include "trader.hpp"
#include "order.hpp"

#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <deque>

struct SimulationSnapshot
{
    int tick;
    //instruments
    std::vector<int> instrument_ids;
    std::map<int,std::string> instrument_names;
    std::unordered_map<int,double> instrument_reference_price;
    std::unordered_map<int,int> last_execution_price;
    std::map<int,int> total_trades_per_instrument;
    //population
    int active_total_traders;
    int random;
    std::int64_t random_cash;
    std::int64_t random_portfolio_value;
    double random_cash_fraction;
    std::map<int,int> random_percentage_of_each_instrument;

    int mean_reversion;
    std::int64_t mean_reversion_cash;
    std::int64_t mean_reversion_portfolio_value;
    double mean_reversion_cash_fraction;
    std::map<int,int> mean_reversion_percentage_of_each_instrument;

    int portfolio_rebalancer;
    std::int64_t portfolio_rebalancer_cash;
    std::int64_t portfolio_rebalancer_portfolio_value;
    double portfolio_rebalancer_cash_fraction;
    std::map<int,int> portfolio_rebalancer_percentage_of_each_instrument;

    //bank
    std::int64_t bank_cash;
    std::int64_t bank_redistributed_this_tick;
    std::map<int,int> bank_holdings;
    std::map<int,int> bank_reserved_holdings;
    std::map<int,int> bank_percentage_of_each_instrument;
    //activity
    int total_trades;
    int total_deaths = 0;
    int random_deaths = 0;
    int mean_reversion_deaths = 0;
    int portfolio_rebalancer_deaths = 0;
    int replacements;
    int active_orders;
};

struct Simulation
{
public:
    Simulation(std::uint64_t seed, int total_cash);
    void snapshot_prices();
    bool add_trader(int64_t starting_money, TraderType type);
    bool queue_trader(TraderType type);
    bool add_instrument(int id, std::string name, int starting_price);
    void settle_accounts();
    bool submit_order(int trader_id, int instrument_id, Side side, int quantity, int price);
    bool cancel_order(int order_id, int trader_id);
    bool introduce_holdings(int instrument_id, int quantity);
    bool reserve_order(int trader_id, int instrument_id, Side side, int quantity, int price);
    bool charge_trader(int trader_id, std::int64_t amount);
    bool liquidate_trader(int trader_id);
    void tick();
    Health get_trader_health(int trader_id);
    std::int64_t get_trader_available_cash(int trader_id) const;
    int get_trader_available_holdings(int trader_id, int instrument_id) const;
    int get_trader_total_holdings(int trader_id, int instrument_id) const;
    const std::unordered_map<int, Position>& get_current_trader_positions(int trader_id) const;
    const std::unordered_map<int, TraderOrder>& get_trader_active_orders(int trader_id) const;
    void set_recurring_costs(int frequency, std::int64_t amount);
    void set_starting_amount(std::int64_t cash);
    SimulationSnapshot get_snapshot();
    void set_bank_recycling(std::int64_t target, int frequency, double fraction);
private:
    std::mt19937_64 rng;
    Market market;
    std::map<int,SimpleTrader> traders;
    std::vector<int> active_traders;
    std::unordered_map<int, size_t> active_trader_index;
    std::vector<TraderType> traders_to_add;
    int last_used_id = 0;
    int current_tick = 1;
    std::map<TraderType,int> deaths;
    int replacements = 0;
    int cost_frequency=10;
    std::int64_t cost_amount=100;
    std::int64_t starting_amount = 1000;
    std::int64_t bank_redistributed_this_tick = 0;
    std::size_t next_unsettled_trade = 0;
    std::unordered_map<int, double> visible_prices;
    std::unordered_map<TraderType, int> type_count;
    std::int64_t reserve_target = 10000;
    int recycle_frequency = 1;
    double recycle_fraction = 0.1;
};