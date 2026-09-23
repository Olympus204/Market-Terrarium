#include "order_book_tests.hpp"
#include "market_tests.hpp"
#include "trader_tests.hpp"
#include "simulation_tests.hpp"

#include <iostream>

int main()
{
    std::cout << "order book tests \n";
    run_order_book_tests();
    std::cout << "\n market tests \n";
    run_market_tests();
    std::cout << "\n trader tests \n";
    run_trader_tests();
    std::cout << "\n simulation tests \n";
    run_simulation_tests();
}