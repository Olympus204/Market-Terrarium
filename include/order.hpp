#pragma once

enum class Side
{
    buy,
    sell
};

struct Order
{
    int id;
    int trader_id;
    Side side;
    int price;
    int quantity;
};