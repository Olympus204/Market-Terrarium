#include "market_tests.hpp"
#include "market.hpp"
#include "test_utils.hpp"

#include <iostream>
#include <stdexcept>

void market_test_1()
{
    Market market;
    market.add_instrument(1,"Alpha",100);
    market.submit_order(1, 1, Side::sell, 100, 1);
    market.submit_order(1, 2, Side::sell, 100, 2);
    market.submit_order(1, 3, Side::sell, 100, 3);
    market.submit_order(1, 4, Side::buy, 100, 6);

    const auto& history = market.get_history();

    require(history.size() == 3, "history size mismatch");
    require(history[0].trade_id == 0, "trade id mismatch 1");
    require(history[0].buyer_id == 4, "buy id mismatch 1");
    require(history[0].seller_id == 1, "sell id mismatch 1");
    require(history[0].quantity == 1, "quantity mismatch 1");
    require(history[0].price == 100, "price mismatch 1");
    require(history[0].instrument_id == 1, "instrument id mismatch 1");

    require(history[1].trade_id == 1, "trade id mismatch 2");
    require(history[1].buyer_id == 4, "buy id mismatch 2");
    require(history[1].seller_id == 2, "sell id mismatch 2");
    require(history[1].quantity == 2, "quantity mismatch 2");
    require(history[1].price == 100, "price mismatch 2");
    require(history[1].instrument_id == 1, "instrument id mismatch 2");

    require(history[2].trade_id == 2, "trade id mismatch 3");
    require(history[2].buyer_id == 4, "buy id mismatch 3");
    require(history[2].seller_id == 3, "sell id mismatch 3");
    require(history[2].quantity == 3, "quantity mismatch 3");
    require(history[2].price == 100, "price mismatch 3");
    require(history[2].instrument_id == 1, "instrument id mismatch 3");
}

void market_test_2()
{
    Market market;
    market.add_instrument(1,"Alpha",100);
    market.add_instrument(2,"Beta",100);

    market.submit_order(1,1,Side::buy,100,1);
    market.submit_order(1,2,Side::sell,100,1);

    market.submit_order(2,1,Side::buy,100,1);
    market.submit_order(2,2,Side::sell,100,1);

    const auto& history = market.get_history();

    require(history.size() == 2, "history size mismatch");

    require(history[0].trade_id == 0, "trade id mismatch");
    require(history[0].instrument_id == 1, "instrument id mismatch");

    require(history[1].trade_id == 1, "trade id mismatch");
    require(history[1].instrument_id == 2, "instrument id mismatch");
}

void market_test_3()
{
    Market market;
    market.add_instrument(1, "Alpha", 100);

    bool threw = false;

    try
    {
        market.submit_order(999, 1, Side::buy, 100, 1);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    require(threw, "invalid instrument id did not throw");
    require(market.get_history().empty(), "invalid order changed trade history");
}

void cancel_test_1()
{
    Market market;
    market.add_instrument(1, "Alpha", 100);
    market.submit_order(1,1,Side::buy,100,5);
    auto cancel = market.cancel_order(0,1);
    require(cancel.has_value(), "order was not cancelled");
    require(cancel->remaining_quantity == 5, "cancellation quantity error");
}

void cancel_test_2()
{
    Market market;
    market.add_instrument(1, "Alpha", 100);
    market.submit_order(1,1,Side::buy,100,5);
    market.submit_order(1,2,Side::sell,100,3);
    auto cancel = market.cancel_order(0,1);
    require(cancel.has_value(), "order was not cancelled");
    require(cancel->remaining_quantity == 2, "cancellation quantity error");
}

void cancel_test_3()
{
    Market market;
    market.add_instrument(1, "Alpha", 100);
    market.submit_order(1,1,Side::buy,100,5);
    market.submit_order(1,2,Side::sell,100,5);
    auto cancel = market.cancel_order(0,1);
    require(cancel == std::nullopt, "cancellation quantity error");
}

void cancel_test_4()
{
    Market market;
    market.add_instrument(1, "Alpha", 100);
    market.add_instrument(2, "Beta", 100);
    market.submit_order(1,1,Side::buy,100,5);
    market.submit_order(2,2,Side::buy,100,5);
    auto cancel = market.cancel_order(0,1);
    require(cancel.has_value(), "order was not cancelled");
    require(cancel->instrument_id == 1, "cancelled wrong instrument");
    const auto& active_orders = market.get_active_orders();
    require(active_orders.size() == 1, "active orders size error");
    require(active_orders.at(1).instrument_id == 2, "remaining order has wrong instrument");
}

void run_market_tests()
{
    run_test("market test 1", market_test_1);
    run_test("market test 2", market_test_2);
    run_test("market test 3", market_test_3);
    run_test("cancel test 1", cancel_test_1);
    run_test("cancel test 2", cancel_test_2);
    run_test("cancel test 3", cancel_test_3);
    run_test("cancel test 4", cancel_test_4);
    std::cout << "all tests passed";
}