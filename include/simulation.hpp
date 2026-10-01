#pragma once

#include "market.hpp"
#include "trader.hpp"
#include "order.hpp"

#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <deque>

struct OldTraderSnapshot
{
    int trader_id = 0;
    int age = 0;

    std::int64_t cash = 0;
    std::int64_t wealth = 0;

    std::map<int, int> holdings;
};

struct SimulationSnapshot
{
    int seed;
    int tick;
    //instruments
    std::vector<int> instrument_ids;
    std::map<int,std::string> instrument_names;
    std::unordered_map<int,Observation> instrument_reference_price;
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

    int neural_evolution;
    std::int64_t neural_evolution_cash;
    std::int64_t neural_evolution_portfolio_value;
    double neural_evolution_cash_fraction;
    std::map<int,int> neural_evolution_percentage_of_each_instrument;
    std::vector<OldTraderSnapshot> oldest_neural_traders;

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
    int neural_evolution_deaths = 0;
    int longest_neural_lifespan = 0;
    int replacements;
    int active_orders;
};

struct Simulation
{
public:
    Simulation(std::uint64_t seed, int total_cash);
    void snapshot_prices();

    void set_living_parent_selection(bool enabled);
    std::vector<NeuralParentCandidate> get_neural_parent_candidates(int child_id) const;
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
    void set_tax_frequency(bool enabled, int tax_year_length);

    SimulationSnapshot get_snapshot();

    void set_bank_recycling(std::int64_t target, int frequency, double fraction);
    int get_total_trades() const;
private:
    std::mt19937_64 rng;
    int seed;
    Market market;
    std::map<int,SimpleTrader> traders;
    std::vector<int> active_traders;
    std::unordered_map<int, size_t> active_trader_index;
    std::deque<TraderType> traders_to_add;

    std::int64_t median_worth = 0;
    std::int64_t median_worth_last_epoch = 0;
    bool is_median_worth = false;
    bool is_median_worth_last_epoch = false;

    int epoch_size = 50;
    bool can_learn = true;
    bool allow_living_parents = false;

    int last_used_id = 0;
    int current_tick = 1;

    std::map<TraderType,int> deaths;
    std::multimap<int, NeuralGravestone, std::greater<int>> cemetery;
    int replacements = 0;

    int cost_frequency=10;
    std::int64_t cost_amount=100;
    std::int64_t starting_amount = 1000;
    std::int64_t bank_redistributed_this_tick = 0;
    std::int64_t reserve_target = 20000;
    bool is_taxes = false;
    int tax_season = 2000;

    std::size_t next_unsettled_trade = 0;
    std::unordered_map<int, Observation> visible_prices;

    std::unordered_map<TraderType, int> type_count;

    int recycle_frequency = 1;
    double recycle_fraction = 0.1;
};