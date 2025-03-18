#ifndef MYSERVER_ENGINE_H
#define MYSERVER_ENGINE_H

#include <chrono>
#include <boost/lockfree/queue.hpp>
#include <google/protobuf/arena.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/timestamp.pb.h>
#include <memory>
#include <utility>
#include <position.pb.h>
#include <thread>
#include <atomic>
#include <unordered_set>

#include "peer.h"
#include "utils.h"


class Engine {
public:
    explicit Engine(const std::shared_ptr<Peer>& peer, std::string&& strategy_name):
            peer_(peer),
            consume_trade_worker_([this] {consume_trades();}),
            consume_position_worker_([this] {consume_positions();}),
            running_(true),
            trades_queue_(65536),
            positions_queue_(65536),
            strategy_name_(std::move(strategy_name))
    {
        log("[Engine::Engine] Registering handlers to Peer Events for " + strategy_name_);
        peer_->received_message += std::function<void(std::string)>(
                [this](const std::string& msg) {
                    incoming_message_handler(msg);
                }
        );

        peer_->connection_accepted += std::function<void(std::shared_ptr<tcp::socket>)>(
                [this](const std::shared_ptr<tcp::socket>& socket) {
                    std::thread([this] (std::shared_ptr<tcp::socket> socket) {
                        push_current_positions(socket);
                    }, socket).detach();
                }
        );
    }

    ~Engine() {
        log("[Engine::Engine] Destroying " + strategy_name_);
        running_.store(false, std::memory_order_release);
        consume_trade_worker_.join();
        consume_position_worker_.join();
    }

    void see_positions() {
        std::string position_msg = "Current positions \n";
        for (auto& [strategy, strategy_positions]: positions_) {
            for (auto& [symbol, position]: strategy_positions) {
                auto [sizing, last_updated] = position;
                position_msg += strategy + " | ";
                position_msg +=  symbol + " | ";
                position_msg += std::to_string(sizing) + " | ";
                position_msg += std::to_string(last_updated) + "\n";
            }
        }
        log(position_msg);
    }

    void push_trade(const Trade& trade) {
        auto* arena_trade = google::protobuf::Arena::CreateMessage<Trade>(&arena_);
        arena_trade->CopyFrom(trade);

        while (!trades_queue_.push(arena_trade)) {
            std::this_thread::yield();
        }
    }

    void push_position(const SymbolPos& pos) {
        auto* arena_position = google::protobuf::Arena::CreateMessage<SymbolPos>(&arena_);
        arena_position->CopyFrom(pos);

        while (!positions_queue_.push(arena_position)) {
            std::this_thread::yield();
        }
    }

private:
    boost::lockfree::queue<Trade*> trades_queue_;
    boost::lockfree::queue<SymbolPos*> positions_queue_;
    google::protobuf::Arena arena_;
    std::atomic<bool> running_;
    std::thread consume_trade_worker_;
    std::thread consume_position_worker_;
    std::shared_ptr<Peer> peer_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::pair<double, int64_t>>> positions_;
    std::string strategy_name_;
private:
    void incoming_message_handler(const std::string& msg) {
        std::string message_str;
        Trade trade;
        SymbolPos pos;
        if (pos.ParseFromString(msg)) {
            google::protobuf::TextFormat::PrintToString(pos, &message_str);
            log("[Engine::incoming_message_handler] Received SymPos message:\n" + message_str);
            push_position(pos);
        } else if (trade.ParseFromString(msg)) {
            google::protobuf::TextFormat::PrintToString(trade, &message_str);
            log("[Engine::incoming_message_handler] Received Trade message:\n" + message_str);
            push_trade(trade);
        } else {
            log("[Engine::incoming_message_handler] Could not parse protobuf message, dropping", true);
        }
    }

    void push_current_positions(std::shared_ptr<tcp::socket>& socket) {
        log("[Engine::push_current_positions] Sending " + strategy_name_ + \
            " positions to " + Peer::get_host_port_str(socket->remote_endpoint()));
        for (auto& [symbol, position] : positions_[strategy_name_]) {
            auto [sizing, last_updated] = position;
            SymbolPos pos;
            pos.set_strategy_name(strategy_name_);
            pos.set_symbol(symbol);
            pos.set_net_position(sizing);
            pos.set_timestamp(last_updated);

            std::string position_msg;
            if (!pos.SerializeToString(&position_msg)) {
                log("[Engine::push_current_positions] Failed to serialize protobuf message. Skipping sending position...", true);
                continue;
            }
            peer_->send_message(socket, position_msg);
        }
    }

    void process_positions(SymbolPos& pos) {
        log("[Engine::process_positions] Processing position " + pos.symbol() + " from " + pos.strategy_name());
        if (positions_.find(pos.strategy_name()) == positions_.end() || \
            positions_[pos.strategy_name()].find(pos.symbol()) == positions_[pos.strategy_name()].end())
        {
            positions_[pos.strategy_name()][pos.symbol()].first = pos.net_position();
            positions_[pos.strategy_name()][pos.symbol()].second = pos.timestamp();
            see_positions();
            return;
        }

        auto& [current_pos, timestamp] = positions_[pos.strategy_name()][pos.symbol()];
        if (pos.timestamp() > timestamp) {
            current_pos = pos.net_position();
            timestamp = pos.timestamp();
            positions_[pos.strategy_name()][pos.symbol()].first = pos.net_position();
            positions_[pos.strategy_name()][pos.symbol()].second = pos.timestamp();
        }
        see_positions();
    }

    void process_trade(Trade& trade) {
        log("[Engine::process_trade] Processing trade on symbol " + trade.symbol());
        auto now = std::chrono::system_clock::now();
        auto ns_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
        positions_[strategy_name_][trade.symbol()].first += trade.position();
        positions_[strategy_name_][trade.symbol()].second = ns_since_epoch.count();
        see_positions();

        SymbolPos pos;
        pos.set_symbol(trade.symbol());
        pos.set_net_position(positions_[strategy_name_][trade.symbol()].first);
        pos.set_strategy_name(strategy_name_);
        pos.set_timestamp(ns_since_epoch.count());

        std::string pos_msg;
        if (!pos.SerializeToString(&pos_msg)) {
            log("[Engine::process_trade] Failed to serialize gossip position for " + trade.symbol(), true);
            return;
        }

        peer_->broadcast(pos_msg);
    }

    void consume_trades() {
        log("[Engine::consume_trades] Consuming trades....");
        while (running_.load(std::memory_order_acquire)) {
            Trade* trade = nullptr;
            if (trades_queue_.pop(trade)) {
                process_trade(*trade);
            }
        }

        log("[Engine::consume_trades] Stopping, processing last few trades in trades_queue....");
        Trade* trade = nullptr;
        while (trades_queue_.pop(trade)) {
            process_trade(*trade);
        }
    }

    void consume_positions() {
        log("[Engine::consume_positions] Consuming trades....");
        while (running_.load(std::memory_order_acquire)) {
            SymbolPos* position = nullptr;
            if (positions_queue_.pop(position)) {
                process_positions(*position);
            }
        }
        log("[Engine::consume_positions] Stopping....");
    }
};

#endif //MYSERVER_ENGINE_H