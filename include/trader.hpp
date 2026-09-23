#pragma once

#include "order.hpp"

#include <cstdint>
#include <unordered_map>
#include <deque>

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
    SimpleTrader(int trader_id, std::int64_t starting_cash, TraderType type);

    bool increment_health();
    void reset_health();
    Health get_health(); 

    TraderType get_trader_type() const;

    std::int64_t get_available_cash() const;
    std::int64_t get_total_cash() const;
    int get_available_holding(int instrument_id) const;
    int get_total_holding(int instrument_id) const;

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

    void update_observed_prices(const std::unordered_map<int, double>& prices);
    std::deque<double> observed_price(int instrument_id) const;
    const std::unordered_map<int, std::deque<double>>& get_observed_prices() const;

    TraderDecision bankruptcy_check();

    bool apply_cost(std::int64_t amount);
    bool apply_payment(std::int64_t amount);

    bool remove_holding(int instrument_id, int quantity);

private:
    int id;
    Health trader_health{Health::healthy};
    std::int64_t cash;
    std::int64_t reserved_cash{0};
    int memory = 40;

    TraderType type;

    std::unordered_map<int, TraderOrder> active_orders;
    std::unordered_map<int, Position> positions;
    std::unordered_map<int, int> reserved_holdings;
    std::unordered_map<int, std::deque<double>> observed_prices;
};