#pragma once

#include "order.hpp"
#include "neural_genome.hpp"
#include "market.hpp"

#include <cstdint>
#include <unordered_map>
#include <deque>
#include <array>
#include <random>

struct TraderOrder
{
    int instrument_id;
    Side side;
    int limit_price;
    int remaining_quantity;
};

enum class Health
{
    healthy,
    recovering,
    critical,
    failed,
};

enum class ActionType
{
    none,
    buy,
    sell,
    cancel,
};

enum class TraderType
{
    random,
    portfolio_rebalancer,
    mean_value,
    neural_evolution,
    none,
};

struct TraderDecision
{
    ActionType type;
    int order_id;
    int instrument_id;
    int quantity;
    std::int64_t price;
};

struct Position
{
    int quantity;
    std::int64_t cost_basis;
};

struct SimpleTrader
{
public:
    SimpleTrader(int trader_id, std::int64_t starting_cash, TraderType type, int current_tick = 0);

    bool increment_health();
    void reset_health();
    Health get_health(); 

    TraderType get_trader_type() const;

    std::int64_t get_available_cash() const;
    std::int64_t get_total_cash() const;
    int get_available_holding(int instrument_id) const;
    int get_total_holding(int instrument_id) const;

    std::int64_t get_total_worth() const;
    bool record_total_worth(std::int64_t new_total_worth);

    std::optional<std::int64_t> get_worth_change();

    const std::unordered_map<int, TraderOrder>& get_active_orders() const;
    const std::unordered_map<int, Position>& get_current_positions() const;

    bool reserve_cash(std::int64_t amount);
    bool release_cash(std::int64_t amount);

    bool reserve_holding(int instrument_id, int quantity);
    bool release_holding(int instrument_id, int quantity);

    void add_holding(int instrument_id, int quantity, int price);

    bool settle_buy(int order_id, int execution_price, int quantity);
    bool settle_sell(int order_id, int execution_price, int quantity);

    bool confirm_order(int order_id, int instrument_id, Side side, int limit_price, int quantity);
    bool confirm_cancel(int order_id, int instrument_id, Side side, int limit_price, int quantity);

    void update_observed_prices(const std::unordered_map<int, Observation>& prices);
    std::deque<Observation> observed_price(int instrument_id) const;
    const std::unordered_map<int, std::deque<Observation>>& get_observed_prices() const;

    TraderDecision bankruptcy_check();

    bool apply_cost(std::int64_t amount);
    bool apply_payment(std::int64_t amount);

    std::int64_t calculate_taxes();
    void record_tax_season();

    bool remove_holding(int instrument_id, int quantity);
    void record_death(int current_tick);
    int get_age(int current_tick) const;

    bool set_neural_genome(NeuralGenome neural_genome);
    NeuralGravestone write_gravestone();
    const NeuralGenome& get_genome() const;
    void update_memory(std::map<int,double> memory_1, std::map<int,double> memory_2, std::map<int,double> memory_3, std::map<int,double> memory_4);
    void learn(std::int64_t median_this_epoch, std::int64_t median_last_epoch);
private:
    int id;
    int birth_tick = 0;
    int death_tick = 0;
    Health trader_health{Health::healthy};
    std::int64_t cash;
    std::int64_t reserved_cash{0};
    int memory = 20;

    std::int64_t worth_last_tax_season = 0;

    std::int64_t total_worth = 0;
    std::int64_t total_worth_last_epoch = 0;
    bool is_worth_last_epoch = false;
    bool is_worth_current_epoch = false;

    TraderType type;
    NeuralGenome genome;

    std::unordered_map<int, TraderOrder> active_orders;
    std::unordered_map<int, Position> positions;
    std::unordered_map<int, int> reserved_holdings;
    std::unordered_map<int, std::deque<Observation>> observed_prices;
};