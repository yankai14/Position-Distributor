#include <iostream>
#include <position.pb.h>

#include "peer.h"
#include "Engine.h"

void print_trade(const Trade& trade) {
    std::string trade_str;
    google::protobuf::TextFormat::PrintToString(trade, &trade_str);
    log("Parsed Trade:\n" + trade_str);
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " <strategy name> <listen_port> [connect_host:port...]\n";
            return 1;
        }

        std::string strategy_name(argv[1]);
        asio::io_context io_context;
        std::shared_ptr<Peer> peer = std::make_shared<Peer>(io_context, std::stoi(argv[2]));
        Engine engine(peer, std::move(strategy_name));

        // Connect to other peers
        for (int i = 3; i < argc; ++i) {
            std::string host_port = argv[i];
            size_t colon = host_port.find(':');
            peer->connect_to_peer(
                    host_port.substr(0, colon),
                    std::stoi(host_port.substr(colon + 1))
            );
        }

        // Start IO context in background
        std::atomic<bool> running{true};
        std::thread io_thread([&] {
            while (running) {
                try {
                    io_context.run();
                    running = false;
                } catch(...) {}
            }
        });

        // Command interface
        std::string message;
        while (std::getline(std::cin, message)) {
            if (message == "exit") break;

            try {
                Trade trade = parse_trade(message);
                print_trade(trade);
                engine.push_trade(trade);
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
                std::cerr << "Valid example: AAPL 100\n\n";
            }
        }

        running = false;
        io_context.stop();
        io_thread.join();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}