#include "trader_tests.hpp"
#include "trader.hpp"
#include "test_utils.hpp"

#include <iostream>
#include <stdexcept>

void trader_test_1()
{
    SimpleTrader trader{1,1000,TraderType::none};
    auto available_cash = trader.get_available_cash();
    require(available_cash == 1000, "trader initialisation error");
}

void trader_test_2()
{
    SimpleTrader trader{1,1000,TraderType::none};
    require(trader.reserve_cash(400), "cash reservation failed");
    require(trader.get_available_cash() == 600, "incorrect cash reservation");
}

void trader_test_3()
{
    SimpleTrader trader{1,1000,TraderType::none};
    require(trader.reserve_cash(800), "cash reservation failed");
    require(trader.reserve_cash(300) == false, "over allocation failure");
    require(trader.get_available_cash() == 200, "incorrect cash reservation");
}

void trader_test_4()
{
    SimpleTrader trader{1,1000,TraderType::none};
    require(trader.reserve_cash(600), "cash reservation failed");
    require(trader.release_cash(250), "cash release failed");
    require(trader.get_available_cash() == 650, "incorrect cash reservation");
}

void trader_test_5()
{
    SimpleTrader trader{1,1000,TraderType::none};
    require(trader.reserve_cash(0) == false, "cash allocation failure 1");
    require(trader.reserve_cash(-10) == false, "cash allocation failure 2");
    require(trader.release_cash(0) == false, "cash release failure 1");
    require(trader.release_cash(-10) == false, "cash release failure 2");
    trader.reserve_cash(10);
    require(trader.release_cash(100) == false, "cash release failure 3");
}

void trader_test_6()
{
    SimpleTrader trader{1,1000,TraderType::none};
    require(trader.get_available_holding(999) == 0, "inaccurate holdings");
}

void trader_test_7()
{
    SimpleTrader trader{1,1000,TraderType::none};
    trader.add_holding(1,10,0);
    require(trader.reserve_holding(1,4), "holding reservation failure 1");
    require(trader.get_available_holding(1) == 6, "holding availablility error 1");
    require(trader.reserve_holding(1,3), "holding reservation failure 2");
    require(trader.get_available_holding(1) == 3, "holding availablility error 2");
}

void trader_test_8()
{
    SimpleTrader trader{1,1000,TraderType::none};
    trader.add_holding(1,10,0);
    trader.reserve_holding(1,7);
    require(trader.reserve_holding(1,4) == false, "holding reservation error");
    require(trader.get_available_holding(1) == 3, "holding ammount error 1");
    require(trader.release_holding(1,2), "Holding release error");
    require(trader.get_available_holding(1) == 5, "holding ammount error 2");
}

void trader_test_9()
{
    SimpleTrader trader{1,1000,TraderType::none};
    trader.add_holding(1,10,0);
    require(trader.release_holding(2,10) == false, "unheld holding error");
    require(trader.release_holding(1,10) == false, "trader release holding error 1");
    require(trader.release_holding(1,0) == false, "trader release holding error 2");
    require(trader.release_holding(1,-10) == false, "trader release holding error 3");
}

void trader_test_10()
{
    SimpleTrader trader{1,1000,TraderType::none};

    require(trader.reserve_cash(400), "cash reservation failed");
    require(trader.release_cash(400), "cash release failed");
    require(trader.get_available_cash() == 1000, "cash not fully restored");

    trader.add_holding(1,10,0);
    require(trader.reserve_holding(1,6), "holding reservation failed");
    require(trader.release_holding(1,6), "holding release failed");
    require(trader.get_available_holding(1) == 10, "holdings not fully restored");
}

void confirmation_test_1()
{
    SimpleTrader trader{1,1000,TraderType::none};
    require(trader.confirm_order(0,1,Side::buy,100,5), "confirmation failed");
    require(!trader.confirm_order(0,1,Side::buy,100,5), "duplicate passed");
    require(!trader.confirm_order(1,1,Side::buy,100,0), "zero quantity passed");
    require(!trader.confirm_order(2,1,Side::buy,0,5), "zero price passed");
    require(!trader.confirm_order(3,0,Side::buy,100,5), "invalide instrument passed");
    require(!trader.confirm_order(-1,1,Side::buy,100,5), "negative order id passed");
}

void settlement_test_1()
{
    SimpleTrader trader{1,1000,TraderType::none};

    require(trader.reserve_cash(500), "cash reservation failed");
    require(trader.confirm_order(0,1,Side::buy,100,5), "order confirmation failed");
    require(trader.settle_buy(0,90,2), "partial buy settlement failed");
    require(trader.get_available_cash() == 520, "incorrect available cash after partial buy");
    require(trader.get_available_holding(1) ==2, "incorrect holdings after partial buy");
    require(trader.settle_buy(0,100,3), "final buy settlement failed");
    require(trader.get_available_cash() == 520, "incorrect available cash after complete buy");
    require(trader.get_available_holding(1) == 5, "incorrect holdings after complete buy");
    require(!trader.settle_buy(0,100,1), "completed order still exists");
}

void settlement_test_2()
{
    SimpleTrader trader{1,1000,TraderType::none};

    require(trader.reserve_cash(500), "cash reservation failed");
    require(trader.confirm_order(0,1,Side::buy,100,5), "order confirmation failed");
    require(!trader.settle_buy(0,190,2), "incorrect buy settlement passed");
    require(trader.get_available_cash() == 500, "incorrect balance");
    require(trader.get_available_holding(1) == 0, "incorrect holding amount");
    require(trader.settle_buy(0,90,2), "partial buy settlement failed");
}

void settlement_test_3()
{
    SimpleTrader trader{1,1000,TraderType::none};

    trader.add_holding(1,5,0);
    require(trader.reserve_holding(1,5), "holding reservation failed");
    require(trader.confirm_order(1,1,Side::sell,100,5), "order confirmation failed");
    require(trader.settle_sell(1, 110, 3), "sell settlement failed");
    require(trader.get_available_cash() == 1330, "incorrect available cash");
    require(trader.get_available_holding(1) == 0, "incorrect available holdings");
    require(trader.settle_sell(1, 120, 2), "second sell settlement failed");
    require(trader.get_available_cash() == 1570, "incorrect final cash");
    require(!trader.settle_sell(1, 1000, 1), "sold nonexistant holdings");
}

void settlement_test_4()
{
    SimpleTrader trader{1,1000,TraderType::none};

    trader.add_holding(1,5,0);
    require(trader.reserve_holding(1,5), "holding reservation failed");
    require(trader.confirm_order(1,1,Side::sell,100,5), "order confirmation failed");
    require(!trader.settle_sell(1, 10, 3), "sell settlement passed");
    require(trader.settle_sell(1, 110, 3), "sell settlement failed");
    require(trader.get_available_cash() == 1330, "incorrect available cash");
    require(trader.get_available_holding(1) == 0, "incorrect available holdings");
}

void cancelation_test_1()
{
    SimpleTrader trader{1,1000,TraderType::none};

    require(trader.reserve_cash(500), "cash not reserved");
    require(trader.confirm_order(0,1,Side::buy,100,5), "order not confirmed");
    require(trader.confirm_cancel(0,1,Side::buy,100,5), "order not cancelled");
    require(trader.get_available_cash() == 1000, "available cash incorrect");
    require(trader.get_available_holding(1) == 0, "incorrect holdings");
    require(!trader.confirm_cancel(0,1,Side::buy,100,5), "canceled order twice");
}

void cancelation_test_2()
{
    SimpleTrader trader{1,1000,TraderType::none};

    require(trader.reserve_cash(500), "cash not reserved");
    require(trader.confirm_order(0,1,Side::buy,100,5), "order not confirmed");
    require(trader.settle_buy(0, 90, 2), "partial settlement failure");
    require(trader.get_available_cash() == 520, "incorrect cash balance");
    require(trader.get_available_holding(1) == 2, "incorrect holdings amount");
    require(trader.confirm_cancel(0,1,Side::buy,100,3), "order not cancelled");
    require(trader.get_available_cash() == 820, "final cash balance incorrect");
    require(trader.get_available_holding(1) == 2, "final holding quantity incorrect");
}

void cancelation_test_3()
{
    SimpleTrader trader{1,1000,TraderType::none};

    trader.add_holding(1,10,0);
    require(trader.reserve_holding(1,6), "holdings not reserved");
    require(trader.get_available_holding(1) == 4, "holding amount incorrect");
    require(trader.confirm_order(0,1,Side::sell,100,6), "order not confirmed");
    require(trader.confirm_cancel(0,1,Side::sell,100,6), "order not cancelled");
    require(trader.get_available_holding(1) == 10, "incorrect final holdings amount");
    require(trader.get_available_cash() == 1000 ,"cash balance incorrect");
}

void cancelation_test_4()
{
    SimpleTrader trader{1,1000,TraderType::none};

    trader.add_holding(1,10,0);
    require(trader.reserve_holding(1,6), "holdings not reserved");
    require(trader.get_available_holding(1) == 4, "holding amount incorrect");
    require(trader.confirm_order(0,1,Side::sell,100,6), "order not confirmed");
    require(!trader.confirm_cancel(0,1,Side::buy,100,6), "order cancelled with wrong side");
    require(!trader.confirm_cancel(1,1,Side::sell,100,6), "order cancelled with wrong order id");
    require(!trader.confirm_cancel(0,0,Side::sell,100,6), "order cancelled with wrong instrument id");
    require(!trader.confirm_cancel(0,1,Side::sell,10,6), "order cancelled with wrong price");
    require(!trader.confirm_cancel(0,1,Side::sell,100,9), "order cancelled with wrong quantity");
    require(trader.get_available_holding(1) == 4, "holding amount incorrect 2");
    require(trader.confirm_cancel(0,1,Side::sell,100,6), "order not cancelled");
    require(trader.get_available_holding(1) == 10, "incorrect final holdings amount");
    require(trader.get_available_cash() == 1000 ,"cash balance incorrect");
}

void cancelation_test_5()
{
    SimpleTrader trader{1,1000,TraderType::none};

    trader.add_holding(1,10,0);
    require(trader.reserve_holding(1,6), "holdings not reserved");
    require(trader.get_available_holding(1) == 4, "holding amount incorrect");
    require(trader.confirm_order(0,1,Side::sell,100,6), "order not confirmed");
    require(trader.settle_sell(0,100,2), "sell order not settled");
    require(trader.get_available_holding(1) == 4, "holding amount incorrect 2");
    require(trader.get_available_cash() == 1200, "incorrect cash balance");
    require(trader.confirm_cancel(0,1,Side::sell,100,4), "order not cancelled");
    require(trader.get_available_holding(1) == 8, "final holding amount incorrect");
    require(trader.get_available_cash() == 1200, "incorrect final cash balance");
}

void bankruptcy_test_1()
{
    SimpleTrader trader{1,1000,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 100;
    trader.update_observed_prices(prices);
    require(trader.reserve_cash(100), "failed cash reserve 1");
    require(trader.confirm_order(0,1,Side::buy,100,1), "failed to submit order one");
    require(trader.reserve_cash(260), "failed to reserve cash 2");
    require(trader.confirm_order(1,1,Side::buy,260,1), "failed to submit order 2");
    require(trader.reserve_cash(400), "failed to reserve cash 3");
    require(trader.confirm_order(2,1,Side::buy,400,1), "failed to submit order 3");
    require(trader.apply_cost(490), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::cancel, "chose to do the wrong thing");
    require(choice.order_id == 1, "chose to cancel wrong order");
}

void bankruptcy_test_2()
{
    SimpleTrader trader{1,1000,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 100;
    trader.update_observed_prices(prices);
    require(trader.reserve_cash(100), "failed cash reserve 1");
    require(trader.confirm_order(0,1,Side::buy,100,1), "failed to submit order one");
    require(trader.reserve_cash(260), "failed to reserve cash 2");
    require(trader.confirm_order(1,1,Side::buy,260,1), "failed to submit order 2");
    require(trader.reserve_cash(400), "failed to reserve cash 3");
    require(trader.confirm_order(2,1,Side::buy,400,1), "failed to submit order 3");
    require(trader.apply_cost(740), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::cancel, "chose to do the wrong thing");
    require(choice.order_id == 2, "chose to cancel wrong order");
}

void bankruptcy_test_3()
{
    SimpleTrader trader{1,1000,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 100;
    trader.update_observed_prices(prices);
    trader.add_holding(1,10,100);
    require(trader.reserve_cash(100), "failed cash reserve 1");
    require(trader.confirm_order(0,1,Side::buy,100,1), "failed to submit order one");
    require(trader.apply_cost(1040), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::cancel, "chose not to cancel order");
}

void bankruptcy_test_4()
{
    SimpleTrader trader{1,1,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 567;
    prices[2] = 112;
    trader.update_observed_prices(prices);
    trader.add_holding(1,1,567);
    trader.add_holding(2,5,112);
    require(trader.apply_cost(501), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::sell, "chose not to sell");
    require(choice.instrument_id == 2, "chose to sell wrong instrument");
    require(choice.quantity == 5, "chose wrong quantity");
}

void bankruptcy_test_5()
{
    SimpleTrader trader{1,1,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 100;
    trader.update_observed_prices(prices);
    trader.add_holding(1,2,100);
    require(trader.apply_cost(92), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::sell, "chose not to sell");
    require(choice.quantity == 2, "chose wrong quantity");
}

void bankruptcy_test_6()
{
    SimpleTrader trader{1,1,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 180;
    prices[2] = 360;
    prices[3] = 270;
    trader.update_observed_prices(prices);
    trader.add_holding(1,1,180);
    trader.add_holding(2,1,360);
    trader.add_holding(3,1,270);
    require(trader.apply_cost(501), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::sell, "chose not to sell");
    require(choice.instrument_id == 2, "chose to sell wrong holdings");

}

void bankruptcy_test_7()
{
    SimpleTrader trader{1,1,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 180;
    prices[2] = 360;
    trader.update_observed_prices(prices);
    trader.add_holding(1,10,180);
    trader.add_holding(2,5,360);
    require(trader.reserve_holding(1,10),"failed to reserve holding");
    require(trader.reserve_holding(2,5),"failed to reserve holding 2");
    require(trader.apply_cost(101), "failed to pay fine");
    auto choice = trader.bankruptcy_check();
    require(choice.type == ActionType::none, "chose to do something");
}

void observation_test()
{
    SimpleTrader trader{1,1,TraderType::none};
    std::unordered_map<int,double> prices;
    prices[1] = 100;
    trader.update_observed_prices(prices);
    auto observation = trader.observed_price(1);
    require(observation.at(0) == 100, "incorrect observation 1");
    prices[1] = 105;
    trader.update_observed_prices(prices);
    observation = trader.observed_price(1);
    require(observation.at(0) == 100, "incorrect observation 2");
    require(observation.at(1) == 105, "incorrect observation 3");
    prices[1] = 110;
    trader.update_observed_prices(prices);
    observation = trader.observed_price(1);
    require(observation.at(0) == 100, "incorrect observation 4");
    require(observation.at(1) == 105, "incorrect observation 5");
    require(observation.at(2) == 110, "incorrect observation 6");
    prices[1] = 110;
    trader.update_observed_prices(prices);
    observation = trader.observed_price(1);
    require(observation.at(0) == 100, "incorrect observation 7");
    require(observation.at(1) == 105, "incorrect observation 8");
    require(observation.at(2) == 110, "incorrect observation 9");
    require(observation.at(3) == 110, "incorrect observation 10");
}


void run_trader_tests()
{
    run_test("trader test 1", trader_test_1);
    run_test("trader test 2", trader_test_2);
    run_test("trader test 3", trader_test_3);
    run_test("trader test 4", trader_test_4);
    run_test("trader test 5", trader_test_5);
    run_test("trader test 6", trader_test_6);
    run_test("trader test 7", trader_test_7);
    run_test("trader test 8", trader_test_8);
    run_test("trader test 9", trader_test_9);
    run_test("trader test 10", trader_test_10);
    run_test("confirmation test 1", confirmation_test_1);
    run_test("settlement test 1", settlement_test_1);
    run_test("settlement test 2", settlement_test_2);
    run_test("settlement test 3", settlement_test_3);
    run_test("settlement test 4", settlement_test_4);
    run_test("cancelation test 1", cancelation_test_1);
    run_test("cancelation test 2", cancelation_test_2);
    run_test("cancelation test 3", cancelation_test_3);
    run_test("cancelation test 4", cancelation_test_4);
    run_test("cancelation test 5", cancelation_test_5);
    run_test("bankruptcy test 1", bankruptcy_test_1);
    run_test("bankruptcy test 2", bankruptcy_test_2);
    run_test("bankruptcy test 3", bankruptcy_test_3);
    run_test("bankruptcy test 4", bankruptcy_test_4);
    run_test("bankruptcy test 5", bankruptcy_test_5);
    run_test("bankruptcy test 6", bankruptcy_test_6);
    run_test("bankruptcy test 7", bankruptcy_test_7);
    run_test("observation test", observation_test);
    std::cout << "all tests passed";
}