//
// Created by Yan Kai Lim on 13/3/25.
//

#ifndef MYSERVER_PEER_H
#define MYSERVER_PEER_H

#include <boost/asio.hpp>
#include <memory>
#include <unordered_map>
#include <mutex>

#include "utils.h"
#include "EventDispatcher.h"

using boost::asio::ip::tcp;
namespace asio = boost::asio;


class Peer {
public:
    Event received_message;
    Event connection_accepted;
public:
    Peer(asio::io_context& io_context, unsigned short port)
            : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
              io_context_(io_context),
              strand_(asio::make_strand(io_context)){
        log("[Peer::Peer] Server starting on port " + std::to_string(port));
        start_accept();
    }

    void connect_to_peer(const std::string& host, unsigned short port, int max_retries = 3, int retry_delay_ms = 5000) {
        auto socket = std::make_shared<tcp::socket>(io_context_);
        tcp::resolver resolver(io_context_);

        log("[Peer::connect_to_peer] Attempting connection to " + host + ":" + std::to_string(port) + " (retries left: " + std::to_string(max_retries) + ")");

        boost::system::error_code resolve_ec;
        auto endpoints = resolver.resolve(host, std::to_string(port), resolve_ec);
        if (resolve_ec) {
            log("[Peer::connect_to_peer] Resolve failed for " + host + ":" + std::to_string(port) + ": " + resolve_ec.message(), true);
            if (max_retries > 0) {
                log("[Peer::connect_to_peer] Scheduling retry...");
                schedule_retry(host, port, max_retries - 1, retry_delay_ms);
            } else {
                log("[Peer::connect_to_peer] No more retries left for " + host + ":" + std::to_string(port), true);
            }
            return;
        }

        async_connect(*socket, endpoints,
                      [this, socket, host, port, max_retries, retry_delay_ms](const boost::system::error_code& ec, const tcp::endpoint& endpoint) {
                          if (!ec) {
                              log("[Peer::connect_to_peer] Connected to " + get_host_port_str(socket->remote_endpoint()));
                              connection_accepted();
                              std::lock_guard<std::mutex> lock(connections_mutex_);
                              connections_.emplace(socket, socket);
                              start_read(socket);
                          } else {
                              log("[Peer::connect_to_peer] Connection to " + host + ":" + std::to_string(port) +
                                  " failed: " + ec.message(), true);
                              if (max_retries > 0) {
                                  log("[Peer::connect_to_peer] Scheduling retry (" + std::to_string(max_retries - 1) + " retries left)...");
                                  schedule_retry(host, port, max_retries - 1, retry_delay_ms);
                              } else {
                                  log("[Peer::connect_to_peer] No more retries left for " + host + ":" + std::to_string(port), true);
                              }
                          }
                      });
    }

    void broadcast(const std::string& message) {
        log("[Peer::broadcast] Broadcasting to all connections");
        for (auto& [_, socket] : connections_) {
            send_message(socket, message);
        }
    }

    void send_message(const std::shared_ptr<tcp::socket>& socket, const std::string& message) {
        uint32_t net_length = htonl(static_cast<uint32_t>(message.size()));
        auto write_buffer = std::make_shared<std::vector<char>>(sizeof(uint32_t) + message.size());

        std::memcpy(write_buffer->data(), &net_length, sizeof(uint32_t));
        std::memcpy(write_buffer->data() + sizeof(uint32_t), message.data(), message.size());

        log("[Peer::send_message] Sending message to " + get_host_port_str(socket->remote_endpoint()));

        asio::post(strand_, [socket, write_buffer]() {
            asio::async_write(*socket, asio::buffer(*write_buffer),
                              [socket, write_buffer](boost::system::error_code ec, std::size_t /*bytes_transferred*/) {
                                  if (ec) {
                                      std::string err = "[Peer::send_message] Write error: " + ec.message();
                                      log(err, true);
                                  }
                              });
        });
    }

    static std::string get_host_port_str(const tcp::endpoint&& remote_ep) {
        return remote_ep.address().to_string() + ":" + std::to_string(remote_ep.port());
    }
private:
    tcp::acceptor acceptor_;
    asio::io_context& io_context_;
    asio::streambuf buffer_;
    asio::strand<asio::io_context::executor_type> strand_;

    std::unordered_map<std::shared_ptr<tcp::socket>, std::shared_ptr<tcp::socket>> connections_;
    std::mutex connections_mutex_;

private:
    void schedule_retry(const std::string& host, unsigned short port, int remaining_retries, int retry_delay_ms) {
        auto timer = std::make_shared<asio::steady_timer>(io_context_);
        timer->expires_after(std::chrono::milliseconds(retry_delay_ms));
        timer->async_wait([this, host, port, remaining_retries, retry_delay_ms, timer](const boost::system::error_code& ec) {
            if (!ec) {
                connect_to_peer(host, port, remaining_retries, retry_delay_ms);
            } else if (ec != asio::error::operation_aborted) {
                log("[Peer::schedule_retry] Retry timer error: " + ec.message(), true);
            }
        });
    }

    void start_accept() {
        auto socket = std::make_shared<tcp::socket>(io_context_);
        acceptor_.async_accept(*socket,
                               [this, socket](boost::system::error_code ec) {
                                   if (!ec) {
                                       log("[Peer::start_accept] Accepted connection from " + get_host_port_str(socket->remote_endpoint()));
                                       {
                                           std::lock_guard<std::mutex> lock(connections_mutex_);
                                           connections_.emplace(socket, socket);
                                       }
                                       connection_accepted(socket);
                                       start_read(socket);
                                       start_accept();
                                   } else {
                                       log("[Peer::start_accept] Accept error: " + ec.message(), true);
                                   }
                               });
    }

    void start_read(const std::shared_ptr<tcp::socket>& socket) {
        // Allocate local buffer for reading the length.
        auto size_buffer = std::make_shared<uint32_t>(0);

        asio::async_read(*socket, asio::buffer(size_buffer.get(), sizeof(uint32_t)),
                         [this, socket, size_buffer](boost::system::error_code ec, std::size_t) {
                             if (!ec) {
                                 uint32_t msg_size = ntohl(*size_buffer);
                                 // Allocate a buffer for the incoming message.
                                 auto message_buffer = std::make_shared<std::string>();
                                 message_buffer->resize(msg_size);

                                 asio::async_read(*socket, asio::buffer(&(*message_buffer)[0], msg_size),
                                                  [this, socket, message_buffer](boost::system::error_code ec, std::size_t) {
                                                      log("[Peer::start_read] Received a message from " + get_host_port_str(socket->remote_endpoint()));
                                                      if (!ec) {
                                                          received_message(*message_buffer);
                                                          // Start the next read operation using a new local buffer.
                                                          start_read(socket);
                                                      } else {
                                                          log("[Peer::start_read] Connection closed by " + get_host_port_str(socket->remote_endpoint()) + ": " + ec.message());
                                                          std::lock_guard<std::mutex> lock(connections_mutex_);
                                                          connections_.erase(socket);
                                                      }
                                                  });
                             } else {
                                 log("[Peer::start_accept] Connection closed by " + get_host_port_str(socket->remote_endpoint()) + ": " + ec.message());
                                 std::lock_guard<std::mutex> lock(connections_mutex_);
                                 connections_.erase(socket);
                             }
                         });
    }

};


#endif //MYSERVER_PEER_H
