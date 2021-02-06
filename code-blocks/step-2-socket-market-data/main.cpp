#include <iostream>
#include "../../include/hitbit-socket-market-data.hpp"

int main() {
    {
        hitbit_cpp_api::SocketMarketData hitbit;

        std::cout.precision(8);

        hitbit.on_start = [&]() {
            std::cout << "on_start" << std::endl;
            hitbit.subscribe_ticker("ETHUSD");
            hitbit.subscribe_ticker("BTCUSD");
            hitbit.subscribe_ticker("ETHBTC");
        };

        hitbit.on_ticker = [&](
                const std::string &symbol,
                const double bid,
                const double ask,
                const double tick_size,
                const double timestamp) {
            std::cout << symbol << " tz " << tick_size << " bid " << bid << " ask " << ask << " t " << std::fixed << timestamp << std::endl;
        };

        std::cout << "start" << std::endl;

        hitbit.start();

        std::cout << "wait" << std::endl;

        for(int i = 0; i < 30; ++i) {
            const uint64_t DELAY = 1000;
            std::this_thread::sleep_for(std::chrono::milliseconds(DELAY));
        }
        std::cout << "stop" << std::endl;
    }
    std::cout << "exit" << std::endl;
    return 0;
}
