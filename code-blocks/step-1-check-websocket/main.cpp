#include <iostream>
#include "client_wss.hpp"
#include <openssl/ssl.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>

using namespace std;
using WssClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;
using json = nlohmann::json;

int main() {
    WssClient client("api.hitbtc.com/api/2/ws/public", true, std::string(), std::string(), "curl-ca-bundle.crt");

    client.on_message = [&](shared_ptr<WssClient::Connection> connection, std::shared_ptr<WssClient::InMessage> message) {
        std::string temp = message->string();
        /*
         *  {
         *      "jsonrpc": "2.0",
         *      "method": "ticker",
         *      "params": {
         *          "ask": "0.054464",
         *          "bid": "0.054463",
         *          "last": "0.054463",
         *          "open": "0.057133",
         *          "low": "0.053615",
         *          "high": "0.057559",
         *          "volume": "33068.346",
         *          "volumeQuote": "1832.687530809",
         *          "timestamp": "2017-10-19T15:45:44.941Z",
         *          "symbol": "ETHBTC"
         *      }
         *  }
         */
        std::cout << temp << std::endl;
        json j = json::parse(temp);
        std::cout << "ask: " << j["params"]["ask"] << std::endl;
        std::cout << "bid: " << j["params"]["bid"] << std::endl;
    };

    client.on_open = [](shared_ptr<WssClient::Connection> connection) {
        std::cout << "Client: Opened connection" << std::endl;
        /*
         *   {
         *       "method": "subscribeTicker",
         *       "params": {
         *           "symbol": "ETHBTC"
         *       },
         *       "id": 123
         *   }
         */
        json j;
        json j_params;
        j["method"] = "subscribeTicker";
        j_params["symbol"] = "BTCUSD";
        j["params"] = j_params;
        j["id"] = 1;
        string message = j.dump();
        cout << "Client: Sending message: \"" << message << "\"" << endl;
        connection->send(message);
    };

    client.on_close = [](shared_ptr<WssClient::Connection> /*connection*/, int status, const string & /*reason*/) {
        std::cout << "Client: Closed connection with status code " << status << endl;
    };

    // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
    client.on_error = [](shared_ptr<WssClient::Connection> /*connection*/, const SimpleWeb::error_code &ec) {
        cout << "Client: Error: " << ec << ", error message: " << ec.message() << endl;
    };

    client.start();

    return 0;
}
