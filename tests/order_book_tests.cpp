#include "order_book_tests.hpp"
#include "order_book.hpp"
#include "trades.hpp"
#include "test_utils.hpp"

#include <iostream>
#include <stdexcept>

void order_book_test_1()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::buy,100,5});
    auto new_trades = book.process_order({2,2,Side::sell,100,5});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 1, "trade size error");

    require(trades[0].buy_id == 1, "trade buy id error");
    require(trades[0].buyer_id == 1, "trades buyer id error");

    require(trades[0].sell_id == 2, "trades sell id error");
    require(trades[0].seller_id == 2, "trades seller id error");

    require(trades[0].price == 100, "trades price error");
    require(trades[0].quantity == 5, "trades quantity error");
}

void order_book_test_2()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::sell,90,3});
    auto new_trades = book.process_order({2,2,Side::buy,100,5});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 1, "trade size error");

    require(trades[0].buy_id == 2, "trade buy id error");
    require(trades[0].buyer_id == 2, "trades buyer id error");

    require(trades[0].sell_id == 1, "trades sell id error");
    require(trades[0].seller_id == 1, "trades seller id error");

    require(trades[0].price == 90, "trades price error");
    require(trades[0].quantity == 3, "trades quantity error");

    BuyBook buys = book.get_buys();

    require(buys.size() == 1, "buys size error");
    require(buys.begin()->second.front().id == 2, "buys id error");
    require(buys.begin()->second.front().quantity == 2, "buys quantity error");
}

void order_book_test_3()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::buy,100,3});
    auto new_trades = book.process_order({2,2,Side::sell,90,5});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 1, "trade size error");

    require(trades[0].buy_id == 1, "trade buy id error");
    require(trades[0].buyer_id == 1, "trades buyer id error");

    require(trades[0].sell_id == 2, "trades sell id error");
    require(trades[0].seller_id == 2, "trades seller id error");

    require(trades[0].price == 100, "trades price error");
    require(trades[0].quantity == 3, "trades quantity error");

    SellBook sells = book.get_sells();

    require(sells.size() == 1, "sells size error");
    require(sells.begin()->second.front().id == 2, "sells id error");
    require(sells.begin()->second.front().quantity == 2, "sells quantity error");
}

void order_book_test_4()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::sell,100,1});
    book.process_order({2,2,Side::sell,100,2});
    book.process_order({3,3,Side::sell,100,3});
    auto new_trades = book.process_order({4,4,Side::buy,100,6});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 3, "trade size error");

    require(trades[0].sell_id == 1, "trade sell id error 1");
    require(trades[0].quantity == 1, "trade quantity error 1");

    require(trades[1].sell_id == 2, "trade sell id error 2");
    require(trades[1].quantity == 2, "trade quantity error 2");

    require(trades[2].sell_id == 3, "trade sell id error 3");
    require(trades[2].quantity == 3, "trade quantity error 3");
}

void order_book_test_5()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::sell,100,1});
    book.process_order({2,2,Side::sell,95,1});
    book.process_order({3,3,Side::sell,90,1});
    auto new_trades = book.process_order({4,4,Side::buy,100,3});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 3, "trade size error");

    require(trades[0].price == 90, "trade order error 1");

    require(trades[1].price == 95, "trades order error 2");

    require(trades[2].price == 100, "trades order error 3");
}

void order_book_test_6()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::buy,100,1});
    book.process_order({2,2,Side::buy,105,1});
    book.process_order({3,3,Side::buy,110,1});
    auto new_trades = book.process_order({4,4,Side::sell,100,3});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 3, "trade size error");

    require(trades[0].price == 110, "trade order error 1");

    require(trades[1].price == 105, "trades order error 2");

    require(trades[2].price == 100, "trades order error 3");
}

void order_book_test_7()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::sell,105,5});
    auto new_trades = book.process_order({2,2,Side::buy,100,5});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 0, "trade size error");
    BuyBook buys = book.get_buys();
    SellBook sells = book.get_sells();

    require(sells.size() == 1, "sells size error");
    require(sells.begin()->second.front().id == 1, "sells id error");
    require(sells.begin()->second.front().quantity == 5, "sells quantity error");

    require(buys.size() == 1, "buys size error");
    require(buys.begin()->second.front().id == 2, "buys id error");
    require(buys.begin()->second.front().quantity == 5, "buys quantity error");
}

void cancel_order_test_1()
{
    OrderBook book;
    book.process_order({1,1,Side::buy,100,5});
    auto canceled = book.cancel_order(1);
    require(canceled.has_value(), "order was not canceled");
    require(canceled->quantity == 5, "canceled quantity mismatch");

    BuyBook buys = book.get_buys();

    require(buys.size() == 0, "buys size mismatch");
}

void cancel_order_test_2()
{
    OrderBook book;
    book.process_order({1,1,Side::buy,100,5});
    book.process_order({2,2,Side::sell,100,3});
    auto canceled = book.cancel_order(1);
    require(canceled.has_value(), "order was not canceled");
    require(canceled->quantity == 2, "canceled quantity mismatch");
}

void cancel_order_test_3()
{
    OrderBook book;
    book.process_order({1,1,Side::buy,100,5});

    auto canceled = book.cancel_order(2);

    require(canceled == std::nullopt, "canceled incorrect id error");
}

void cancel_order_test_4()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::sell,100,1});
    book.process_order({2,2,Side::sell,95,1});
    book.process_order({3,3,Side::sell,90,1});
    auto canceled = book.cancel_order(2);
    require(canceled.has_value(), "order was not canceled");
    auto new_trades = book.process_order({4,4,Side::buy,100,3});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 2, "trade size error");

    require(trades[0].price == 90, "trade order error 1");

    require(trades[1].price == 100, "trades order error 2");
}

void cancel_order_test_5()
{
    OrderBook book;
    std::vector<Temp_trade> trades;
    book.process_order({1,1,Side::sell,100,1});
    book.process_order({2,2,Side::sell,100,1});
    book.process_order({3,3,Side::sell,100,1});
    auto canceled = book.cancel_order(2);
    require(canceled.has_value(), "order was not canceled");
    auto new_trades = book.process_order({4,4,Side::buy,100,3});
    trades.insert(trades.end(), new_trades.begin(), new_trades.end());
    require(trades.size() == 2, "trade size error");

    require(trades[0].sell_id == 1, "trade order error 1");

    require(trades[1].sell_id == 3, "trades order error 2");
}

void run_order_book_tests()
{
    run_test("order book test 1", order_book_test_1);
    run_test("order book test 2", order_book_test_2);
    run_test("order book test 3", order_book_test_3);
    run_test("order book test 4", order_book_test_4);
    run_test("order book test 5", order_book_test_5);
    run_test("order book test 6", order_book_test_6);
    run_test("order book test 7", order_book_test_7);
    run_test("cancel order test 1", cancel_order_test_1);
    run_test("cancel order test 2", cancel_order_test_2);
    run_test("cancel order test 3", cancel_order_test_3);
    run_test("cancel order test 4", cancel_order_test_4);
    run_test("cancel order test 5", cancel_order_test_5);
    std::cout << "all tests passed";
}