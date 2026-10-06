#pragma once

#include <array>
#include <random>
#include <map>

struct NeuralGenome
{
public:
    static NeuralGenome random(std::mt19937_64& rng);

    void mutate_value(double& value, std::mt19937_64& rng);

    void mutate(std::mt19937_64& rng);

    void inherit();
    
    void learn(std::int64_t trader_worth_change, std::int64_t median_this_epoch, std::int64_t median_last_epoch);


    static constexpr int INPUT_SIZE = 11;
    static constexpr int HIDDEN_SIZE = 10;
    static constexpr int OUTPUT_SIZE = 10;

    std::array<double, INPUT_SIZE * HIDDEN_SIZE> input_weights;
    std::array<double, HIDDEN_SIZE> hidden_biases;

    std::array<double, HIDDEN_SIZE * OUTPUT_SIZE> output_weights;
    std::array<double, OUTPUT_SIZE> output_biases;

    std::map<int,double> memory_1;
    std::map<int,double> memory_2;
    std::map<int,double> memory_3;
    double plasticity = 0;
private:
    int layer_no = 0;
    int node_no = 0;
    int extra_node_no = 0;
    int change_no = 0;
    int max_changes = 5;
    double initial_change = 0.1;
};

struct NeuralGravestone
{
    NeuralGenome genome;
    int birth_tick;
    int death_tick;
    int id;
};

struct NeuralParentCandidate
{
    int fitness = 0;
    NeuralGenome genome;
};