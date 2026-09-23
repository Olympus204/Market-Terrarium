#include "trader.hpp"

#include <map>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <stdexcept>

SimpleTrader::SimpleTrader(int trader_id, std::int64_t starting_cash, TraderType trader_type)
    : id(trader_id),
      cash(starting_cash),
      reserved_cash(0),
      type(trader_type)
{
    if (starting_cash <= 0)
    {
        throw std::invalid_argument("Starting cash must be positive");
    }
}

bool SimpleTrader::increment_health()
{
    if (trader_health == Health::healthy)
    {
        trader_health = Health::recovering;
        return true;
    }
    else if (trader_health == Health::recovering)
    {
        trader_health = Health::critical;
        return true;
    }
    else if (trader_health == Health::critical)
    {
        trader_health = Health::failed;
        return false;
    }
    return false;
}

void SimpleTrader::reset_health()
{
    trader_health = Health::healthy;
}

Health SimpleTrader::get_health()
{
    return trader_health;
}

TraderType SimpleTrader::get_trader_type() const
{
    return type;
}

std::int64_t SimpleTrader::get_available_cash() const
{
    std::int64_t available = cash - reserved_cash;
    return available;
}

std::int64_t SimpleTrader::get_total_cash() const
{
    return cash;
}

int SimpleTrader::get_available_holding(int instrument_id) const
{
    auto it = positions.find(instrument_id);
    if (it == positions.end())
    {
        return 0;
    }
    int current = it->second.quantity;

    const int reserved = this->reserved_holdings.contains(instrument_id)
        ? this->reserved_holdings.at(instrument_id)
        : 0;

    return current - reserved;
}

int SimpleTrader::get_total_holding(int instrument_id) const
{
    auto it = positions.find(instrument_id);
    if (it == positions.end())
    {
        return 0;
    }
    int current = it->second.quantity;
    return current;
}

const std::unordered_map<int, TraderOrder>& SimpleTrader::get_active_orders() const
{
    return active_orders;
}

const std::unordered_map<int, Position>& SimpleTrader::get_current_positions() const
{
    return positions;
}

bool SimpleTrader::reserve_cash(std::int64_t amount)
{
    if (amount <= 0)
    {
        return false;
    }
    std::int64_t available_cash = get_available_cash();
    if(available_cash < amount)
    {
        return false;
    }
    else
    {
        reserved_cash += amount;
        return true;
    }
}

bool SimpleTrader::release_cash(std::int64_t amount)
{
    if (amount <= 0)
    {
        return false;
    }
    if(reserved_cash < amount)
    {
        return false;
    }
    else
    {
        reserved_cash -= amount;
        return true;
    }
}

bool SimpleTrader::reserve_holding(int instrument_id, int quantity)
{
    if (quantity <= 0)
    {
        return false;
    }
    int available_holdings = get_available_holding(instrument_id);
    if(available_holdings < quantity)
    {
        return false;
    }
    else
    {
        reserved_holdings[instrument_id] += quantity;
        return true;
    }
}

bool SimpleTrader::release_holding(int instrument_id, int quantity)
{
    if (quantity <= 0)
    {
        return false;
    }

    auto it = reserved_holdings.find(instrument_id);

    if (it == reserved_holdings.end() || it->second < quantity)
    {
        return false;
    }

    it->second -= quantity;

    if (it->second == 0)
    {
        reserved_holdings.erase(it);
    }

    return true;
}

void SimpleTrader::add_holding(int instrument_id, int quantity, int price)
{
    assert(quantity > 0);
    auto position = positions.find(instrument_id);
    if (position == positions.end())
    {
        positions[instrument_id].quantity = quantity;
        positions[instrument_id].cost_basis = quantity * static_cast<std::int64_t>(price);
    }
    else
    {
        positions[instrument_id].quantity += quantity;
        positions[instrument_id].cost_basis += quantity * static_cast<std::int64_t>(price);
    }
}

bool SimpleTrader::settle_buy(int order_id, int execution_price, int quantity)
{
    if (quantity <= 0 || execution_price <= 0)
    {
        return false;
    }
    auto it = active_orders.find(order_id);
    if(it == active_orders.end())
    {
        return false;
    }
    int instrument_id = it->second.instrument_id;
    std::int64_t total_spent = static_cast<std::int64_t>(execution_price) * quantity;
    std::int64_t reserved_amount = static_cast<std::int64_t>(it->second.limit_price) * quantity;
    if(it->second.remaining_quantity < quantity || it->second.side != Side::buy || execution_price > it->second.limit_price || reserved_cash < reserved_amount)
    {
        return false;
    }
    bool released = release_cash(reserved_amount);
    assert(released);
    cash -= total_spent;
    add_holding(instrument_id, quantity, execution_price);
    it->second.remaining_quantity -= quantity;
    assert(it->second.remaining_quantity >= 0);
    if(it->second.remaining_quantity == 0)
    {
        active_orders.erase(it);
    }
    return true;
}

bool SimpleTrader::settle_sell(int order_id, int execution_price, int quantity)
{
    if (quantity <= 0 || execution_price <= 0)
    {
        return false;
    }
    auto it = active_orders.find(order_id);
    if(it == active_orders.end())
    {
        return false;
    }
    if (quantity > it->second.remaining_quantity)
    {
        return false;
    }
    int instrument_id = it->second.instrument_id;
    std::int64_t total_earned = static_cast<std::int64_t>(execution_price) * quantity;
    auto reserved_it = reserved_holdings.find(instrument_id);

    if (reserved_it == reserved_holdings.end())
    {
        return false;
    }
    if(quantity > reserved_it->second || it->second.side != Side::sell || execution_price < it->second.limit_price)
    {
        return false;
    }
    auto position = positions.find(instrument_id);

    if (position == positions.end() || position->second.quantity < quantity)
    {
        return false;
    }
    bool released = release_holding(instrument_id, quantity);
    assert(released);

    position->second.cost_basis -= position->second.cost_basis * quantity / position->second.quantity;
    position->second.quantity -= quantity;
    if (position->second.quantity == 0)
    {
        positions.erase(position);
    }
    cash += total_earned;
    it->second.remaining_quantity -= quantity;
    assert(it->second.remaining_quantity >= 0);
    if (it->second.remaining_quantity == 0)
    {
        active_orders.erase(it);
    }
    return true;
}

bool SimpleTrader::confirm_order(int order_id, int instrument_id, Side side, int limit_price, int quantity)
{
    if (order_id < 0 || instrument_id <= 0 || limit_price <= 0 || quantity <= 0)
    {
        return false;
    }
    if (active_orders.contains(order_id))
    {
        return false;
    }
    active_orders[order_id] = {instrument_id, side, limit_price, quantity};
    return true;
}

bool SimpleTrader::confirm_cancel(int order_id, int instrument_id, Side side, int limit_price, int quantity)
{
    if (order_id < 0 || instrument_id <= 0 || limit_price <= 0 || quantity <= 0)
    {
        return false;
    }
    auto it = active_orders.find(order_id);
    if(it == active_orders.end())
    {
        return false;
    }
    if(it->second.instrument_id != instrument_id || it->second.limit_price != limit_price || it->second.side != side || it->second.remaining_quantity != quantity)
    {
        return false;
    }
    if (side == Side::sell)
    {
        bool confirm = release_holding(instrument_id, quantity);
        if (!confirm)
        {
            return false;
        }
        active_orders.erase(it);
        return true;
    }
    if (side == Side::buy)
    {
        std::int64_t reserved_amount = static_cast<std::int64_t>(it->second.limit_price) * quantity;
        bool confirm = release_cash(reserved_amount);
        if (!confirm)
        {
            return false;
        }
        active_orders.erase(it);
        return true;
    }
    return false;
}

void SimpleTrader::update_observed_prices(const std::unordered_map<int, double>& prices)
{
    for (const auto& [id, price] : prices)
    {
        auto& history = observed_prices[id];
        history.push_back(price);
        if (history.size() > memory)
        {
            history.pop_front();
        }
    }
}

std::deque<double> SimpleTrader::observed_price(int instrument_id) const
{
    auto it = observed_prices.find(instrument_id);
    if (it == observed_prices.end())
    {
        throw std::logic_error("instrument not found");
    }
    return it->second;
}

const std::unordered_map<int, std::deque<double>>& SimpleTrader::get_observed_prices() const
{
    return observed_prices;
}

TraderDecision SimpleTrader::bankruptcy_check()
{
    std::int64_t shortfall = -get_available_cash();
    std::int64_t reserved_value{0};
    std::int64_t gap{0};
    std::int64_t best_covering_gap{0};
    int best_covering_order{0};
    bool is_covering_order = false;
    std::int64_t best_fallback_gap{0};
    int best_fallback_order{0};
    bool is_fallback_order = false;
    auto& orders = get_active_orders();
    for (auto& entry : orders)
    {
        TraderOrder order = entry.second;  
        if (order.side == Side::buy)
        {
            reserved_value = static_cast<std::int64_t>(order.limit_price) * order.remaining_quantity;
            gap = reserved_value - shortfall;
            if (gap >= 0)
            {
                if (!is_covering_order)
                {
                    best_covering_gap = gap;
                    best_covering_order = entry.first;
                    is_covering_order = true;
                }
                else if (gap < best_covering_gap)
                {
                    best_covering_gap = gap;
                    best_covering_order = entry.first;
                }
            }
            if (gap < 0)
            {
                if (!is_fallback_order)
                {
                    best_fallback_gap = gap;
                    best_fallback_order = entry.first;
                    is_fallback_order = true;
                }
                else if(gap > best_fallback_gap)
                {
                    best_fallback_gap = gap;
                    best_fallback_order = entry.first;
                }
            }        
        }
        
    }
    if (is_covering_order)
    {
        return TraderDecision{ActionType::cancel,best_covering_order, 0, 0, 0};
    }
    else if (is_fallback_order)
    {
        return TraderDecision{ActionType::cancel,best_fallback_order, 0, 0, 0};
    }
    else
    {
        // sell holdings here

        int best_covering_id = 0;
        std::int64_t best_covering_gap = 0;
        int best_quantity_needed = 0;
        int best_liquidation = 0;
        bool is_covering = false;
        int best_fallback_id=0;
        std::int64_t best_fallback_gap = 0;
        int fallback_quantity = 0;
        int fallback_liquidation = 0;
        bool is_fallback = false;
        for (const auto& [instrument_id, position] : positions)
        {
            auto it = reserved_holdings.find(instrument_id);
            int reserved_ammount = 0;
            if (it != reserved_holdings.end())
            {
                reserved_ammount = it->second;
            } 
            int available_ammount = position.quantity - reserved_ammount;
            auto price = observed_prices.find(instrument_id);
            if (price == observed_prices.end())
            {
                throw std::logic_error("observed prices missing instrument");
            }
            int liquidation_price = std::ceil(price->second.back() * 0.9);
            std::int64_t quantity_needed = (shortfall + liquidation_price - 1) / liquidation_price;
            if (available_ammount >= quantity_needed)
            {
                std::int64_t covering_gap = quantity_needed * liquidation_price - shortfall;
                if (!is_covering)
                {
                    best_covering_id = instrument_id;
                    best_covering_gap = covering_gap;
                    best_quantity_needed = quantity_needed;
                    best_liquidation = liquidation_price;
                    is_covering = true;
                }
                else if (covering_gap < best_covering_gap)
                {
                    best_covering_id = instrument_id;
                    best_covering_gap = covering_gap;
                    best_quantity_needed = quantity_needed;
                    best_liquidation = liquidation_price;
                }
            }
            else
            {
                std::int64_t fallback_gap = shortfall - available_ammount * static_cast<std::int64_t>(liquidation_price);
                if (!is_fallback && available_ammount > 0)
                {
                    best_fallback_id = instrument_id;
                    best_fallback_gap = fallback_gap;
                    fallback_quantity = available_ammount;
                    fallback_liquidation = liquidation_price;
                    is_fallback = true;
                }
                else if (fallback_gap < best_fallback_gap)
                {
                    best_fallback_id = instrument_id;
                    best_fallback_gap = fallback_gap;
                    fallback_quantity = available_ammount;
                    fallback_liquidation = liquidation_price;
                }
            }
        }
        if (is_covering)
        {
            return TraderDecision{ActionType::sell, 0, best_covering_id, best_quantity_needed, best_liquidation};
        }
        else if(is_fallback)
        {
            return TraderDecision{ActionType::sell, 0, best_fallback_id, fallback_quantity, fallback_liquidation};
        }
    }
    // you are fucked
    return TraderDecision{ActionType::none,0,0,0,0};
}

bool SimpleTrader::apply_cost(std::int64_t amount)
{
    if (amount <= 0)
    {
        return false;
    }
    cash -= amount;
    return true;
}

bool SimpleTrader::apply_payment(std::int64_t amount)
{
    if (amount <= 0)
    {
        return false;
    }
    cash += amount;
    return true;
}

bool SimpleTrader::remove_holding(int instrument_id, int quantity)
{
    auto holding = positions.find(instrument_id);
    if (holding == positions.end() || quantity > holding->second.quantity)
    {
        return false;
    }
    holding->second.quantity -= quantity;
    if (holding->second.quantity <= 0)
    {
        positions.erase(holding);
    }
    return true;
}