#pragma once

#include <optional>

#include "trader.hpp"

struct EvolutionInputs
{
    double change_over_5_ticks;
    double change_over_20_ticks;
    double relative_best_buy = 0;
    double relative_best_sell = 0;
    bool buy_exists = false;
    bool sell_exists = false;
    double fraction_of_wealth_in_instruments = 0;
    double fraction_of_wealth_in_cash = 0;
    double memory_1;
    double memory_2;
};

struct EvolutionOutputs
{
    double buy_score;
    double sell_score;
    double cancel_score;
    double wait_score;
    double price;
    double quantity;
    double memory_1;
    double memory_2;
    double memory_3;
    double plasticity;
};

struct MemoryDecision
{
    TraderDecision decision;
    std::map<int,double> memory_1;
    std::map<int,double> memory_2;
    std::map<int,double> memory_3;
    double plasticity;
};

MemoryDecision neural_decide(const SimpleTrader& trader);