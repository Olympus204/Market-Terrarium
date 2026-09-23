#include "market.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

int Market::submit_order(int instrument_id, int trader_id, Side side, int price, int quantity)
{
    if (price <= 0)
    {
        throw std::invalid_argument("Order price must be positive");
    }

    if (quantity <= 0)
    {
        throw std::invalid_argument("Order quantity must be positive");
    }
    auto instrument_it = instruments.find(instrument_id);

    if (instrument_it == instruments.end())
    {
        throw std::invalid_argument("Instrument ID does not exist");
    }
    int order_id = next_order_id;
    next_order_id += 1;
    ActiveOrder active_order = {instrument_id, trader_id, side, price, quantity};
    active_orders[order_id] = active_order;
    Order order = {order_id, trader_id, side, price, quantity};
    auto temp_trades = instrument_it->second.book.process_order(order);
    for(const Temp_trade& temp_trade : temp_trades)
    {
        int trade_id = next_trade_id;
        next_trade_id += 1;
        Trade trade = {trade_id,
                       instrument_id, 
                       temp_trade.buy_id, 
                       temp_trade.sell_id,
                       temp_trade.buyer_id,
                       temp_trade.seller_id,
                       temp_trade.quantity,
                       temp_trade.price};
        history.push_back(trade);
        total_trades[instrument_id] += 1;
        auto buy_it = active_orders.find(temp_trade.buy_id);
        assert(buy_it != active_orders.end());
        buy_it->second.remaining_quantity -= temp_trade.quantity;
        assert(buy_it->second.remaining_quantity >= 0);
        if (buy_it->second.remaining_quantity == 0)
        {
            active_orders.erase(buy_it);
        }
        auto sell_it = active_orders.find(temp_trade.sell_id);
        assert(sell_it != active_orders.end());
        sell_it->second.remaining_quantity -= temp_trade.quantity;
        assert(sell_it->second.remaining_quantity >= 0);
        if (sell_it->second.remaining_quantity == 0)
        {
            active_orders.erase(sell_it);
        }
        instrument_it->second.last_price = instrument_it->second.last_price + alpha * (temp_trade.price - instrument_it->second.last_price);
    }
    return order_id;
}

void Market::add_instrument(int id, std::string name, double estimated_price)
{
    if (id <= 0)
    {
        throw std::invalid_argument("Instrument ID must be positive");
    }
    if (estimated_price <= 0)
    {
        throw std::invalid_argument("Estimated price must be positive");
    }

    auto result = instruments.try_emplace(id, std::move(name), estimated_price);

    if (!result.second)
    {
        throw std::invalid_argument("Instrument ID already exists");
    }
}

std::optional<ActiveOrder> Market::cancel_order(int order_id, int trader_id)
{
    auto it = active_orders.find(order_id);
    if (it == active_orders.end() || trader_id != it->second.trader_id)
    {
        return std::nullopt;
    }
    auto instrument = instruments.find(it->second.instrument_id);
    assert(instrument != instruments.end());
    auto book_order = instrument->second.book.cancel_order(order_id);
    assert(book_order.has_value());
    ActiveOrder cancelled_order = it->second;
    assert(book_order->quantity == cancelled_order.remaining_quantity);
    active_orders.erase(it);
    return cancelled_order;
}

const std::vector<Trade>& Market::get_history() const
{
    return history;
}

const std::unordered_map<int, ActiveOrder>& Market::get_active_orders() const
{
    return active_orders;
}

std::unordered_map<int,double> Market::get_last_prices()
{
    std::unordered_map<int,double> estimated_prices;
    for (auto [id, instrument] : instruments)
    {
        estimated_prices[id] = instrument.last_price;
    }
    return estimated_prices;
}

std::map<int, std::string> Market::get_instrument_names()
{
    std::map<int, std::string> names;
    for (auto& [id, instrument] : instruments)
    {
        names[id] = instrument.name;
    }
    return names;
}

std::map<int,int> Market::get_total_trades()
{
    return total_trades;
}