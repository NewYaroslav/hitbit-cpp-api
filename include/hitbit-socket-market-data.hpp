/*
* hitbit-cpp-api - C ++ API client for hitbit
*
* Copyright (c) 2021 Elektro Yar. Email: git.electroyar@gmail.com
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#pragma once
#ifndef HITBIT_SOCKET_MARKET_DATA_HPP_INCLUDED
#define HITBIT_SOCKET_MARKET_DATA_HPP_INCLUDED

#include "hitbit-common.hpp"
#include <client_wss.hpp>
#include <openssl/ssl.h>
#include <nlohmann/json.hpp>
#include <xtime.hpp>
#include <mutex>
#include <atomic>
#include <future>
#include <iostream>

namespace hitbit_cpp_api {

    /** \brief Класс для получения данных рынка через сокет
     */
    class SocketMarketData {
    private:
        using WssClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;
        using json = nlohmann::json;

        /* параметры для подключения */
        std::string point;
        std::string sert_file;

        /* сохранение соедиения */
        std::shared_ptr<WssClient::Connection> save_connection;
        std::mutex save_connection_mutex;

        std::shared_ptr<WssClient> client;

        /* поток для вебсокета */
        std::future<void> client_future;

        std::map<std::string, double> symbol_to_tick_size;
        std::mutex symbol_to_tick_size_mutex;

        std::atomic<int> req_id = ATOMIC_VAR_INIT(1);
        std::atomic<int> req_id_get_symbols = ATOMIC_VAR_INIT(0);

        std::atomic<double> last_server_timestamp = ATOMIC_VAR_INIT(0);

        std::atomic<bool> is_shutdown = ATOMIC_VAR_INIT(false);
        std::atomic<bool> is_open = ATOMIC_VAR_INIT(false);

        /** \brief Отправить сообщение
         */
        void send(const std::string &message) {
            if (!is_open) return;
            std::lock_guard<std::mutex> lock(save_connection_mutex);
            if(!save_connection) return;
            save_connection->send(message);
        }

        /** \brief Парсер сообщения от вебсокета
         * \param response Ответ от сервера
         */
        void parser(const std::string &response) {
            try {
                json j = json::parse(response);
                const auto it_id = j.find("id");
                const auto it_method = j.find("method");
                if (it_id != j.end()) {
                    const int id = *it_id;
                    if(id == req_id_get_symbols) {
                        json j_array = j["result"];
                        const size_t j_array_size = j_array.size();
                        for(size_t i = 0; i < j_array_size; ++i) {
                            const std::string symbol = j_array[i]["id"];
                            const double tick_size = std::stod((const std::string)j_array[i]["tickSize"]);
                            std::lock_guard<std::mutex> lock(symbol_to_tick_size_mutex);
                            symbol_to_tick_size[symbol] = tick_size;
                        }
                    }
                } else {
                    if (it_method != j.end()) {
                        const std::string method = *it_method;
                        if (method == "ticker") {
                            auto it_param = j.find("params");
                            const std::string symbol = (*it_param)["symbol"];
                            const std::string str_timestamp = (*it_param)["timestamp"];
                            const double bid = std::stod((const std::string)(*it_param)["bid"]);
                            const double ask = std::stod((const std::string)(*it_param)["ask"]);
                            const double timestamp = xtime::convert_iso((const std::string)(*it_param)["timestamp"]);
                            double tick_size = 0;

                            {
                                std::lock_guard<std::mutex> lock(symbol_to_tick_size_mutex);
                                auto it_tick_size = symbol_to_tick_size.find(symbol);
                                if (it_tick_size != symbol_to_tick_size.end()) tick_size = it_tick_size->second;
                                else return;
                            }

                            /* проверяем, не поменялась ли метка времени */
                            if(last_server_timestamp < timestamp) {

                                /* если метка времени поменялась, найдем смещение времени сервера */
                                const xtime::ftimestamp_t offset_timestamp = timestamp - xtime::get_ftimestamp();

                                /* запоминаем последнюю метку времени сервера */
                                last_server_timestamp = timestamp;
                            }
                            if(on_ticker != nullptr) on_ticker(symbol, bid, ask, tick_size, timestamp);
                        }
                    }
                }
            }
            catch(const json::parse_error& e) {
            }
            catch(json::out_of_range& e) {
            }
            catch(json::type_error& e) {
            }
            catch(...) {
            }
        }

    public:

        std::function<void(
                const std::string &symbol,
                const double bid,
                const double ask,
                const double tick_size,
                const double timestamp)> on_ticker = nullptr;

        std::function<void()> on_start = nullptr;

        SocketMarketData(
                const std::string user_point = "api.hitbtc.com/api/2/ws/public",
                const std::string user_sert_file = "curl-ca-bundle.crt") :
                point(user_point), sert_file(user_sert_file) {

        };

        ~SocketMarketData() {
            is_shutdown = true;
            std::shared_ptr<WssClient> client_ptr = std::atomic_load(&client);
            if(client_ptr) client_ptr->stop();
            if(client_future.valid()) {
                try {
                    client_future.wait();
                    client_future.get();
                }
                catch(const std::exception &e) {
                    std::cerr << "hitbit_cpp_api::~SocketMarketData() error, what: " << e.what() << std::endl;
                }
                catch(...) {
                    std::cerr << "hitbit_cpp_api::~SocketMarketData() error" << std::endl;
                }
            }
        };

        /** \brief Добавить поток символа с заданным периодом
         * \param symbol Имя символа
         */
        void subscribe_ticker(const std::string &symbol) {
            if(!is_open) return;
            try {
                json j;
                json j_params;
                j["method"] = "subscribeTicker";
                j_params["symbol"] = common::to_upper_case(symbol);
                j["params"] = j_params;
                j["id"] = (int)req_id++;
                send(j.dump());
            }
            catch(...) {};
        }

        void start() {
            if(client_future.valid()) return;

            /* запустим соединение в отдельном потоке */
            client_future = std::async(std::launch::async,[&]() {
                while(!is_shutdown) {
                    try {
                        /* создадим соединение */;
                        client = std::make_shared<WssClient>(
                                point,
                                true,
                                std::string(),
                                std::string(),
                                std::string(sert_file));

                        /* читаем собщения, которые пришли */
                        client->on_message =
                                [&](std::shared_ptr<WssClient::Connection> connection,
                                std::shared_ptr<WssClient::InMessage> message) {
                            parser(message->string());
                        };

                        client->on_open =
                            [&](std::shared_ptr<WssClient::Connection> connection) {
                            {
                                std::lock_guard<std::mutex> lock(save_connection_mutex);
                                save_connection = connection;
                            }
                            is_open = true;

                            /* */
                            try {
                                json j;
                                j["method"] = "getSymbols";
                                j["params"] = json::object();
                                req_id_get_symbols = (int)req_id++;
                                j["id"] = (int)req_id_get_symbols;
                                send(j.dump());
                            }
                            catch(...) {};
                            /* */

                            if(on_start != nullptr) on_start();
                        };

                        client->on_close =
                                [&](std::shared_ptr<WssClient::Connection> /*connection*/,
                                int status, const std::string & /*reason*/) {
                            //is_websocket_init = false;
                            is_open = false;
                            //is_error = true;
                            {
                                std::lock_guard<std::mutex> lock(save_connection_mutex);
                                if(save_connection) save_connection.reset();
                            }
                            std::cerr
                                << point
                                << " closed connection with status code " << status
                                << std::endl;
                        };

                        // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
                        client->on_error =
                                [&](std::shared_ptr<WssClient::Connection> /*connection*/,
                                const SimpleWeb::error_code &ec) {
                            is_open = false;
                            {
                                std::lock_guard<std::mutex> lock(save_connection_mutex);
                                if(save_connection) save_connection.reset();
                            }
                            std::cerr
                                << point
                                << " wss error: " << ec
                                << std::endl;
                        };
                        client->start();
                        client.reset();
                    }
                    catch (std::exception& e) {
                        //is_websocket_init = false;
                        //is_error = true;
                    }
                    catch (...) {
                        //is_websocket_init = false;
                        //is_error = true;
                    }
                    if(is_shutdown) break;

                    /* обнуляем данные */
                    req_id_get_symbols = 0;

					const uint64_t RECONNECT_DELAY = 1000;
					std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY));
                } // while
            });
        }
    };
};

#endif // HITBIT_SOCKET_MARKET_DATA_HPP_INCLUDED
