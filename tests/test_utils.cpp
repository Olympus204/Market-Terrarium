#include "test_utils.hpp"

#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void run_test(const char* name, void (*test)())
{
    std::cout << "Running " << name << "... ";
    test();
    std::cout << "passed\n";
}