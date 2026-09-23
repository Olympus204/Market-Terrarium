#pragma once

struct Trade
{
    int trade_id;
    int instrument_id;
    int buy_id;
    int sell_id;
    int buyer_id;
    int seller_id;
    int quantity;
    int price;
};

struct Temp_trade
{
    int buy_id;
    int sell_id;
    int buyer_id;
    int seller_id;
    int quantity;
    int price;
};