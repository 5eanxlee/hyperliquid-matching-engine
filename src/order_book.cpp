#include <hyperliquid/order_book.h>
#include <hyperliquid/timestamp.h>

// Explicit template instantiation for array-based price levels
template class hyperliquid::OrderBook<hyperliquid::PriceLevelsArray>;
