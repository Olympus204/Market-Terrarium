#pragma once

#include "order_book.hpp"
#include "trades.hpp"
#include "trader.hpp"

#include <vector>
#include <map>
#include <unordered_map>
#include <string>

struct Instrument
{
    std::string name;
    double last_price;
    OrderBook book;
};

struct ActiveOrder
{
    int instrument_id;
    int trader_id;
    Side side;
    int limit_price;
    int remaining_quantity;
};

struct Market
{
public:
    int submit_order(int instrument_id, int trader_id, Side side, int price, int quantity);
    void add_instrument(int id, std::string name, double starting_price);
    const std::vector<Trade>& get_history() const;
    const std::unordered_map<int, ActiveOrder>& get_active_orders() const;
    std::optional<ActiveOrder> cancel_order(int order_id, int trader_id);
    std::unordered_map<int,double> get_last_prices();
    std::map<int, std::string> get_instrument_names();
    std::map<int,int> get_total_trades();

private:
    std::map<int, Instrument> instruments;
    std::vector<SimpleTrader> traders;
    std::vector<Trade> history;
    int next_trade_id{0};
    int next_order_id{0};
    double alpha = 0.1;
    std::unordered_map<int, ActiveOrder> active_orders;
    std::map<int,int> total_trades;
};