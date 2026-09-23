#pragma once

#include "order.hpp"
#include "trades.hpp"

#include <vector>
#include <algorithm>
#include <stdexcept>
#include <map>
#include <deque>
#include <unordered_map>
#include <functional>
#include <optional>

struct OrderLocation
{
    Side side;
    int price;
};

using PriceLevel = std::deque<Order>;

using BuyBook =
    std::map<int, PriceLevel, std::greater<int>>;

using SellBook =
    std::map<int, PriceLevel>;


struct OrderBook
{
public:
    std::vector<Temp_trade> process_order(Order incoming);
    std::optional<Order> cancel_order(int order_id);
    const BuyBook& get_buys() const;
    const SellBook& get_sells() const;
    const Order get_last_sale();

private:
    BuyBook buys;
    SellBook sells;
    std::vector<int> used_ids;
    std::unordered_map<int, OrderLocation> active_orders;
};