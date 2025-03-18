# Position Distributor

In most trading systems, we have multiple clients that trades on many different exchange. For this position distributor, we assume a client (strategy) has a 1-1 mapping with the exchange. From a risk management standpoint, it is essential to know the overall exposure of the firm. The position distributor aims to provide a global view of each strategy's positions.

# Requirements

1. Correctness

At any point in time, all strategies (clients) should have a consistent view of positions from different strategies.

2. Order preservation

Ensure that the position information is processed in the order sent by the original strategy.

3. Resilience

This system aims to account for 2 scenarios: Dropped connections and process crashes.

# High level overview

The position distributor is implemented as a simple p2p distributed system. Peers communicate over the network via a persistent TCP connection. In this case, each peer is connected to only 1 exchange (See above assumption).

Each peer contains a global view of the positions that is eventually consistent. This is stored in memory using `std::unordered_map<std::string, std::unordered_map<std::string, std::pair<double, int64_t>>>`. This contains the following: <strategy name>:<symbol>:<last updated timestamp, position>.

Whenever a peer receives a `Trade` message from the broker, it updates its own symbol positions and then broadcast the `SymbolPos` message (containing symbol position updates) over to all connected peers. For example if a trade message for strategy_2 comes in, the resulting position will be as follows:
`strategy_3 | AAPL | 100.000000 | 1742220817595451000`.

The columns are as follows:
`<strategy name>,<symbol>,<last updated timestamp>,<position>`

Below shows the proto structures:
```
message Trade {
  string symbol = 1;
  double position = 2;
}

message SymbolPos {
  string strategy_name = 1;
  string symbol = 2;
  double net_position = 3;
  int64 timestamp = 4;
}

```

When a peer receives the `SymbolPos` message, it will process the position updates based on the timestamp. Suppose if strategy_2 have `strategy_3 | AAPL | 100.000000 | 1742220817595451000` and I receive the following SymbolPos message from strategy_3

```
struct SymbolPos {
  strategy_name = "strategy_3"
  symbol = "AAPL"
  net_position = 50
  timestamp = 1742220817595460000
}
```
strategy_3 will update its position to `strategy_3 | AAPL | 50.000000 | 1742220817595460000` since the message has a timestamp that is later than what strategy_3 has. This ensures the ordering of the trades and positions should be reflected correctly.

Suppose a peer (eg strategy X) crashes or a new peer joins the p2p network, it will still be able to receive the latest position. Whenever each existing peer accepts and establish a connection with strategy X, it will send all its own positions (not just symbol positions). Each strategy has ownership of only its own strategy positions. The positions will be send 1 symbol position at a time to ensure that the buffer on the receiver do not overflow (leading to dropped messages). As for position updates, it is handled the same way as the above. Since there is no guarantee which position updates come first (from a trade event or from the accept event), the timestamp ensures that the position will always remain updated. <TODO example>


# Design
Position Distributor contains mainly `Engine`, `Event Dispatcher` and `Peer`.

## Engine.h
The `Engine` class contains the core logic. It ensures that all position and trades messages gets updated while also building the messages which will be send to the other strategies.

This is designed using the single producer consumer pattern where the incoming `Trades` and `SymbolPos` messages will be pushed into a `boost::lockfree::queue` and consumed via its respective consumer threads.

## Peer.h
Contains the core logic of socket handling. This is realised via `boost::asio`, leveraging on its async io capabilities. Its main member functions are:

1. connect_to_peer: Attempts to connect to peer with retry capabilities.
2. start_accept: Accepts all connections from peers (for now).
3. send_message: Send messages to specified connection
4. start_read: Reads messages from connections
5. broadcast: Broadcast messages to all connected peers

## EventDispatcher.h
A simple utility class that allows subscribing `EventHandler` to `Event` invoking them whenever an event is deemed to have happened.
Usages:
- In Engine.h, the constructor subscribes event handlers to events owned by the `Peer` instance.
- In Peer.h, the start_accept and start_read member functions invokes the `Event` which in turn call the registered event handlers.

# User Interface
We currently mock the exchange incoming trades via user input and it is always of the format
`<symbol> <qty>`. For example typing `AAPL 100` on strategy_1 will show the following:
```
AAPL 100
2025-03-18 22:35:49 Parsed Trade:
symbol: "AAPL"
position: 100

2025-03-18 22:35:49 [Engine::process_trade] Processing trade on symbol AAPL
2025-03-18 22:35:49 Current positions 
strategy_1 | AAPL | 100.000000 | 1742308549773254000
2025-03-18 22:35:49 [Peer::broadcast] Broadcasting to all connections
```
and strategy_2, connected to strategy_1 to show the following:
```
2025-03-18 22:36:49 [Peer::connect_to_peer] Attempting connection to 127.0.0.1:12345 (retries left: 3)
2025-03-18 22:36:49 [Peer::connect_to_peer] Connected to 127.0.0.1:12345
2025-03-18 22:36:49 [Peer::start_read] Receive a message from 
2025-03-18 22:36:49 [Engine::incoming_message_handler] Received SymPos message:
strategy_name: "strategy_1"
symbol: "AAPL"
net_position: 100
timestamp: 1742308549773254000

2025-03-18 22:36:49 [Engine::process_positions] Processing position AAPL from strategy_1
2025-03-18 22:36:49 Current positions 
strategy_1 | AAPL | 100.000000 | 1742308549773254000
```

# How to build and run

Prerequisites:
1. C++20
2. CMAKE 

## Building
After cloning the repository, cd into the repository folder and do the following
```
mkdir -p build
cmake ..
```
To compile protobuf cpp files, in {cwd}/build
```
make protoc
```
To build binary, in {cwd}/build
```
make server
```

Note that the CMakeLists.txt file in server contains commented out sanitizers.

## To run the code
Ensure that the binary has been build. In {cwd}/build
```
>> ./server 
Usage: ./server <strategy name> <listen_port> [connect_host:port...]
```
For example if I want to start the first peer
```
>> ./server strategy_1 12345
2025-03-19 00:06:55 [Peer::Peer] Server starting on port 12345
2025-03-19 00:06:56 [Engine::Engine] Registering handlers to Peer Events for strategy_1
2025-03-19 00:06:56 [Engine::consume_trades] Consuming trades....
2025-03-19 00:06:56 [Engine::consume_positions] Consuming trades....
```
I can 'add' trades by adding <symbol> <qty> on the console
```
AAPL 100                            <------- <symbol> <qty> user input
2025-03-19 00:07:39 Parsed Trade:   <------- stdout
symbol: "AAPL"
position: 100

2025-03-19 00:07:39 [Engine::process_trade] Processing trade on symbol AAPL
2025-03-19 00:07:39 Current positions 
strategy_1 | AAPL | 100.000000 | 1742314059563634000
strategy_1 | BTC | 500.000000 | 1742314214454189000

2025-03-19 00:07:39 [Peer::broadcast] Broadcasting to all connections
```
We can connect to existing peers by starting a new process on a new terminal, 
this time specifying the existing peers
```
>> ./server strategy_2 12346  127.0.0.1:12345

2025-03-19 00:10:36 [Peer::Peer] Server starting on port 12346
2025-03-19 00:10:36 [Engine::Engine] Registering handlers to Peer Events for strategy_2
2025-03-19 00:10:36 [Peer::connect_to_peer] Attempting connection to 127.0.0.1:12345 (retries left: 3)
2025-03-19 00:10:36 [Engine::consume_trades] Consuming trades....
2025-03-19 00:10:36 [Engine::consume_positions] Consuming trades....
2025-03-19 00:10:36 [Peer::connect_to_peer] Connected to 127.0.0.1:12345
2025-03-19 00:10:36 [Peer::start_read] Received a message from 127.0.0.1:12345
2025-03-19 00:10:36 [Engine::incoming_message_handler] Received SymPos message:
strategy_name: "strategy_1"
symbol: "BTC"
net_position: 500
timestamp: 1742314214454189000

2025-03-19 00:10:36 [Engine::process_positions] Processing position BTC from strategy_1
2025-03-19 00:10:36 Current positions 
strategy_1 | BTC | 500.000000 | 1742314214454189000

2025-03-19 00:10:36 [Peer::start_read] Received a message from 127.0.0.1:12345
2025-03-19 00:10:36 [Engine::incoming_message_handler] Received SymPos message:
strategy_name: "strategy_1"
symbol: "AAPL"
net_position: 300
timestamp: 1742314208002396000

2025-03-19 00:10:36 [Engine::process_positions] Processing position AAPL from strategy_1
2025-03-19 00:10:36 Current positions 
strategy_1 | AAPL | 300.000000 | 1742314208002396000
strategy_1 | BTC | 500.000000 | 1742314214454189000
```
Similarly, strategy_2 can mock trades from exchange by doing the following:
```
AAPL 100
2025-03-19 00:12:54 Parsed Trade:
symbol: "AAPL"
position: 100

2025-03-19 00:12:54 [Engine::process_trade] Processing trade on symbol AAPL
2025-03-19 00:12:54 Current positions 
strategy_2 | AAPL | 100.000000 | 1742314374795829000
strategy_1 | AAPL | 300.000000 | 1742314208002396000
strategy_1 | BTC | 500.000000 | 1742314214454189000
```
And we can add another strategy to the network
```
>>  ./server strategy_3 12347 127.0.0.1:12345 127.0.0.1:12346
2025-03-19 00:13:31 [Peer::Peer] Server starting on port 12347
2025-03-19 00:13:31 [Engine::Engine] Registering handlers to Peer Events for strategy_3
2025-03-19 00:13:31 [Engine::consume_trades] Consuming trades....
2025-03-19 00:13:31 [Peer::connect_to_peer] Attempting connection to 127.0.0.1:12345 (retries left: 3)
2025-03-19 00:13:31 [Engine::consume_positions] Consuming trades....
2025-03-19 00:13:31 [Peer::connect_to_peer] Attempting connection to 127.0.0.1:12346 (retries left: 3)
2025-03-19 00:13:31 [Peer::connect_to_peer] Connected to 127.0.0.1:12345
2025-03-19 00:13:31 [Peer::connect_to_peer] Connected to 127.0.0.1:12346
2025-03-19 00:13:31 [Peer::start_read] Received a message from 127.0.0.1:12345
2025-03-19 00:13:31 [Engine::incoming_message_handler] Received SymPos message:
strategy_name: "strategy_1"
symbol: "BTC"
net_position: 500
timestamp: 1742314214454189000

2025-03-19 00:13:31 [Engine::process_positions] Processing position BTC from strategy_1
2025-03-19 00:13:31 Current positions 
strategy_1 | BTC | 500.000000 | 1742314214454189000

2025-03-19 00:13:31 [Peer::start_read] Received a message from 127.0.0.1:12345
2025-03-19 00:13:31 [Engine::incoming_message_handler] Received SymPos message:
strategy_name: "strategy_1"
symbol: "AAPL"
net_position: 300
timestamp: 1742314208002396000

2025-03-19 00:13:31 [Engine::process_positions] Processing position AAPL from strategy_1
2025-03-19 00:13:31 Current positions 
strategy_1 | AAPL | 300.000000 | 1742314208002396000
strategy_1 | BTC | 500.000000 | 1742314214454189000

2025-03-19 00:13:31 [Peer::start_read] Received a message from 127.0.0.1:12346
2025-03-19 00:13:31 [Engine::incoming_message_handler] Received SymPos message:
strategy_name: "strategy_2"
symbol: "AAPL"
net_position: 100
timestamp: 1742314374795829000

2025-03-19 00:13:31 [Engine::process_positions] Processing position AAPL from strategy_2
2025-03-19 00:13:31 Current positions 
strategy_2 | AAPL | 100.000000 | 1742314374795829000
strategy_1 | AAPL | 300.000000 | 1742314208002396000
strategy_1 | BTC | 500.000000 | 1742314214454189000
```
# Things to improve (Due to time constraints)
1. Currently broadcasting might cause network congestions. Since each strategy is assumed to be mapped to only 1 exchange, it is fine for now.
2. We might want to have a persistent storage of positions and do a periodic write through to the DB. This can be done via a separate listener process which sends a request message to each strategy which retrieves the strategy positions for each strategy and push to a database such as KDB to keep a snapshot.
3. EOD jobs that takes a snapshot of positions to keep historical positions.
4. Have some sort of centralised listener that keeps track of existing peers within the p2p network and send this to peers that requests for it
5. Use memory pools for Peer.h reads. Currently using heap allocated std::string as an easy replacement
6. Logging can be replaced with spdlog etc. Currently using std::cout and std::endl which causes contention for the file descriptor stdout when multiple threads tries to use it (therefore the lock)
7. P2P Gossip algorithm to sync positions to improve reliability. https://highscalability.com/gossip-protocol-explained/
