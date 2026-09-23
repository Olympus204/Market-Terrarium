#include "order_book.hpp"

#include <algorithm>
#include <cassert>

std::vector<Temp_trade> OrderBook::process_order(Order incoming)
{
    std::vector<Temp_trade> trades;
    if (incoming.side == Side::buy)
    {
        while (incoming.quantity > 0 and !sells.empty() and incoming.price >= sells.begin()->first)
        {
            int amount = std::min(incoming.quantity, sells.begin()->second.front().quantity);
            incoming.quantity -= amount;
            sells.begin()->second.front().quantity -= amount;
            trades.push_back({incoming.id, sells.begin()->second.front().id, incoming.trader_id, sells.begin()->second.front().trader_id, amount, sells.begin()->second.front().price});
            if (sells.begin()->second.front().quantity == 0)
            {
                active_orders.erase(sells.begin()->second.front().id);
                sells.begin()->second.pop_front();
                if (sells.begin()->second.empty())
                {
                    sells.erase(sells.begin());
                }
            }
        }
        if (incoming.quantity > 0)
        {
            buys[incoming.price].push_back(incoming);
            active_orders[incoming.id] = {incoming.side, incoming.price};
        }
    }
    else
    {
        while (incoming.quantity > 0 and !buys.empty() and incoming.price <= buys.begin()->first)
        {
            int amount = std::min(incoming.quantity, buys.begin()->second.front().quantity);
            incoming.quantity -= amount;
            buys.begin()->second.front().quantity -= amount;
            trades.push_back({buys.begin()->second.front().id, incoming.id, buys.begin()->second.front().trader_id, incoming.trader_id, amount, buys.begin()->second.front().price});
            if (buys.begin()->second.front().quantity == 0)
            {
                active_orders.erase(buys.begin()->second.front().id);
                buys.begin()->second.pop_front();
                if (buys.begin()->second.empty())
                {
                    buys.erase(buys.begin());
                }
            }
        }
        if (incoming.quantity > 0)
        {
            sells[incoming.price].push_back(incoming);
            active_orders[incoming.id] = {incoming.side, incoming.price};
        }
    }
    return trades;
}

std::optional<Order> OrderBook::cancel_order(int order_id)
{
    auto location_it = active_orders.find(order_id);
    if (location_it == active_orders.end())
    {
        return std::nullopt;
    }

    const Side side = location_it->second.side;
    const int price = location_it->second.price;

    auto remove_from_book = [&](auto& book) -> std::optional<Order>
    {
        auto level_it = book.find(price);
        assert(level_it != book.end());

        auto& orders = level_it->second;
        auto order_it = std::find_if(
            orders.begin(),
            orders.end(),
            [order_id](const Order& order)
            {
                return order.id == order_id;
            });

        if (order_it == orders.end())
        assert(order_it != orders.end());

        Order removed = *order_it;
        active_orders.erase(location_it);
        orders.erase(order_it);

        if (orders.empty())
        {
            book.erase(price);
        }

        return removed;
    };

    if (side == Side::sell)
    {
        return remove_from_book(sells);
    }

    return remove_from_book(buys);
}

const BuyBook& OrderBook::get_buys() const
{
    return buys;
}

const SellBook& OrderBook::get_sells() const
{
    return sells;
}