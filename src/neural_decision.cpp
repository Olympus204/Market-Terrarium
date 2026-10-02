#include "neural_decision.hpp"

#include <cmath>

EvolutionInputs build_inputs(const SimpleTrader& trader, int instrument_id)
{
    EvolutionInputs inputs;
    const auto& all_observations = trader.get_observed_prices();
    auto it = all_observations.find(instrument_id);
    if (it == all_observations.end())
    {
        throw std::logic_error("instrument not found");
    }
    const auto& observations = it->second;
    int steps = std::min(static_cast<int>(observations.size()),6);
    inputs.change_over_5_ticks = (observations.back().current_price - observations.at(observations.size() - steps).current_price) / observations.at(observations.size() - steps).current_price;
    steps = std::min(static_cast<int>(observations.size()),21);
    inputs.change_over_20_ticks = (observations.back().current_price - observations.at(observations.size() - steps).current_price) / observations.at(observations.size() - steps).current_price;
    if (observations.back().best_buy.has_value())
    {
        inputs.relative_best_buy = (observations.back().best_buy.value() - observations.back().current_price) / observations.back().current_price;
        inputs.buy_exists = true;
    }
    if (observations.back().best_sell.has_value())
    {
        inputs.relative_best_sell = (observations.back().best_sell.value() - observations.back().current_price) / observations.back().current_price;
        inputs.sell_exists = true;
    }
    double instrument_wealth = 0;
    double cash_wealth = trader.get_total_cash();
    double total_wealth = cash_wealth;
    for (const auto& [id, obs] : all_observations)
    {
        int quantity = trader.get_total_holding(id);
        total_wealth += quantity * obs.back().current_price;
        if (id == instrument_id)
        {
            instrument_wealth += quantity * obs.back().current_price;
        }
    }
    if (total_wealth != 0)
    {
        inputs.fraction_of_wealth_in_instruments = instrument_wealth / total_wealth;
        inputs.fraction_of_wealth_in_cash = cash_wealth / total_wealth;
    }
    return inputs;
}

EvolutionOutputs forward_pass(const NeuralGenome& genome, const EvolutionInputs& inputs, double memory_1, double memory_2, double memory_3, double memory_4)
{
    std::array<double,NeuralGenome::INPUT_SIZE> input_array;
    input_array[0] = inputs.change_over_5_ticks;
    input_array[1] = inputs.change_over_20_ticks;
    input_array[2] = inputs.buy_exists;
    input_array[3] = inputs.relative_best_buy;
    input_array[4] = inputs.sell_exists;
    input_array[5] = inputs.relative_best_sell;
    input_array[6] = inputs.fraction_of_wealth_in_instruments;
    input_array[7] = inputs.fraction_of_wealth_in_cash;
    input_array[8] = memory_1;
    input_array[9] = memory_2;
    input_array[10] = memory_3;
    input_array[11] = memory_4;

    EvolutionOutputs outputs;
    std::array<double,NeuralGenome::HIDDEN_SIZE> hidden_nodes;
    for (std::size_t i{0}; i < NeuralGenome::HIDDEN_SIZE; ++i)
    {
        double unactivated = 0;
        for (std::size_t j{0}; j < input_array.size(); ++j)
        {
            unactivated += input_array[j] * genome.input_weights[i * NeuralGenome::INPUT_SIZE + j]; 
        }
        unactivated += genome.hidden_biases[i];
        hidden_nodes[i] = std::tanh(unactivated);
    }
    std::array<double,NeuralGenome::OUTPUT_SIZE> output_array;
    for (std::size_t i{0}; i < NeuralGenome::OUTPUT_SIZE; ++i)
    {
        double unactivated = 0;
        for (std::size_t j{0}; j < hidden_nodes.size(); ++j)
        {
            unactivated += hidden_nodes[j] * genome.output_weights[i * NeuralGenome::HIDDEN_SIZE + j]; 
        }
        unactivated += genome.output_biases[i];
        output_array[i] = std::tanh(unactivated);
    }

    outputs.buy_score = output_array[0];
    outputs.sell_score = output_array[1];
    outputs.cancel_score = output_array[2];
    outputs.wait_score = output_array[3];
    outputs.price = output_array[4];
    outputs.quantity = output_array[5];
    outputs.memory_1 = output_array[6];
    outputs.memory_2 = output_array[7];
    outputs.memory_3 = output_array[8];
    outputs.plasticity = output_array[9];
    return outputs;
}

TraderDecision evaluate(std::map<int, EvolutionOutputs>& outputs, const SimpleTrader& trader)
{
    const auto& active_orders = trader.get_active_orders();
    int best_instrument = 0;
    double best_weight = 0;
    bool is_best = false;
    for (auto& [id, output] : outputs)
    {
        if (trader.get_available_holding(id) == 0)
        {
            output.sell_score = -1000;
        }
        bool can_cancel = false;
        for (const auto& [order_id, order] : active_orders)
        {
            if (order.instrument_id != id)
            {
                continue;
            }
            else
            {
                can_cancel = true;
                break;
            }
        }
        if (!can_cancel)
        {
            output.cancel_score = -1000;
        }
        double weight = std::max(output.buy_score,output.sell_score);
        weight = std::max(weight,output.cancel_score);
        weight -= output.wait_score;
        if (!is_best || weight > best_weight)
        {
            is_best = true;
            best_weight = weight;
            best_instrument = id;
        }
    }
    if (is_best)
    {
        if (best_weight > 0)
        {
            std::int64_t best_buy = 0;
            std::int64_t best_sell = 0;
            const auto& observation = trader.get_observed_prices().at(best_instrument);
            int spread = 20;
            if (observation.back().best_buy.has_value() && observation.back().best_sell.has_value())
            {   
                spread = observation.back().best_sell.value() - observation.back().best_buy.value();
                best_buy = observation.back().best_buy.value();
                best_sell = observation.back().best_sell.value();
            }
            else if (observation.back().best_buy.has_value())
            {
                spread =  lround(observation.back().current_price - observation.back().best_buy.value());
                best_buy = observation.back().best_buy.value();
                best_sell = lround(observation.back().current_price);
            }
            else
            {
                spread = lround(observation.back().current_price * 0.05);
                best_buy = lround(observation.back().current_price);
                best_sell = lround(observation.back().current_price);
                if (observation.back().best_sell.has_value())
                {
                    best_sell = observation.back().best_sell.value();
                }
            }
            spread = std::max(spread,1);
            double max_weight = std::max(outputs.at(best_instrument).buy_score,outputs.at(best_instrument).sell_score);
            max_weight = std::max(max_weight,outputs.at(best_instrument).cancel_score);
            if (max_weight == outputs.at(best_instrument).buy_score)
            {
                std::int64_t buy_price = lround(best_buy + outputs.at(best_instrument).price * spread);
                buy_price = std::max(buy_price,std::int64_t{1});
                int available_quantity = std::floor(trader.get_available_cash() / buy_price);
                int chosen_quantity = lround(available_quantity * ((outputs.at(best_instrument).quantity + 1) / 2));
                if (chosen_quantity == 0)
                {
                    return TraderDecision{ActionType::none,0,0,0,0};
                }
                return TraderDecision{ActionType::buy, 0, best_instrument, chosen_quantity, buy_price};
            }
            if (max_weight == outputs.at(best_instrument).sell_score)
            {
                std::int64_t sell_price = lround(best_sell - outputs.at(best_instrument).price * spread);
                sell_price = std::max(sell_price,std::int64_t{1});
                int available_quantity = trader.get_available_holding(best_instrument);
                int chosen_quantity = lround(available_quantity * ((outputs.at(best_instrument).quantity + 1) / 2));
                if (chosen_quantity == 0)
                {
                    return TraderDecision{ActionType::none,0,0,0,0};
                }
                return TraderDecision{ActionType::sell, 0, best_instrument, chosen_quantity, sell_price};
            }
            if (max_weight == outputs.at(best_instrument).cancel_score)
            {
                int smallest_id;
                bool found_smallest = false;
                for (const auto& [order_id, order] : active_orders)
                {
                    if (order.instrument_id != best_instrument)
                    {
                        continue;
                    }
                    if (!found_smallest || order_id < smallest_id)
                    {
                        found_smallest = true;
                        smallest_id = order_id;
                    }
                }
                if (found_smallest)
                {
                    return TraderDecision{ActionType::cancel,smallest_id,0,0,0};
                }
                return TraderDecision{ActionType::none,0,0,0,0};
            }
        }
    }
    return TraderDecision{ActionType::none,0,0,0,0};
}


MemoryDecision neural_decide(const SimpleTrader& trader)
{
    std::map<int,EvolutionOutputs> outputs;
    const auto& observations = trader.get_observed_prices();
    const auto& genome = trader.get_genome();
    std::map<int, double> memory_1 = genome.memory_1;
    std::map<int, double> memory_2 = genome.memory_2;
    std::map<int, double> memory_3 = genome.memory_3;
    std::map<int, double> plasticity;
    for (const auto& [id, observation] : observations)
    {
        auto it_1 = memory_1.find(id);
        if (it_1 == memory_1.end())
        {
            memory_1[id] = 0;
        }
        auto it_2 = memory_2.find(id);
        if (it_2 == memory_2.end())
        {
            memory_2[id] = 0;
        }
        auto it_3 = memory_3.find(id);
        if (it_3 == memory_3.end())
        {
            memory_3[id] = 0;
        } 
        plasticity[id] = 0;
        EvolutionInputs inputs = build_inputs(trader,id);
        EvolutionOutputs output = forward_pass(genome, inputs, memory_1.at(id), memory_2.at(id),memory_3.at(id),plasticity.at(id));
        memory_1.at(id) = output.memory_1;
        memory_2.at(id) = output.memory_2;
        memory_3.at(id) = output.memory_3;
        plasticity[id] = output.plasticity;
        outputs.emplace(id,output);
    }
    double average_plastiicty = 0;
    for (const auto& [id, plastic] : plasticity)
    {
        average_plastiicty += plastic;
    }
    average_plastiicty = average_plastiicty / static_cast<double>(plasticity.size());
    return MemoryDecision{evaluate(outputs,trader),memory_1,memory_2,memory_3,average_plastiicty};
}