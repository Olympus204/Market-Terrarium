#include "neural_genome.hpp"

NeuralGenome NeuralGenome::random(std::mt19937_64& rng)
{
    NeuralGenome genome;

    std::normal_distribution<double> distribution(0.0, 0.5);

    for (double& weight : genome.input_weights)
        weight = distribution(rng);

    for (double& bias : genome.hidden_biases)
        bias = distribution(rng);

    for (double& weight : genome.output_weights)
        weight = distribution(rng);

    for (double& bias : genome.output_biases)
        bias = distribution(rng);

    return genome;
}

void NeuralGenome::mutate_value(double& value, std::mt19937_64& rng)
{
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::normal_distribution<double> mutation(0.0, 0.1);

    if (chance(rng) < 0.05)
    {
        value += mutation(rng);
    }
}

void NeuralGenome::mutate(std::mt19937_64& rng)
{
    for (double& x : input_weights)
        mutate_value(x, rng);

    for (double& x : hidden_biases)
        mutate_value(x, rng);

    for (double& x : output_weights)
        mutate_value(x, rng);

    for (double& x : output_biases)
        mutate_value(x, rng);
}

void NeuralGenome::inherit()
{
    change_no = 0;
    initial_change = std::abs(initial_change);
    std::map<int,double> empty_memory;
    memory_1 = empty_memory;
    memory_2 = empty_memory;
    memory_3 = empty_memory;
}

void NeuralGenome::learn(std::int64_t trader_worth_change, std::int64_t median_this_epoch, std::int64_t median_last_epoch)
{

    std::int64_t worth_change = trader_worth_change;
    std::int64_t median_change = median_this_epoch - median_last_epoch;
    if (change_no == 0)
    {
        if (layer_no == 0)
        {
            input_weights.at(node_no * INPUT_SIZE + extra_node_no) += initial_change;
            change_no += 1;
        }
        else if (layer_no == 1)
        {
            hidden_biases.at(node_no) += initial_change;
            change_no += 1;
        }
        else if (layer_no == 2)
        {
            output_weights.at(node_no * HIDDEN_SIZE + extra_node_no) += initial_change;
            change_no += 1;
        }
        else if (layer_no == 3)
        {
            output_biases.at(node_no) += initial_change;
            change_no += 1;
        }
    }
    else if (change_no == 1 && worth_change < median_change)
    {
        initial_change *= -1;
        if (layer_no == 0)
        {
            input_weights.at(node_no * INPUT_SIZE + extra_node_no) += initial_change;
            change_no += 1;
        }
        else if (layer_no == 1)
        {
            hidden_biases.at(node_no) += initial_change;
            change_no += 1;
        }
        else if (layer_no == 2)
        {
            output_weights.at(node_no * HIDDEN_SIZE + extra_node_no) += initial_change;
            change_no += 1;
        }
        else if (layer_no == 3)
        {
            output_biases.at(node_no) += initial_change;
            change_no += 1;
        }
    }
    else
    {
        if (worth_change < median_change)
        {
            initial_change *= -1; 
        }
        if (layer_no == 0)
        {
            input_weights.at(node_no * INPUT_SIZE + extra_node_no) += initial_change / (2 * change_no);
            change_no += 1;
            if (change_no >= max_changes)
            {
                change_no = 0;
                node_no += 1;
                if (node_no >= HIDDEN_SIZE)
                {
                    node_no = 0;
                    extra_node_no += 1;
                    if (extra_node_no >= INPUT_SIZE)
                    {
                        extra_node_no = 0;
                        layer_no += 1;
                        initial_change = std::abs(initial_change);
                    }
                }
            }
        }
        else if (layer_no == 1)
        {
            hidden_biases.at(node_no) += initial_change / (2 * change_no);
            change_no += 1;
            if (change_no >= max_changes)
            {
                change_no = 0;
                node_no += 1;
                if (node_no >= HIDDEN_SIZE)
                {
                    node_no = 0;
                    layer_no += 1;
                    initial_change = std::abs(initial_change);
                }
            }
        }
        else if (layer_no == 2)
        {
            output_weights.at(node_no * HIDDEN_SIZE + extra_node_no) += initial_change / (2 * change_no);
            change_no += 1;
            if (change_no >= max_changes)
            {
                change_no = 0;
                node_no += 1;
                if (node_no >= OUTPUT_SIZE)
                {
                    node_no = 0;
                    extra_node_no += 1;
                    if (extra_node_no >= HIDDEN_SIZE)
                    {
                        extra_node_no = 0;
                        layer_no += 1;
                        initial_change = std::abs(initial_change);
                    }
                }
            }
        }
        else if (layer_no == 3)
        {
            output_biases.at(node_no) += initial_change / (2 * change_no);
            change_no += 1;
            if (change_no >= max_changes)
            {
                change_no = 0;
                node_no += 1;
                if (node_no >= OUTPUT_SIZE)
                {
                    node_no = 0;
                    layer_no = 0;
                    initial_change = std::abs(initial_change);
                }
            }
        }
    }
}