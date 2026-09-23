#include "simulation_tests.hpp"
#include "simulation.hpp"
#include "test_utils.hpp"

#include <iostream>

void simulation_test_1()
{
    Simulation sim{0,1000};
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",100);
    sim.introduce_holdings(1,10);
    require(sim.reserve_order(1,1,Side::buy,10,100), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,10,100), "order not submitted");
    require(sim.get_trader_available_cash(0) == 1000, "bank balance incorrect");
    require(sim.get_trader_total_holdings(0,1) == 0, "bank holdings incorrect");
    require(sim.get_trader_available_cash(1) == 0, "trader balance incorrect");
    require(sim.get_trader_available_holdings(1,1) == 10, "incorrect holdings amount");
}

void simulation_test_2()
{
    Simulation sim{0,1000};
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",50);
    sim.introduce_holdings(1,10);
    require(sim.reserve_order(1,1,Side::buy,6,50), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,6,50), "order not submitted");
    require(sim.get_trader_available_cash(0) == 300, "bank balance incorrect");
    require(sim.get_trader_total_holdings(0,1) == 4, "bank holdings incorrect");
    require(sim.get_trader_available_cash(1) == 700, "trader balance incorrect");
    require(sim.get_trader_available_holdings(1,1) == 6, "incorrect holdings amount");
    sim.settle_accounts();
    require(sim.get_trader_available_cash(0) == 300, "bank balance incorrect 2");
    require(sim.get_trader_total_holdings(0,1) == 4, "bank holdings incorrect 2");
    require(sim.get_trader_available_cash(1) == 700, "trader balance incorrect 2");
    require(sim.get_trader_available_holdings(1,1) == 6, "incorrect holdings amount 2");
}

void simulation_test_3()
{
    Simulation sim{0,1000};
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",100);
    require(sim.reserve_order(1,1,Side::buy,10,50), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,10,50), "order not submitted");
    require(sim.cancel_order(0,1), "order not cancelled");
    require(sim.get_trader_available_cash(1) == 1000, "trader balance incorrect");
}

void simulation_test_4()
{
    Simulation sim{0,2000};
    sim.add_trader(1000,TraderType::none);
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",50);
    sim.introduce_holdings(1,10);
    require(sim.reserve_order(1,1,Side::buy,10,50), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,10,50), "order not submitted");
    require(sim.reserve_order(1,1,Side::sell,10,100), "second order not reserved");
    require(sim.submit_order(1,1,Side::sell,10,100), "second order not submitted");
    require(sim.reserve_order(2,1,Side::buy,4,100), "buy order not reserved");
    require(sim.submit_order(2,1,Side::buy,4,100), "buy order not submitted");
    require(sim.get_trader_total_holdings(1,1) == 6 ,"trader has incorrect holdings");
    require(sim.get_trader_available_holdings(1,1) == 0, "trader has incorrect available holdings before cancellation");
    require(sim.cancel_order(2,1), "sell order not cancelled");
    require(sim.get_trader_total_holdings(1,1) == 6 ,"trader has incorrect holdings after cancellation");
    require(sim.get_trader_available_holdings(1,1) == 6, "trader has incorrect available holdings after cancellation");
}

void simulation_test_5()
{
    Simulation sim{0,2000};
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",50);
    sim.introduce_holdings(1,15);
    require(sim.reserve_order(1,1,Side::buy,10,80), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,10,80), "order not submitted");
    require(sim.reserve_order(1,1,Side::buy, 5, 100), "second order not reserved");
    require(sim.submit_order(1,1,Side::buy,5,100), "second order not submitted");
    auto positions = sim.get_current_trader_positions(1);
    require(positions.at(1).quantity == 15, "positions quantity incorrect");
    require(positions.at(1).cost_basis == 750, "positions cost basis incorrect");
    sim.add_trader(1000,TraderType::none);
    require(sim.reserve_order(2,1,Side::buy,5,80), "second trader buy reservation failed");
    require(sim.submit_order(2,1,Side::buy,5,80), "second trader buy not submitted");
    require(sim.reserve_order(1,1,Side::sell,6,80), "sell not reserved");
    require(sim.submit_order(1,1,Side::sell,6,80), "sell not submitted");
    auto positions_2 = sim.get_current_trader_positions(1);
    require(positions_2.at(1).quantity == 10, "positions quantity incorrect 2");
    require(positions_2.at(1).cost_basis == 500, "positions cost basis incorrect 2");
    auto positions_3 = sim.get_current_trader_positions(2);
    require(positions_3.at(1).quantity == 5, "positions quantity incorrect 3");
    require(positions_3.at(1).cost_basis == 400, "positions cost basis incorrect 3");
}

void simulation_test_6()
{
    Simulation sim{0,1000};
    sim.set_recurring_costs(3,100);
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",200);
    sim.introduce_holdings(1,10);
    require(sim.reserve_order(1,1,Side::buy,5,200), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,5,200), "order not submitted");
    sim.tick();
    sim.tick();
    require(sim.get_trader_available_cash(1) == 0, "trader not broke");
    sim.tick();
    require(sim.get_trader_available_cash(1) == -100, "trader not bankrupt");
    auto& orders = sim.get_trader_active_orders(1);
    require(!orders.empty(), "no bankruptcy order created");
    require(orders.size() == 1, "too many orders");
    require(orders.begin()->second.side == Side::sell, "chose not to sell shares");
    require(orders.begin()->second.remaining_quantity == 1, "chose to sell wrong quantity");
    require(orders.begin()->second.limit_price == 180, "chose wrong price");
    require(sim.get_trader_available_holdings(1,1) == 4, "wrong number of available holdings");
}

void simulation_test_7()
{
    Simulation sim{0,500};
    sim.set_recurring_costs(1,100);
    sim.add_trader(500,TraderType::none);
    sim.add_instrument(1,"alpha",200);
    require(sim.reserve_order(1,1,Side::buy,2,100), "order not reserved 1");
    require(sim.submit_order(1,1,Side::buy,2,100), "order not submitted 1");
    require(sim.reserve_order(1,1,Side::buy,3,100), "order not reserved 2");
    require(sim.submit_order(1,1,Side::buy,3,100), "order not submitted 2");
    sim.tick();
    auto& orders = sim.get_trader_active_orders(1);
    require(!orders.empty(), "bankruptcy cancelled all orders");
    require(orders.size() == 1, "too many orders");
    require(orders.begin()->second.remaining_quantity == 3, "chose to cancel wrong order");
}

void simulation_test_8()
{
    Simulation sim{1,2000};
    sim.set_recurring_costs(1,100);
    sim.add_trader(1000,TraderType::none);
    sim.add_trader(1000,TraderType::none);
    sim.add_instrument(1,"alpha",200);
    sim.introduce_holdings(1,5);
    require(sim.reserve_order(1,1,Side::buy,5,200), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,5,200), "order not submitted");
    require(sim.reserve_order(2,1,Side::buy,4,200), "second trader order not reserved");
    require(sim.submit_order(2,1,Side::buy,4,200), "second trader order not submitted");
    sim.tick();
    require(sim.get_trader_available_holdings(2,1) == 1, "trader sold too many shares");
    require(sim.get_trader_available_cash(1) == 100, "trader has wrong balance");
}

void simulation_test_9()
{
    Simulation sim{1,100};
    sim.set_recurring_costs(1,90);
    sim.add_trader(100,TraderType::none);
    sim.tick();
    require(sim.get_trader_available_cash(0) == 90, "bank charge incorrect 1");
    require(sim.get_trader_available_cash(1) == 10, "trader incorrect cash 1");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 180, "bank charge incorrect 2");
    require(sim.get_trader_available_cash(1) == -80, "trader incorrect cash 2");
    require(sim.get_trader_health(1) == Health::recovering, "trader has incorrect health 1");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 270, "bank charge incorrect 3");
    require(sim.get_trader_available_cash(1) == -170, "trader incorrect cash 3");
    require(sim.get_trader_health(1) == Health::critical, "trader has incorrect health 2");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 100, "bank balance incorrect after liquidation");
    require(sim.get_trader_available_cash(1) == 0, "trader incorrect cash 4");
    require(sim.get_trader_health(1) == Health::failed, "trader has incorrect health 3");
}

void simulation_test_10()
{
    Simulation sim{1,1000};
    sim.add_trader(1000,TraderType::none);
    sim.set_recurring_costs(1,100);
    sim.add_instrument(1,"alpha",200);
    sim.introduce_holdings(1,5);
    require(sim.reserve_order(1,1,Side::buy,5,200), "trader failed to reserve");
    require(sim.submit_order(1,1,Side::buy,5,200), "trader failed to submit");
    require(sim.get_trader_available_cash(0) == 1000, "bank made no money off trade");
    require(sim.get_trader_available_cash(1) == 0, "trader was not charged");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 1100, "bank charge failed");
    require(sim.get_trader_available_cash(1) == -100, "trader was not charged 2");
    auto orders = sim.get_trader_active_orders(1);
    require(orders.size() == 1, "incorrect number of orders");
    require(orders.at(2).side == Side::sell, "incorrect side chosen");
    require(orders.at(2).limit_price == 180 ,"incorrect price used");
    require(orders.at(2).remaining_quantity == 1, "incorrect quantity set");
    require(sim.get_trader_health(1) == Health::recovering, "trader health incorrect");
    require(sim.get_trader_available_holdings(1,1) == 4, "trader has incorrect available holdings");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 1200, "bank charge failed 2");
    require(sim.get_trader_available_cash(1) == -200, "trader was not charged 3");
    orders = sim.get_trader_active_orders(1);
    require(orders.at(3).side == Side::sell, "incorrect side chosen 2");
    require(orders.at(3).remaining_quantity == 2, "incorrect quantity set 2");
    require(orders.at(3).limit_price == 180 ,"incorrect price used 2");
    require(sim.get_trader_health(1) == Health::critical, "trader health incorrect 2");
    require(sim.get_trader_available_holdings(1,1) == 2, "trader has incorrect available holdings 2");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 1000, "bank has incorrect balance");
    require(sim.get_trader_available_holdings(0,1) == 5, "bank has incorrect holdings");
    require(sim.get_trader_available_cash(1) == 0, "trader has cash");
    require(sim.get_trader_total_holdings(1,1) == 0, "trader has holdings");
    orders = sim.get_trader_active_orders(1);
    require(orders.size() == 0, "trader has orders");
}

void simulation_test_11()
{
    Simulation sim{1,500};
    sim.set_recurring_costs(1,90);
    sim.add_trader(100,TraderType::none);
    sim.add_trader(100,TraderType::none);
    sim.add_trader(300,TraderType::none);
    sim.tick();
    require(sim.get_trader_available_cash(0) == 270, "bank has incorrect balance");
    require(sim.get_trader_available_cash(1) == 10, "trader 1 has incorrect balance");
    require(sim.get_trader_health(1) == Health::healthy, "trader 1 has incorrect health");
    require(sim.get_trader_available_cash(2) == 10, "trader 2 has incorrect balance");
    require(sim.get_trader_health(2) == Health::healthy, "trader 2 has incorrect health");
    require(sim.get_trader_available_cash(3) == 210, "trader 3 has incorrect balance");
    require(sim.get_trader_health(3) == Health::healthy, "trader 3 has incorrect health");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 540, "bank has incorrect balance 2");
    require(sim.get_trader_available_cash(1) == -80, "trader 1 has incorrect balance 2");
    require(sim.get_trader_health(1) == Health::recovering, "trader 1 has incorrect health 2");
    require(sim.get_trader_available_cash(2) == -80, "trader 2 has incorrect balance 2");
    require(sim.get_trader_health(2) == Health::recovering, "trader 2 has incorrect health 2");
    require(sim.get_trader_available_cash(3) == 120, "trader 3 has incorrect balance 2");
    require(sim.get_trader_health(3) == Health::healthy, "trader 3 has incorrect health 2");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 810, "bank has incorrect balance 3");
    require(sim.get_trader_available_cash(1) == -170, "trader 1 has incorrect balance 3");
    require(sim.get_trader_health(1) == Health::critical, "trader 1 has incorrect health 3");
    require(sim.get_trader_available_cash(2) == -170, "trader 2 has incorrect balance 3");
    require(sim.get_trader_health(2) == Health::critical, "trader 2 has incorrect health 3");
    require(sim.get_trader_available_cash(3) == 30, "trader 3 has incorrect balance 3");
    require(sim.get_trader_health(3) == Health::healthy, "trader 3 has incorrect health 3");
    sim.tick();
    require(sim.get_trader_available_cash(0) == 560, "bank has incorrect balance 4");
    require(sim.get_trader_available_cash(1) == 0, "trader 1 has incorrect balance 4");
    require(sim.get_trader_health(1) == Health::failed, "trader 1 has incorrect health 4");
    require(sim.get_trader_available_cash(2) == 0, "trader 2 has incorrect balance 4");
    require(sim.get_trader_health(2) == Health::failed, "trader 2 has incorrect health 4");
    require(sim.get_trader_available_cash(3) == -60, "trader 3 has incorrect balance 4");
    require(sim.get_trader_health(3) == Health::recovering, "trader 3 has incorrect health 4");
}

void simulation_test_12()
{
    Simulation sim{1,6000};
    sim.add_trader(1000,TraderType::none);
    sim.add_trader(5000,TraderType::none);
    sim.set_recurring_costs(1,800);
    sim.add_instrument(1,"alpha", 100);
    sim.introduce_holdings(1,3);
    require(sim.reserve_order(2,1,Side::buy,3,100), "trader failed to reserve for trade");
    require(sim.submit_order(2,1,Side::buy,3,100), "trader failed to submit order");
    require(sim.reserve_order(1,1,Side::buy,1,100), "trader 1 failed to reserve order 1");
    require(sim.submit_order(1,1,Side::buy,1,100), "trader 1 failed to submit order 1");
    require(sim.reserve_order(1,1,Side::buy,2,100), "trader 1 failed to reserve order 2");
    require(sim.submit_order(1,1,Side::buy,2,100), "trader 1 failed to submit order 2");
    require(sim.reserve_order(1,1,Side::buy,4,100), "trader 1 failed to reserve order 3");
    require(sim.submit_order(1,1,Side::buy,4,100), "trader 1 failed to submit order 3");
    sim.tick();
    auto orders = sim.get_trader_active_orders(1);
    require(orders.size() == 2, "did not cancel order");
    require(orders.at(2).remaining_quantity == 1, "trader submitted wrong quantity");
    require(orders.at(3).remaining_quantity == 2, "trader submitted wrong quantity 2");
    require(sim.reserve_order(2,1,Side::sell,3,100), "trader failed ot reserve sell");
    require(sim.submit_order(2,1,Side::sell,3,100), "trader failed to submit sell order");
    require(sim.get_trader_available_cash(1) == -100, "trader has money");
    require(sim.get_trader_available_holdings(1,1) == 3, "trader has incorrect holdings");
    orders = sim.get_trader_active_orders(1);
    require(orders.size() == 0, "trader has active orders");
}

void simulation_test_13()
{
    Simulation sim{1,2000};
    sim.add_trader(1000,TraderType::none);
    sim.add_trader(1000,TraderType::none);
    sim.set_recurring_costs(2,100);
    sim.add_instrument(1,"alpha",200);
    sim.introduce_holdings(1,10);
    require(sim.reserve_order(1,1,Side::buy,5,200), "order not reserved");
    require(sim.submit_order(1,1,Side::buy,5,200), "order not submitted");
    sim.tick();
    require(sim.get_trader_available_cash(1) == 0, "incorrect trader balance");
    require(sim.get_trader_health(1) == Health::healthy, "trader health incorrect");
    sim.tick();
    require(sim.get_trader_available_cash(1) == -100, "incorrect trader balance 2");
    require(sim.get_trader_health(1) == Health::recovering, "trader health incorrect 2");
    require(sim.get_trader_available_holdings(1,1) == 4, "incorrect holding amount");
    require(sim.reserve_order(2,1,Side::buy,1,180), "order not reserved 2");
    require(sim.submit_order(2,1,Side::buy,1,180), "order not submitted 2");
    sim.tick();
    require(sim.get_trader_available_cash(1) == 80, "trader has wrong balance after trade");
    require(sim.get_trader_health(1) == Health::healthy, "trader health not recovered");
}

void run_simulation_tests()
{
    run_test("simulation test 1", simulation_test_1);
    run_test("simulation test 2", simulation_test_2);
    run_test("simulation test 3", simulation_test_3);
    run_test("simulation test 4", simulation_test_4);
    run_test("simulation test 5", simulation_test_5);
    run_test("simulation test 6", simulation_test_6);
    run_test("simulation test 7", simulation_test_7);
    run_test("simulation test 8", simulation_test_8);
    run_test("simulation test 9", simulation_test_9);
    run_test("simulation test 10", simulation_test_10);
    run_test("simulation test 11", simulation_test_11);
    run_test("simulation test 12", simulation_test_12);
    run_test("simulation test 13", simulation_test_13);
    std::cout << "all tests passed";
}