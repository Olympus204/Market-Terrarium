#pragma once

#include "trader.hpp"

#include <random>
#include <map>
#include <optional>

struct ActionWeights
{
    int buy;
    int sell;
    int cancel;
    int wait;
};

std::optional<int> chooser(const std::map<int, int>& weights, std::mt19937_64& rng);
std::map<int, int> random_instrument_choice(std::unordered_map<int, std::deque<int>>& observed_prices);
TraderDecision make_decision(const SimpleTrader& trader, std::mt19937_64& rng);