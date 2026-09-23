#include "simulation.hpp"
#include "trader.hpp"
#include "trades.hpp"
#include "decisions.hpp"

#include <stdexcept>
#include <cmath>
#include <vector>
#include <algorithm>

Simulation::Simulation(std::uint64_t seed, int total_cash)
    : rng(seed)
{
    traders.emplace(0, SimpleTrader{0, total_cash, TraderType::random});
}

void Simulation::snapshot_prices()
{
    std::unordered_map<int,double> last_prices = market.get_last_prices();
    visible_prices = last_prices;
}

bool Simulation::add_trader(int64_t starting_money, TraderType type)
{
    if(starting_money <= 0)
    {
        return false;
    }
    ++last_used_id;
    auto it = traders.emplace(last_used_id, SimpleTrader{last_used_id, starting_money, type});
    if (it.second == false)
    {
        return false;
    }
    auto bank = traders.find(0);
    if (bank == traders.end())
    {
        throw std::logic_error("bank no longer exists");
    }
    bool bank_agrees = bank->second.apply_cost(starting_money);
    if (!bank_agrees)
    {
        return false;
    }
    active_trader_index.emplace(last_used_id, active_traders.size());
    active_traders.push_back(last_used_id);
    type_count[type] += 1;
    return true;
}

bool Simulation::queue_trader(TraderType type)
{
    traders_to_add.push_back(type);
    return true;
}

bool Simulation::add_instrument(int id, std::string name, int starting_price)
{
    market.add_instrument(id, name ,starting_price);
    visible_prices.emplace(id, starting_price);
    return true;
}

void Simulation::settle_accounts()
{
    const std::vector<Trade>& history = market.get_history();

    while (next_unsettled_trade < history.size())
    {
        const Trade& current = history[next_unsettled_trade];
        auto buyer = traders.find(current.buyer_id);
        if (buyer == traders.end())
        {
            throw std::logic_error("buyer does not exist");
        }
        
        auto seller = traders.find(current.seller_id);
        if (seller == traders.end())
        {
            throw std::logic_error("seller does not exist");
        }
        bool buyer_confirm = buyer->second.settle_buy(current.buy_id, current.price, current.quantity);
        bool seller_confirm = seller->second.settle_sell(current.sell_id, current.price, current.quantity);
        if (!buyer_confirm || !seller_confirm)
        {
            throw std::logic_error("settlement failed");
        }
        next_unsettled_trade += 1;
    }
}

bool Simulation::submit_order(int trader_id, int instrument_id, Side side, int quantity, int price)
{
    // assumes trader has succesfully reserved assets for this order
    auto it = traders.find(trader_id);
    if(it == traders.end())
    {
        return false;
    }
    if(price <= 0 ||quantity <= 0)
    {
        return false;
    }
    int order_id = 0;
    try
    {
        order_id = market.submit_order(instrument_id,trader_id,side,price,quantity);
    }
    catch(const std::invalid_argument&)
    {
        if (side == Side::buy)
        {
            std::int64_t reserved_amount = static_cast<std::int64_t>(price) * quantity;
            bool required = it->second.release_cash(reserved_amount);
            if (!required)
            {
                throw std::logic_error("Market rejected order but trader reservation could not be released");
            }
        }
        if (side == Side::sell)
        {
            bool required = it->second.release_holding(instrument_id, quantity);
            if (!required)
            {
                throw std::logic_error("Market rejected order but trader reservation could not be released");
            }
        }
        return false;
    }
    bool required = it->second.confirm_order(order_id, instrument_id, side, price, quantity);
    if (!required)
    {
        throw std::logic_error("Market accepted order but trader could not confirm it");
    }
    settle_accounts();
    return true;
}

bool Simulation::cancel_order(int order_id, int trader_id)
{
    if (order_id < 0)
    {
        return false;
    }
    auto order = market.cancel_order(order_id,trader_id);
    if (order == std::nullopt)
    {
        return false;
    }
    auto it = traders.find(order->trader_id);
    if (it == traders.end())
    {
        throw std::logic_error("Trader not found after order cancelled");
    }
    bool confirmation = it->second.confirm_cancel(order_id, order->instrument_id, order->side, order->limit_price, order->remaining_quantity);
    if (!confirmation)
    {
        throw std::logic_error("Trader failed to cancel order");
    }
    return true;
}

bool Simulation::introduce_holdings(int instrument_id, int quantity)
{
    if (quantity <= 0)
    {
        return false;
    }
    auto it = traders.find(0);
    if(it == traders.end())
    {
        SimpleTrader bank{0,1,TraderType::random};
        traders.emplace(0, bank);
    }
    it = traders.find(0);
    int sell_price = visible_prices.at(instrument_id);
    it->second.add_holding(instrument_id,quantity,0);
    bool required_1 = it->second.reserve_holding(instrument_id,quantity);
    if (!required_1)
        {
            throw std::logic_error("bank failed to reserve order");
        }
    bool required_2 = submit_order(0,instrument_id,Side::sell,quantity,sell_price);
    if (!required_2)
        {
            throw std::logic_error("bank failed confirm order");
        }
    return true;
}

bool Simulation::reserve_order(int trader_id, int instrument_id, Side side, int quantity, int price)
{
    if(instrument_id <= 0 || quantity <= 0 || price <= 0)
    {
        return false;
    }
    auto it = traders.find(trader_id);
    if (it == traders.end())
    {
        return false;
    }
    if (side == Side::buy)
    {
        std::int64_t reserved_amount = static_cast<std::int64_t>(price) * quantity;
        return it->second.reserve_cash(reserved_amount);
    }
    if (side == Side::sell)
    {
        return it->second.reserve_holding(instrument_id,quantity);
    }
    return false;
}

bool Simulation::charge_trader(int trader_id, std::int64_t amount)
{
    auto trader = traders.find(trader_id);
    if (trader == traders.end())
    {
        return false;
    }
    bool charged = trader->second.apply_cost(amount);
    if (!charged)
    {
        return false;
    }
    auto bank = traders.find(0);
    if (bank == traders.end())
    {
        traders.emplace(0,SimpleTrader{0,amount,TraderType::random});
    }
    else
    {
        bank->second.apply_payment(amount);
    }
    return true;
}

bool Simulation::liquidate_trader(int trader_id)
{
    auto trader = traders.find(trader_id);
    if (trader == traders.end())
    {
        return false;
    }
    std::unordered_map<int, TraderOrder> active_orders = trader->second.get_active_orders();
    for (auto& [id, order] : active_orders)
    {
        bool cancelled = cancel_order(id, trader_id);
        if (!cancelled)
        {
            throw std::logic_error("failed to cancel order");
        }
    }
    std::int64_t total_cash = trader->second.get_total_cash();
    if (total_cash > 0)
    {
        bool charged = charge_trader(trader_id, total_cash);
        if (!charged)
        {
            throw std::logic_error("Trader failed to pay fines");
        }
    }
    else if (total_cash < 0)
    {
        std::int64_t debt = -total_cash;
        trader->second.apply_payment(debt);
        auto bank = traders.find(0);
        if (bank == traders.end())
        {
            throw std::logic_error("Bank does not exist");
        }
        bank->second.apply_cost(debt);
    }
    std::unordered_map<int, Position> positions = trader->second.get_current_positions();
    for (auto& [id, position] : positions)
    {
        bool removed = trader->second.remove_holding(id,position.quantity);
        if (!removed)
        {
            throw std::logic_error("failed to remove holding");
        }
        auto bank = traders.find(0);
        if (bank == traders.end())
        {
            throw std::logic_error("Bank does not exist");
        }
        bank->second.add_holding(id, position.quantity, 0);
    }
    const std::size_t index_position = active_trader_index.at(trader_id);
    std::size_t last_index = active_traders.size() - 1;
    if (index_position != last_index)
    {
        int last_id = active_traders.back();
        std::swap(active_traders[index_position], active_traders.back());
        active_trader_index[last_id] = index_position;
    }

    active_traders.pop_back();
    active_trader_index.erase(trader_id);
    return true;
}

void Simulation::tick()
{
    while (!traders_to_add.empty())
        {
            if (traders.at(0).get_available_cash() < starting_amount)
            {
                break;
            }
            TraderType type = traders_to_add.back();
            if (!add_trader(starting_amount,type))
            {
                throw std::logic_error("failed to add trader");
            }
            traders_to_add.pop_back();
            ++ replacements;
            
        }
    auto bank_orders = traders.at(0).get_active_orders();
    for (auto& [id, order] : bank_orders)
    {
        cancel_order(id,0);
    }
    auto bank_positions = traders.at(0).get_current_positions();
    for (auto& [id, position] : bank_positions)
    {
        int available_quantity = traders.at(0).get_available_holding(id);
        if (available_quantity == 0)
        {
            continue;
        }
        int price = std::lround(visible_prices.at(id));
        bool reserved = reserve_order(0,id,Side::sell,available_quantity,price);
        if (!reserved)
        {
            throw std::logic_error("failed to reserve order");
        }
        bool submitted = submit_order(0,id,Side::sell,available_quantity,price);
        if (!submitted)
        {
            throw std::logic_error("failed to submit order");
        }
    }
    auto bank_balance = traders.at(0).get_available_cash();
    if (bank_balance > reserve_target && current_tick % recycle_frequency == 0)
    {
        int excess = bank_balance - reserve_target;
        int trader_count = static_cast<int>(active_traders.size());
        if (trader_count > 0)
        {
            int distribution = excess * recycle_fraction;
            int distribution_per_trader = distribution / trader_count;
            if (distribution_per_trader > 0)
            {
                for (int id : active_traders)
                {
                    bool cost_applied = traders.at(0).apply_cost(distribution_per_trader);
                    if (!cost_applied)
                    {
                        throw std::logic_error("failed to apply cost to bank");
                    }
                    bool payment_accepted = traders.at(id).apply_payment(distribution_per_trader);
                    if (!payment_accepted)
                    {
                        throw std::logic_error("failed to accept payment from bank");
                    }
                }
                bank_redistributed_this_tick = distribution;
            }
        }
    }
    else
    {
        bank_redistributed_this_tick = 0;
    }
    std::vector<int> tick_traders = active_traders;
    std::shuffle(tick_traders.begin(), tick_traders.end(), rng);
    std::vector<int> failed_traders;
    for (int id : tick_traders)
    {
        snapshot_prices();
        auto it = traders.find(id);
        if (it == traders.end())
        {
            throw std::logic_error("Active trader missing");
        }
        it->second.update_observed_prices(visible_prices);
        if (current_tick % cost_frequency == 0)
        {
            bool charged = charge_trader(id, cost_amount);
            if (!charged)
            {
                throw std::logic_error("trader in active traders not in traders");
            }
        }
        std::int64_t available_cash = it->second.get_available_cash();
        if (available_cash >= 0)
        {
            it->second.reset_health();
            TraderDecision decision = make_decision(it->second, rng);

            if (decision.type == ActionType::cancel)
        {
            bool cancel = cancel_order(decision.order_id,id);
            if (!cancel)
            {
                throw std::logic_error("failed to cancel order");
            }
        }
        else if (decision.type == ActionType::sell)
        {
            bool reserve = reserve_order(id,decision.instrument_id,Side::sell,decision.quantity,decision.price);
            if (!reserve)
            {
                throw std::logic_error("failed to reserve sell order");
            }
            bool submit = submit_order(id,decision.instrument_id,Side::sell,decision.quantity,decision.price);
            if (!submit)
            {
                throw std::logic_error("failed to submit sell order");
            }
        }
        else if (decision.type == ActionType::buy)
        {
            bool reserve = reserve_order(id,decision.instrument_id,Side::buy,decision.quantity,decision.price);
            if (!reserve)
            {
                throw std::logic_error("failed to reserve buy  order");
            }
            bool submit = submit_order(id,decision.instrument_id,Side::buy,decision.quantity,decision.price);
            if (!submit)
            {
                throw std::logic_error("failed to submit buy order");
            }
        }

            continue;
        }
        auto choice = it->second.bankruptcy_check();
        if (choice.type == ActionType::cancel)
        {
            bool cancel = cancel_order(choice.order_id,id);
            if (!cancel)
            {
                throw std::logic_error("failed to cancel order");
            }
        }
        else if (choice.type == ActionType::sell)
        {
            bool reserve = reserve_order(id,choice.instrument_id,Side::sell,choice.quantity,choice.price);
            if (!reserve)
            {
                throw std::logic_error("failed to reserve order");
            }
            bool submit = submit_order(id,choice.instrument_id,Side::sell,choice.quantity,choice.price);
            if (!submit)
            {
                throw std::logic_error("failed to submit order");
            }
        }
        else
        {
            //you poor bastard
        }
        available_cash = it->second.get_available_cash();
        if (available_cash >= 0)
        {
            it->second.reset_health();
        }
        else
        {
            bool alive = it->second.increment_health();
            if (!alive)
            {
                //kill trader
                failed_traders.push_back(id);
            }
        }

    }
    for (int trader : failed_traders)
    {
        liquidate_trader(trader);
        TraderType type = traders.at(trader).get_trader_type();
        type_count[type] -= 1;
        traders_to_add.push_back(type);
        //write tombstone
        //remove from simulation
        ++ deaths[type];
    }
    current_tick += 1;
}

Health Simulation::get_trader_health(int trader_id)
{
    auto it = traders.find(trader_id);
    if(it == traders.end())
    {
        return Health::failed;
    }
    return it->second.get_health();
}

std::int64_t Simulation::get_trader_available_cash(int trader_id) const
{
    auto it = traders.find(trader_id);
    if(it == traders.end())
    {
        return 0;
    }
    return it->second.get_available_cash();
}

int Simulation::get_trader_available_holdings(int trader_id, int instrument_id) const
{
    auto it = traders.find(trader_id);
    if(it == traders.end())
    {
        return 0;
    }
    return it->second.get_available_holding(instrument_id);
}

int Simulation::get_trader_total_holdings(int trader_id, int instrument_id) const
{
    auto it = traders.find(trader_id);
    if(it == traders.end())
    {
        return 0;
    }
    return it->second.get_total_holding(instrument_id);
}

const std::unordered_map<int, Position>& Simulation::get_current_trader_positions(int trader_id) const
{
    auto it = traders.find(trader_id);
    if (it == traders.end())
    {
        throw std::logic_error("trader not found");
    }
    return it->second.get_current_positions();
}

const std::unordered_map<int, TraderOrder>& Simulation::get_trader_active_orders(int trader_id) const
{
    auto it = traders.find(trader_id);
    if (it == traders.end())
    {
        throw std::logic_error("trader not found");
    }
    return it->second.get_active_orders();
}

void Simulation::set_recurring_costs(int frequency, std::int64_t amount)
{
    if (frequency <= 0 || amount < 0)
    {
        throw std::logic_error("cannot have negative frequency");
    }
    cost_frequency = frequency;
    cost_amount = amount;
}

void Simulation::set_starting_amount(std::int64_t cash)
{
    if (cash <= 0)
    {
        throw std::logic_error("cannot have negative starting amount");
    }
    starting_amount = cash;
}

SimulationSnapshot Simulation::get_snapshot()
{
    SimulationSnapshot snapshot;
    snapshot.tick = current_tick;

    //instruments
    const auto& names = market.get_instrument_names();
    std::vector<int> ids;
    for (const auto& [id,name] : names)
    {
        ids.push_back(id);
    }
    snapshot.instrument_ids = ids;
    snapshot.instrument_names = names;
    snapshot.instrument_reference_price = market.get_last_prices();
    snapshot.total_trades_per_instrument = market.get_total_trades();

    //population
    snapshot.active_total_traders = static_cast<int>(active_traders.size());
    auto random_count = type_count.find(TraderType::random);
    if (random_count != type_count.end())
    {
        snapshot.random = type_count.at(TraderType::random);
    }
    else
    {
        snapshot.random = 0;
    }

    auto mr_count = type_count.find(TraderType::mean_value);
    if (mr_count != type_count.end())
    {
        snapshot.mean_reversion = type_count.at(TraderType::mean_value);
    }
    else
    {
        snapshot.mean_reversion = 0;
    }
    auto pr_count = type_count.find(TraderType::portfolio_rebalancer);
    if (pr_count != type_count.end())
    {
        snapshot.portfolio_rebalancer = type_count.at(TraderType::portfolio_rebalancer);
    }
    else
    {
        snapshot.portfolio_rebalancer = 0;
    }
    std::int64_t random_total_cash = 0;
    std::int64_t random_total_holdings_worth = 0;
    std::map<int,int> random_holdings;
    double random_cash_fraction = 0;
    std::int64_t pr_total_cash = 0;
    std::int64_t pr_total_holdings_worth = 0;
    std::map<int,int> pr_holdings;
    double pr_cash_fraction = 0;
    std::int64_t mr_total_cash = 0;
    std::int64_t mr_total_holdings_worth = 0;
    std::map<int,int> mr_holdings;
    double mr_cash_fraction = 0;
    for (const auto& id : active_traders)
    {
        const auto& trader = traders.at(id);
        std::int64_t total_cash = trader.get_total_cash();
        const auto& positions = trader.get_current_positions();
        std::int64_t total_holdings_worth = 0;
        for(const auto& [instrument_id, position] : positions)
        {
            total_holdings_worth += position.quantity * snapshot.instrument_reference_price.at(instrument_id);
            if (trader.get_trader_type() == TraderType::random)
            {
                random_holdings[instrument_id] += position.quantity;
            }
            else if (trader.get_trader_type() == TraderType::mean_value)
            {
                mr_holdings[instrument_id] += position.quantity;
            }
            else if (trader.get_trader_type() == TraderType::portfolio_rebalancer)
            {
                pr_holdings[instrument_id] += position.quantity;
            }
        }
        std::int64_t total_worth = total_cash + total_holdings_worth;
        double cash_fraction = 0;
        if (total_worth > 0)
        {
            cash_fraction = (static_cast<double>(total_cash) / static_cast<double>(total_worth)) * 100.0;
        }
        if (trader.get_trader_type() == TraderType::random)
        {
            random_total_cash += total_cash;
            random_total_holdings_worth += total_worth;
            random_cash_fraction += cash_fraction;
        }
        else if (trader.get_trader_type() == TraderType::portfolio_rebalancer)
        {
            pr_total_cash += total_cash;
            pr_total_holdings_worth += total_worth;
            pr_cash_fraction += cash_fraction;
        }
        else if (trader.get_trader_type() == TraderType::mean_value)
        {
            mr_total_cash += total_cash;
            mr_total_holdings_worth += total_worth;
            mr_cash_fraction += cash_fraction;
        }
    }
    snapshot.random_cash = 0;
    snapshot.random_portfolio_value = 0;
    snapshot.random_cash_fraction = 0;
    if (snapshot.random > 0)
    {
        snapshot.random_cash = random_total_cash / snapshot.random;
        snapshot.random_portfolio_value = random_total_holdings_worth / snapshot.random;
        snapshot.random_cash_fraction = random_cash_fraction / snapshot.random;
    }
    snapshot.mean_reversion_cash = 0;
    snapshot.mean_reversion_portfolio_value = 0;
    snapshot.mean_reversion_cash_fraction = 0;
    if (snapshot.mean_reversion > 0)
    {
        snapshot.mean_reversion_cash = mr_total_cash / snapshot.mean_reversion;
        snapshot.mean_reversion_portfolio_value = mr_total_holdings_worth / snapshot.mean_reversion;
        snapshot.mean_reversion_cash_fraction = mr_cash_fraction / snapshot.mean_reversion;
    }
    snapshot.portfolio_rebalancer_cash = 0;
    snapshot.portfolio_rebalancer_portfolio_value = 0;
    snapshot.portfolio_rebalancer_cash_fraction = 0;
    if (snapshot.portfolio_rebalancer > 0)
    {
        snapshot.portfolio_rebalancer_cash = pr_total_cash / snapshot.portfolio_rebalancer;
        snapshot.portfolio_rebalancer_portfolio_value = pr_total_holdings_worth / snapshot.portfolio_rebalancer;
        snapshot.portfolio_rebalancer_cash_fraction = pr_cash_fraction / snapshot.portfolio_rebalancer;
    }

    for (int id : snapshot.instrument_ids)
    {
        int bank = traders.at(0).get_total_holding(id);
        int random = 0;
        auto random_it = random_holdings.find(id);
        if (random_it != random_holdings.end())
        {
            random = random_it->second;
        }
        int mr = 0;
        auto mr_it = mr_holdings.find(id);
        if (mr_it != mr_holdings.end())
        {
            mr = mr_it->second;
        }
        int pr = 0;
        auto pr_it = pr_holdings.find(id);
        if (pr_it != pr_holdings.end())
        {
            pr = pr_it->second;
        }
        int total = random + mr + pr + bank;
        if (total != 0)
        {
            double random_fraction = static_cast<double>(random) / static_cast<double>(total);
            snapshot.random_percentage_of_each_instrument[id] = lround(random_fraction * 100);
            double mr_fraction = static_cast<double>(mr) / static_cast<double>(total);
            snapshot.mean_reversion_percentage_of_each_instrument[id] = lround(mr_fraction * 100);
            double pr_fraction = static_cast<double>(pr) / static_cast<double>(total);
            snapshot.portfolio_rebalancer_percentage_of_each_instrument[id] = lround(pr_fraction * 100);
            double bank_fraction = static_cast<double>(bank) / static_cast<double>(total);
            snapshot.bank_percentage_of_each_instrument[id] = lround(bank_fraction * 100);
        }
    }

    //bank
    snapshot.bank_cash = traders.at(0).get_total_cash();
    snapshot.bank_redistributed_this_tick = bank_redistributed_this_tick;
    const auto& bank_positions = traders.at(0).get_current_positions();
    std::map<int,int> total_holdings;
    std::map<int,int> reserved_holdings;
    for (const auto& [id, position] : bank_positions)
    {
        total_holdings[id] = traders.at(0).get_total_holding(id);
        reserved_holdings[id] = total_holdings.at(id) - traders.at(0).get_available_holding(id);
    }
    snapshot.bank_holdings = total_holdings;
    snapshot.bank_reserved_holdings = reserved_holdings;

    //activity
    snapshot.total_trades = next_unsettled_trade;
    snapshot.total_deaths = 0;
    for (const auto& [type, number] : deaths)
    {
        snapshot.total_deaths += number;
        if (type == TraderType::random)
        {
            snapshot.random_deaths += number;
        }
        else if (type == TraderType::mean_value)
        {
            snapshot.mean_reversion_deaths += number;
        }
        else if (type == TraderType::portfolio_rebalancer)
        {
            snapshot.portfolio_rebalancer_deaths += number;
        }
    }
    snapshot.replacements = replacements;
    const auto& active_orders = market.get_active_orders();
    snapshot.active_orders = static_cast<int>(active_orders.size());
    return snapshot;
}

void Simulation::set_bank_recycling(std::int64_t target, int frequency, double fraction)
{
    reserve_target = target;
    recycle_frequency = frequency;
    recycle_fraction = fraction;
}