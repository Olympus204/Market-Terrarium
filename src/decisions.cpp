#include "decisions.hpp"

#include <optional>
#include <random>
#include <map>
#include <algorithm>
#include <unordered_map>
#include <deque>
#include <stdexcept>
#include <string>
#include <limits>

int nCr(int n, int r)
{
    int result = 1;
    r = std::min(r,n-r);
    for (int i{1}; i <= r; ++i)
    {
        result *= (n - r + i);
        result /= i;
    }
    return result;
}


std::optional<int> chooser(const std::map<int, int>& weights, std::mt19937_64& rng)
{
    int total_weight = 0;
    for (auto [id, weight] : weights)
    {
        total_weight += weight;
        if (weight < 0)
        {
            throw std::logic_error("failed to catch negative weight");
        }
    }
    if (total_weight == 0)
    {
        return std::nullopt;
    }
    std::uniform_int_distribution<int> distribution(1, total_weight);
    int roll = distribution(rng);
    int current_total = 0;
    for (const auto& [id, weight] : weights)
    {
        current_total += weight;
        if (current_total >= roll)
        {
            return id;
        }
    }
    throw std::logic_error("failed to select choice");
}

// random framework

std::map<int, int> random_instrument_choice(const std::unordered_map<int, std::deque<double>>& observed_prices)
{
    std::map<int, int> decisions;
    for (const auto& [id, prices] : observed_prices)
    {
        decisions[id] = 1;
    }
    return decisions;
}

//portfolio rebalancer framework

ActionWeights portfolio_rebalancer_decide_action(const SimpleTrader& trader)
{
    ActionWeights weights{0,0,0,0};
    const auto& observations = trader.get_observed_prices();
    if (observations.size() == 0)
    {
        return weights;
    }
    const auto& positions = trader.get_current_positions();
    const auto& active_orders = trader.get_active_orders();
    auto total_cash = trader.get_total_cash();
    std::map<int,double> holdings_total_worth;
    double total_worth = total_cash;
    for (const auto& [id, observation] : observations)
    {
        int quantity = 0;
        auto position_it = positions.find(id);
        if (position_it != positions.end())
        {
            quantity = position_it->second.quantity;
        }
        holdings_total_worth[id] += observation.back() * quantity;
        total_worth += holdings_total_worth.at(id);
    }
    if (total_worth <= 0)
    {
        return weights;
    }
    double goal = 1.0 / (observations.size() + 1);
    for (const auto& [id, observation] : observations)
    {
        double fraction = holdings_total_worth.at(id) / total_worth;
        double gap = fraction - goal;
        if (gap < 0)
        {
            int incoming_quantity = 0;
            for (const auto& [order_id, order] : active_orders)
            {
                if (order.instrument_id != id)
                {
                    continue;
                }
                if (order.side == Side::buy)
                {
                    incoming_quantity += order.remaining_quantity;
                }
                else if (order.side == Side::sell)
                {
                    incoming_quantity -= order.remaining_quantity;
                    weights.cancel += 1;
                }
            }
            double total_incoming_worth = incoming_quantity * observation.back();
            double new_fraction = (total_incoming_worth + holdings_total_worth.at(id)) / (total_worth);
            double new_gap = new_fraction - goal;
            if (new_gap > 0)
            {
                int contribution = std::floor(std::abs(new_gap) / 0.05);
                weights.cancel += contribution;
            }
            else
            {
                int contribution = std::floor(std::abs(new_gap) / 0.05);
                weights.buy += contribution;
            }
        }
        else if (gap > 0)
        {
            int outgoing_quantity = 0;
            for (const auto& [order_id, order] : active_orders)
            {
                if (order.instrument_id != id)
                {
                    continue;
                }
                if (order.side == Side::buy)
                {
                    outgoing_quantity += order.remaining_quantity;
                    weights.cancel += 1;
                }
                else if (order.side == Side::sell)
                {
                    outgoing_quantity -= order.remaining_quantity;
                }
            }
            double total_incoming_worth = outgoing_quantity * observation.back();
            double new_fraction = (total_incoming_worth + holdings_total_worth.at(id)) / (total_worth);
            double new_gap = new_fraction - goal;
            if (new_gap < 0)
            {
                int contribution = std::floor(std::abs(new_gap) / 0.05);
                weights.cancel += contribution;
            }
            else
            {
                if (trader.get_available_holding(id) > 0)
                {
                    int contribution = std::floor(std::abs(new_gap) / 0.05);
                    weights.sell += contribution;
                }
            }
        }
    }
    if (trader.get_available_cash() <= 0)
    {
        weights.buy = 0;
    }
    return weights;
}

std::map<int,int> portfolio_rebalancer_choose_instrument_weights(const SimpleTrader& trader, Side side)
{
    std::map<int,int> weights;
    const auto& observations = trader.get_observed_prices();
    const auto& positions = trader.get_current_positions();
    const auto& active_orders = trader.get_active_orders();
    auto total_cash = trader.get_total_cash();
    std::map<int,double> holdings_total_worth;
    double total_worth = total_cash;
    for (const auto& [id, observation] : observations)
    {
        int quantity = 0;
        auto position_it = positions.find(id);
        if (position_it != positions.end())
        {
            quantity = position_it->second.quantity;
        }
        holdings_total_worth[id] += observation.back() * quantity;
        total_worth += holdings_total_worth.at(id);
    }
    if (total_worth <= 0)
    {
        return weights;
    }
    double goal = 1.0 / (observations.size() + 1);
    std::map<int,int> movement;
    for (const auto& [order_id, order] : active_orders)
    {
        if (order.side == Side::buy)
        {
            movement[order.instrument_id] += order.remaining_quantity;
        }
        else if (order.side == Side::sell)
        {
            movement[order.instrument_id] -= order.remaining_quantity;
        }
    }

    if (side == Side::buy)
    {
        for (const auto& [id, observation] : observations)
        {
            double movements = 0;
            auto it = movement.find(id);
            if (it != movement.end())
            {
                movements = it->second;
            }
            double movement_worth = movements * observation.back();
            double predicted_fraction = (holdings_total_worth.at(id) + movement_worth) / total_worth;
            double gap = predicted_fraction - goal;
            if (gap < 0)
            {
                int contribution = std::floor(std::abs(gap) / 0.05);
                weights[id] += contribution;
            }
        }
    }
    else if (side == Side::sell)
    {
        for (const auto& [id, observation] : observations)
        {
            double movements = 0;
            auto it = movement.find(id);
            if (it != movement.end())
            {
                movements = it->second;
            }
            double movement_worth = movements * observation.back();
            double predicted_fraction = (holdings_total_worth.at(id) + movement_worth) / total_worth;
            double gap = predicted_fraction - goal;
            if (gap > 0)
            {
                if (trader.get_available_holding(id) > 0)
                {
                    int contribution = std::floor(std::abs(gap) / 0.05);
                    weights[id] += contribution;
                }
            }
        }
    }
    return weights;
}

int portfolio_rebalancer_quantity(const SimpleTrader& trader, Side side, int instrument_id, std::int64_t chosen_order_price)
{
    const auto& observations = trader.get_observed_prices();
    const auto& holdings = trader.get_total_holding(instrument_id);
    const auto& positions = trader.get_current_positions();
    const auto& active_orders = trader.get_active_orders();
    auto total_cash = trader.get_total_cash();
    auto available_cash = trader.get_available_cash();
    std::map<int,double> holdings_total_worth;
    double total_worth = total_cash;
    for (const auto& [id, observation] : observations)
    {
        int quantity = 0;
        auto position_it = positions.find(id);
        if (position_it != positions.end())
        {
            quantity = position_it->second.quantity;
        }
        holdings_total_worth[id] += observation.back() * quantity;
        total_worth += holdings_total_worth.at(id);
    }
    double goal = 1.0 / (observations.size() + 1);
    std::map<int,int> movement;
    for (const auto& [order_id, order] : active_orders)
    {
        if (order.side == Side::buy)
        {
            movement[order.instrument_id] += order.remaining_quantity;
        }
        else if (order.side == Side::sell)
        {
            movement[order.instrument_id] -= order.remaining_quantity;
        }
    }
    int instrument_movement = 0;
    auto it = movement.find(instrument_id);
    if (it != movement.end())
    {
        instrument_movement = it->second;
    }
    int holdings_after_movement = holdings + instrument_movement;
    int ideal_quantity = std::lround((goal * total_worth) / observations.at(instrument_id).back());
    int difference = ideal_quantity - holdings_after_movement;
    if (side == Side::buy)
    {
        if (difference > 0)
        {
            int possible = available_cash / chosen_order_price;
            int quantity = std::min(difference,possible);
            return std::max(quantity,1);
        }
        return 0;
    }
    else if (side == Side::sell)
    {
        if (difference < 0)
        {
            int desired_sell = std::abs(difference);
            int possible = std::clamp(desired_sell,0,trader.get_available_holding(instrument_id));
            int quantity = std::abs(possible);
            return std::max(quantity,1);
        }
        return 0;
    }
    return 0;
}

std::optional<int> portfolio_rebalancer_cancel(const SimpleTrader& trader)
{
    const auto& observations = trader.get_observed_prices();
    if (observations.size() == 0)
    {
        return std::nullopt;
    }
    const auto& positions = trader.get_current_positions();
    const auto& active_orders = trader.get_active_orders();
    auto total_cash = trader.get_total_cash();
    std::map<int,double> holdings_total_worth;
    double total_worth = total_cash;
    for (const auto& [id, observation] : observations)
    {
        int quantity = 0;
        auto position_it = positions.find(id);
        if (position_it != positions.end())
        {
            quantity = position_it->second.quantity;
        }
        holdings_total_worth[id] += observation.back() * quantity;
        total_worth += holdings_total_worth.at(id);
    }
    if (total_worth <= 0)
    {
        return std::nullopt;
    }
    double goal = 1.0 / (observations.size() + 1);
    std::map<int,int> movement;
    for (const auto& [order_id, order] : active_orders)
    {
        if (order.side == Side::buy)
        {
            movement[order.instrument_id] += order.remaining_quantity;
        }
        else if (order.side == Side::sell)
        {
            movement[order.instrument_id] -= order.remaining_quantity;
        }
    }
    bool is_cancel = false;
    int cancel_id = 0;
    double best_improvement = 0;
    for (const auto& [order_id, order] : active_orders)
    {
        int movements = 0;
        auto it = movement.find(order.instrument_id);
        if (it != movement.end())
        {
            movements = it->second;
        }
        int projected_quantity = trader.get_total_holding(order.instrument_id) + movements;
        double projected_fraction = (projected_quantity * observations.at(order.instrument_id).back()) / total_worth;
        double projected_gap = projected_fraction - goal;
        int quantity_without = 0;
        if (order.side == Side::buy)
        {
            quantity_without = projected_quantity - order.remaining_quantity;
        }
        else if (order.side == Side::sell)
        {
            quantity_without = projected_quantity + order.remaining_quantity;
        }
        double fraction_without = (quantity_without * observations.at(order.instrument_id).back()) / total_worth;
        double gap_without = fraction_without - goal;
        double improvement = std::abs(projected_gap) - std::abs(gap_without);
        
        if (best_improvement < improvement)
        {
            is_cancel = true;
            cancel_id = order_id;
            best_improvement = std::abs(projected_gap) - std::abs(gap_without);
        }
    }
    if (is_cancel)
    {
        return cancel_id;
    }
    return std::nullopt;
}

//mean value framework

ActionWeights mean_value_decide_action(const SimpleTrader& trader)
{
    ActionWeights weights{0,0,0,0};
    const auto& observations = trader.get_observed_prices();
    const auto& active_orders = trader.get_active_orders();
    for (auto& [id,prices] : observations)
    {
        std::int64_t total_instrument_prices = 0;
        for (std::size_t i{0}; i < prices.size(); ++i)
        {
            total_instrument_prices += prices.at(i);
        }
        double current_price = prices.back();
        int memory_number = static_cast<int>(prices.size()) - 1;
        double mean = current_price;
        if (memory_number > 0)
        {
            mean = (total_instrument_prices - current_price) / memory_number;
        }
        if (current_price < mean)
        {
            //buy
            double difference_ratio = abs(current_price - mean) / mean;
            weights.buy += floor(difference_ratio/0.02);
        }
        else if(current_price > mean)
        {
            //sell
            int available = trader.get_available_holding(id);
            if (available > 0)
            {
                double difference_ratio = abs(mean - current_price) / mean;
                double raw_weight = std::floor(difference_ratio / 0.02);

                if (!std::isfinite(raw_weight))
                {
                    throw std::logic_error("invalid strategy weight");
                }

                int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
                weights.sell += contribution;
            }
        }
        else 
        {
            //wait
            weights.wait += 1;
        }

        //cancel logic
        for (const auto& [order_id, order] : active_orders)
        {
            if (order.instrument_id != id)
            {
                continue;
            }
            if (order.side == Side::buy)
            {
                if (order.limit_price < mean)
                {
                    continue;
                }
                double difference_ratio = abs(order.limit_price - mean) / mean;
                double raw_weight = std::floor(difference_ratio / 0.02);

                if (!std::isfinite(raw_weight))
                {
                    throw std::logic_error("invalid strategy weight");
                }
                int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
                weights.cancel += contribution;
            }
            else if (order.side == Side::sell)
            {
                if (order.limit_price > mean)
                {
                    continue;
                }
                double difference_ratio = abs(mean - order.limit_price) / mean;
                double raw_weight = std::floor(difference_ratio / 0.02);

                if (!std::isfinite(raw_weight))
                {
                    throw std::logic_error("invalid strategy weight");
                }
                int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
                weights.cancel += contribution;
            }
        }
    }
    return weights;
}

std::map<int,int> mean_value_choose_instrument_weights(const SimpleTrader& trader, Side side)
{
    std::map<int,int> weights;
    const auto& observations = trader.get_observed_prices();
    for (const auto& [id,prices] : observations)
    {
        weights[id] = 0;
        std::int64_t total_instrument_prices = 0;
        for (std::size_t i{0}; i < prices.size(); ++i)
        {
            total_instrument_prices += prices.at(i);
        }
        double current_price = prices.back();
        int memory_number = static_cast<int>(prices.size()) - 1;
        double mean = current_price;
        if (memory_number > 0)
        {
            mean = (total_instrument_prices - current_price) / memory_number;
        }
        if (current_price < mean && side == Side::buy)
        {
            //buy
            double difference_ratio = abs(current_price - mean) / mean;
            double raw_weight = std::floor(difference_ratio / 0.02);

                if (!std::isfinite(raw_weight))
                {
                    throw std::logic_error("invalid strategy weight");
                }
                int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
                weights.at(id) += contribution;
        }
        else if(current_price > mean && side == Side::sell)
        {
            int available = trader.get_available_holding(id);
            if (available > 0)
            {
                double difference_ratio = abs(mean - current_price) / mean;
                double raw_weight = std::floor(difference_ratio / 0.02);

                if (!std::isfinite(raw_weight))
                {
                    throw std::logic_error("invalid strategy weight");
                }
                int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
                weights.at(id) += contribution;
            }
        }
    }
    return weights;
}

std::map<int, int> mean_value_price_weights(const SimpleTrader& trader, int instrument_id, int steps)
{
    if (steps <= 0)
    {
        throw std::logic_error("cannot have steps <= 0");
    }
    std::map<int,int> price_weights;
    const auto& observation = trader.get_observed_prices().at(instrument_id);
    double current_price = observation.back();
    double total_instrument_prices = 0;
    for (std::size_t i{0}; i < observation.size(); ++i)
    {
        total_instrument_prices += observation.at(i);
    }
    int memory_number = static_cast<int>(observation.size()) - 1;
    double mean = current_price;
    if (memory_number > 0)
    {
        mean = (total_instrument_prices - current_price) / static_cast<double>(memory_number);
    }
    double gap = mean - current_price;
    double step = gap / steps;
    for (int i{0}; i <= steps; ++i)
    {
        double candidate = current_price + i * step;

        if (!std::isfinite(candidate))
        {
            throw std::logic_error("invalid mean value price");
        }

        candidate = std::clamp(
            candidate,
            1.0,
            static_cast<double>(std::numeric_limits<int>::max())
        );

        int price = static_cast<int>(std::lround(candidate));

        price_weights[price] += nCr(steps, i);
    }
    return price_weights;
}

std::map<int,int> mean_value_cancel(const SimpleTrader& trader)
{
    std::map<int,int> cancel_weights;
    const auto& active_orders = trader.get_active_orders();
    for (const auto& [id, order] : active_orders)
    {
        cancel_weights[id] = 0;
        const auto& observation = trader.get_observed_prices().at(order.instrument_id);
        std::int64_t order_price = order.limit_price;
        std::int64_t current_price = observation.back();
        std::int64_t total_instrument_prices = 0;
        for (std::size_t i{0}; i < observation.size(); ++i)
        {
            total_instrument_prices += observation.at(i);
        }
        int memory_number = static_cast<int>(observation.size()) - 1;
        double mean = current_price;
        if (memory_number > 0)
        {
            mean = (total_instrument_prices - current_price) / static_cast<double>(memory_number);
        }
        if (order_price > mean && order.side == Side::buy)
        {
            //cancel buy
            double difference_ratio = abs(order_price - mean) / mean;
            double raw_weight = std::floor(difference_ratio / 0.02);

            if (!std::isfinite(raw_weight))
            {
                throw std::logic_error("invalid strategy weight");
            }
            int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
            cancel_weights.at(id) += contribution;
        }
        else if(order_price < mean && order.side == Side::sell)
        {
            //cancel sell
            double difference_ratio = abs(mean - order_price) / mean;
            double raw_weight = std::floor(difference_ratio / 0.02);

            if (!std::isfinite(raw_weight))
            {
                throw std::logic_error("invalid strategy weight");
            }
            int contribution = static_cast<int>(std::clamp(raw_weight, 0.0, 50.0));
            cancel_weights.at(id) += contribution;
        }
    }
    return cancel_weights;
}

TraderDecision make_decision(const SimpleTrader& trader, std::mt19937_64& rng)
{
    auto type = trader.get_trader_type();
    if (type == TraderType::none)
    {
        return TraderDecision{ActionType::none,0,0,0,0};
    }
    ActionWeights weights{0,0,0,0};
    if (type == TraderType::random)
    {
        weights = {1,1,1,1};
    }
    else if (type == TraderType::portfolio_rebalancer)
    {
        weights = portfolio_rebalancer_decide_action(trader);
    }
    else if (type == TraderType::mean_value)
    {
        weights = mean_value_decide_action(trader);
    }
    const auto& current_positions = trader.get_current_positions();
    int total_holdings = 0;
    for (auto& [id, position] : current_positions)
    {
        total_holdings += trader.get_available_holding(id);
    }
    if (total_holdings == 0)
    {
        weights.sell = 0;
    }
    auto available_cash = trader.get_available_cash();
    if (available_cash <= 0)
    {
        weights.buy = 0;
    }
    const auto& active_orders = trader.get_active_orders();
    if (active_orders.size() == 0)
    {
        weights.cancel = 0;
    }
    int weights_total = weights.buy + weights.sell + weights.cancel + weights.wait;
    if (weights_total == 0)
    {
        weights.wait = 1;
        weights_total = 1;
    }

    //decision making time
    std::uniform_int_distribution<int> distribution(1, weights_total);
    int roll = distribution(rng);
    if (roll <= weights.buy)
    {
        //buy
        //choose holding
        std::map<int,int> holding_weights;
        if (type == TraderType::random)
        {
            holding_weights = random_instrument_choice(trader.get_observed_prices());
        }
        else if (type == TraderType::portfolio_rebalancer)
        {
            holding_weights = portfolio_rebalancer_choose_instrument_weights(trader, Side::buy);
        }
        else if (type == TraderType::mean_value)
        {
            holding_weights = mean_value_choose_instrument_weights(trader, Side::buy);
        }
        auto chosen_id = chooser(holding_weights, rng);
        if (!chosen_id.has_value()) {
            return TraderDecision{ActionType::none, 0, 0, 0, 0};
        }
        int id = chosen_id.value();
        if (type == TraderType::random)
        {
            const auto& prices = trader.observed_price(id);
            int observed_price = prices.back();
            std::normal_distribution<double> price_distribution(observed_price, observed_price * 0.05);
            int price = static_cast<int>(std::round(price_distribution(rng)));
            price = std::max(1,price);
            int max_quantity = trader.get_available_cash() / price;
            if (max_quantity <= 0)
            {
                return TraderDecision{ActionType::none,0,0,0,0};
            }
            std::uniform_int_distribution<int> quantity_distribution(1,max_quantity);
            int quantity = quantity_distribution(rng);
            return TraderDecision(ActionType::buy, 0, id, quantity, price);
        }
        else if (type == TraderType::portfolio_rebalancer)
        {
            const auto& prices = trader.observed_price(id);
            int observed_price = prices.back();
            std::normal_distribution<double> price_distribution(observed_price, observed_price * 0.05);
            int price = static_cast<int>(std::round(price_distribution(rng)));
            price = std::max(price,1);
            int quantity = portfolio_rebalancer_quantity(trader,Side::buy,id,price);
            int max_quantity = trader.get_available_cash() / price;
            if (max_quantity <= 0 || quantity <= 0)
            {
                return TraderDecision{ActionType::none,0,0,0,0};
            }
            return TraderDecision(ActionType::buy, 0, id, quantity, price);
        }
        else if (type == TraderType::mean_value)
        {
            std::map<int,int> price_weights = mean_value_price_weights(trader,id,5);
            auto chosen_price = chooser(price_weights, rng);
            if (!chosen_price.has_value()){
            return TraderDecision{ActionType::none, 0, 0, 0, 0};
            }
            int price = chosen_price.value();
            int max_quantity = trader.get_available_cash() / price;
            if (max_quantity <= 0)
            {
                return TraderDecision{ActionType::none,0,0,0,0};
            }
            std::uniform_int_distribution<int> quantity_distribution(1,max_quantity);
            int quantity = quantity_distribution(rng);
            return TraderDecision(ActionType::buy, 0, id, quantity, price);
        }

    }
    else if (roll <= weights.buy + weights.sell)
    {
        // sell
        //choose holding
        std::map<int,int> holding_weights;
        if (type == TraderType::random)
        {
            for (auto& [id, position] : current_positions)
            {
                int available_quantity = trader.get_available_holding(id);
                if (available_quantity > 0)
                {
                    holding_weights[id] = 1;
                }
            }
        }
        else if (type == TraderType::portfolio_rebalancer)
        {
            holding_weights = portfolio_rebalancer_choose_instrument_weights(trader, Side::sell);
        }
        else if (type == TraderType::mean_value)
        {
            holding_weights = mean_value_choose_instrument_weights(trader, Side::sell);
        }
        auto chosen_id = chooser(holding_weights, rng);
        if (!chosen_id.has_value()) {
            return TraderDecision{ActionType::none, 0, 0, 0, 0};
        }
        int id = chosen_id.value();
        if (type == TraderType::random)
        {
            const auto& prices = trader.observed_price(id);
            int observed_price = prices.back();
            std::normal_distribution<double> price_distribution(observed_price, observed_price * 0.05);
            int price = static_cast<int>(std::round(price_distribution(rng)));
            price = std::max(1,price);
            int max_quantity = trader.get_available_holding(id);
            if (max_quantity <= 0)
            {
                throw std::logic_error("zero or below value got through");
            }
            std::uniform_int_distribution<int> quantity_distribution(1,max_quantity);
            int quantity = quantity_distribution(rng);
            return TraderDecision(ActionType::sell, 0, id, quantity, price);
        }
        else if (type == TraderType::portfolio_rebalancer)
        {
            const auto& prices = trader.observed_price(id);
            int observed_price = prices.back();
            std::normal_distribution<double> price_distribution(observed_price, observed_price * 0.05);
            int price = static_cast<int>(std::round(price_distribution(rng)));
            int quantity = portfolio_rebalancer_quantity(trader,Side::sell,id,price);
            price = std::max(1,price);
            int max_quantity = trader.get_available_holding(id);
            if (max_quantity <= 0 || quantity <= 0)
            {
                return TraderDecision{ActionType::none,0,0,0,0};
            }
            return TraderDecision(ActionType::sell, 0, id, quantity, price);
        }
        else if (type == TraderType::mean_value)
        {
            std::map<int,int> price_weights = mean_value_price_weights(trader,id,5);
            auto chosen_price = chooser(price_weights, rng);
            if (!chosen_price.has_value())
            {
                return TraderDecision{ActionType::none,0,0,0,0};
            }
            std::int64_t price = chosen_price.value();
            int max_quantity = trader.get_available_holding(id);
            if (max_quantity <= 0)
            {
                throw std::logic_error("zero or below value got through");
            }
            std::uniform_int_distribution<int> quantity_distribution(1,max_quantity);
            int quantity = quantity_distribution(rng);
            return TraderDecision(ActionType::sell, 0, id, quantity, price);
        }
    }
    else if (roll <= weights.buy + weights.sell + weights.cancel)
    {
        // cancel
        std::map<int,int> cancel_weights;
        if (type == TraderType::random)
        {
            const auto& orders = trader.get_active_orders();
            for (const auto& [id, order] : orders)
            {
                cancel_weights[id] = 1;
            }
        }
        else if (type == TraderType::portfolio_rebalancer)
        {
            auto chosen_id = portfolio_rebalancer_cancel(trader);
            if (!chosen_id.has_value())
            {
                return TraderDecision{ActionType::none,0,0,0,0};
            }
            int id = chosen_id.value();
            cancel_weights[id] = 1;
        }
        else if (type == TraderType::mean_value)
        {
            cancel_weights = mean_value_cancel(trader);
        }
        auto chosen_id = chooser(cancel_weights,rng);
        if (!chosen_id.has_value())
        {
            return TraderDecision{ActionType::none,0,0,0,0};
        }
        int id = chosen_id.value();
        return TraderDecision{ActionType::cancel,id,0,0,0};
    }
    else
    {
        return TraderDecision{ActionType::none,0,0,0,0};
    }
    return TraderDecision{ActionType::none,0,0,0,0};
}