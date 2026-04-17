#pragma once

#include <string>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

namespace transport {

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    using MessageHandler = std::function<std::string(const std::string&)>;

    explicit WebSocketSession(tcp::socket&& socket, MessageHandler handler)
        : ws_(std::move(socket)), handler_(handler) {}

    void run() {
        beast::net::dispatch(
            ws_.get_executor(),
            beast::bind_front_handler(&WebSocketSession::on_run, shared_from_this())
        );
    }

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    MessageHandler handler_;

    void on_run() {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " vlsi-daemon");
            }));
            
        ws_.async_accept(
            beast::bind_front_handler(&WebSocketSession::on_accept, shared_from_this())
        );
    }

    void on_accept(beast::error_code ec) {
        if(ec) return;
        do_read();
    }

    void do_read() {
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(&WebSocketSession::on_read, shared_from_this())
        );
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if(ec == websocket::error::closed) return;
        if(ec) return;

        // Extract message
        std::string incoming = beast::buffers_to_string(buffer_.data());
        std::cout << "[WebSocket] Received: " << incoming << std::endl;
        buffer_.consume(buffer_.size());

        // Process message
        std::string response = handler_(incoming);

        // Send response
        ws_.text(ws_.got_text());
        ws_.async_write(
            net::buffer(response),
            beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this())
        );
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if(ec) return;
        do_read(); // Wait for next message
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint, WebSocketSession::MessageHandler handler)
        : ioc_(ioc), acceptor_(ioc), handler_(handler) {
        
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
    }

    void run() { do_accept(); }

private:
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    WebSocketSession::MessageHandler handler_;

    void do_accept() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(&Listener::on_accept, shared_from_this())
        );
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if(!ec) {
            std::make_shared<WebSocketSession>(std::move(socket), handler_)->run();
        }
        do_accept();
    }
};

class WebSocketServer {
public:
    using MessageHandler = std::function<std::string(const std::string&)>;

    WebSocketServer(const std::string& address, int port) 
        : address_(address), port_(port), ioc_(1) {}

    void set_message_handler(MessageHandler handler) {
        handler_ = handler;
    }

    void run() {
        // Parse IP
        auto const address = net::ip::make_address(address_);
        
        std::cout << "[WebSocketServer] Listening on " << address_ << ":" << port_ << " with Boost::Beast Async IO.\n";
        
        std::make_shared<Listener>(
            ioc_,
            tcp::endpoint{address, static_cast<unsigned short>(port_)},
            handler_
        )->run();

        // Run the I/O service
        ioc_.run();
    }

private:
    std::string address_;
    int port_;
    net::io_context ioc_;
    MessageHandler handler_;
};

} // namespace transport
