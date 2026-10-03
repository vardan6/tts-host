#include "tts_host/local_api_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <thread>

namespace tts_host {
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

constexpr char kSelectionPath[] = "/v1/selection";
constexpr std::uint64_t kMaximumRequestBytes = 1024 * 1024;

bool blank(const std::string &text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isspace(ch); });
}

http::response<http::string_body> make_response(unsigned version, http::status status,
                                                std::string body = {}) {
  http::response<http::string_body> response{status, version};
  response.set(http::field::server, "tts-host");
  response.set(http::field::content_type, "application/json; charset=utf-8");
  response.keep_alive(false);
  response.body() = std::move(body);
  response.prepare_payload();
  return response;
}
}  // namespace

class LocalApiServer::Impl {
 public:
  Impl(std::string host, unsigned short port, std::vector<std::string> allowed_origins,
       SelectionHandler on_selection, PlaybackHandler on_playback)
      : context_(), acceptor_(context_), on_selection_(std::move(on_selection)),
        on_playback_(std::move(on_playback)),
        allowed_origins_(allowed_origins.begin(), allowed_origins.end()) {
    asio::ip::address address;
    if (host == "localhost") {
      address = asio::ip::address_v4::loopback();
    } else {
      boost::system::error_code error;
      address = asio::ip::make_address(host, error);
      if (error) throw std::runtime_error("server.host must be a loopback IP address: " + host);
    }
    if (!address.is_loopback()) {
      throw std::runtime_error(
          "server.host must resolve to loopback while authentication is disabled");
    }
    endpoint_ = tcp::endpoint(address, port);
    boost::system::error_code error;
    acceptor_.open(endpoint_.protocol(), error);
    if (error) throw std::runtime_error("could not open local API socket: " + error.message());
    acceptor_.set_option(tcp::acceptor::reuse_address(true), error);
    if (error) throw std::runtime_error("could not configure local API socket: " + error.message());
    acceptor_.bind(endpoint_, error);
    if (error) {
      throw std::runtime_error("could not bind local API to " + host + ":" +
                               std::to_string(port) + ": " + error.message());
    }
    // Port 0 assigns an ephemeral endpoint for local API tests.
    endpoint_ = acceptor_.local_endpoint(error);
    if (error) {
      throw std::runtime_error("could not query bound local API endpoint: " + error.message());
    }
    acceptor_.listen(asio::socket_base::max_listen_connections, error);
    if (error) throw std::runtime_error("could not listen on local API socket: " + error.message());
    accept_next();
    worker_ = std::thread([this] { context_.run(); });
  }

  ~Impl() {
    context_.stop();
    if (worker_.joinable()) worker_.join();
  }

  unsigned short port() const { return acceptor_.local_endpoint().port(); }

 private:
  bool origin_allowed(const std::string &origin) const {
    return !origin.empty() && allowed_origins_.contains(origin);
  }

  // All sockets are asynchronous on one worker. An open control connection
  // must not block selection requests, other clients, or Host shutdown.
  struct Connection : std::enable_shared_from_this<Connection> {
    Impl &owner;
    beast::tcp_stream stream;
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    http::response<http::string_body> response;
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws;
    std::string outgoing;

    Connection(Impl &server, tcp::socket socket) : owner(server), stream(std::move(socket)) {
      parser.body_limit(kMaximumRequestBytes);
    }

    void read_http() {
      stream.expires_after(std::chrono::seconds(5));
      http::async_read(stream, buffer, parser,
          [self = shared_from_this()](boost::system::error_code error, std::size_t) {
            if (!error) self->handle_http();
          });
    }

    void handle_http() {
      auto request = parser.release();
      const std::string origin(request[http::field::origin]);
      if (request.target() == "/v1/playback" && websocket::is_upgrade(request) &&
          (origin.empty() || owner.origin_allowed(origin)) && owner.on_playback_) {
        stream.expires_never();
        ws = std::make_unique<websocket::stream<beast::tcp_stream>>(std::move(stream));
        ws->set_option(websocket::stream_base::timeout{
            std::chrono::seconds(5), std::chrono::seconds(15), false});
        ws->read_message_max(4096);
        ws->async_accept(request, [self = shared_from_this()](boost::system::error_code error) {
          if (!error) self->read_command();
        });
        return;
      }
      response = owner.handle(request, origin);
      if (owner.origin_allowed(origin)) {
        response.set(http::field::access_control_allow_origin, origin);
        response.set(http::field::vary, "Origin");
      }
      stream.expires_after(std::chrono::seconds(5));
      http::async_write(stream, response,
          [self = shared_from_this()](boost::system::error_code, std::size_t) {
            boost::system::error_code ignored;
            self->stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
          });
    }

    void read_command() {
      ws->async_read(buffer,
          [self = shared_from_this()](boost::system::error_code error, std::size_t) {
            if (error) return;
            try {
              const auto message = nlohmann::json::parse(beast::buffers_to_string(self->buffer.data()));
              if (!self->ws->got_text() || !message.is_object() ||
                  !message.contains("command") || !message.at("command").is_string()) {
                throw std::invalid_argument("invalid command");
              }
              const auto command = message.at("command").get<std::string>();
              if (command != "status" && command != "pause" && command != "resume" && command != "stop") {
                throw std::invalid_argument("invalid command");
              }
              self->outgoing = self->owner.on_playback_(command);
            } catch (const nlohmann::json::exception &) {
              self->outgoing = R"({"error":"invalid playback command"})";
            } catch (const std::invalid_argument &) {
              self->outgoing = R"({"error":"invalid playback command"})";
            } catch (const std::exception &) {
              self->outgoing = R"({"error":"playback command failed"})";
            }
            self->buffer.consume(self->buffer.size());
            self->ws->text(true);
            self->ws->async_write(asio::buffer(self->outgoing),
                [self](boost::system::error_code write_error, std::size_t) {
                  if (!write_error) self->read_command();
                });
          });
    }
  };

  void accept_next() {
    acceptor_.async_accept([this](boost::system::error_code error, tcp::socket socket) {
      if (!error) std::make_shared<Connection>(*this, std::move(socket))->read_http();
      if (acceptor_.is_open()) accept_next();
    });
  }

  http::response<http::string_body> handle(const http::request<http::string_body> &request,
                                           const std::string &origin) {
    const auto version = request.version();
    if (request.target() == "/v1/playback") {
      if (!origin.empty() && !origin_allowed(origin)) {
        return make_response(version, http::status::forbidden, R"({"error":"origin not allowed"})");
      }
      return make_response(version, http::status::bad_request, R"({"error":"WebSocket upgrade required"})");
    }
    if (request.target() != kSelectionPath) {
      return make_response(version, http::status::not_found, R"({"error":"not found"})");
    }
    if (!origin.empty() && !origin_allowed(origin)) {
      return make_response(version, http::status::forbidden, R"({"error":"origin not allowed"})");
    }
    if (request.method() == http::verb::options) {
      if (origin.empty()) {
        return make_response(version, http::status::forbidden, R"({"error":"origin required"})");
      }
      auto response = make_response(version, http::status::no_content);
      response.set(http::field::access_control_allow_methods, "POST, OPTIONS");
      response.set(http::field::access_control_allow_headers, "Content-Type");
      response.set(http::field::access_control_max_age, "600");
      return response;
    }
    if (request.method() != http::verb::post) {
      auto response = make_response(version, http::status::method_not_allowed,
                                    R"({"error":"POST required"})");
      response.set(http::field::allow, "POST, OPTIONS");
      return response;
    }
    const auto content_type = request[http::field::content_type];
    if (content_type.find("application/json") != 0) {
      return make_response(version, http::status::unsupported_media_type,
                            R"({"error":"application/json required"})");
    }
    try {
      const auto body = nlohmann::json::parse(request.body());
      if (!body.is_object() || !body.contains("text") || !body.at("text").is_string()) {
        return make_response(version, http::status::bad_request,
                              R"({"error":"body must contain a string text field"})");
      }
      auto text = body.at("text").get<std::string>();
      if (blank(text)) {
        return make_response(version, http::status::bad_request,
                              R"({"error":"selection is empty"})");
      }
      on_selection_(std::move(text));
      return make_response(version, http::status::accepted, R"({"status":"queued"})");
    } catch (const nlohmann::json::exception &) {
      return make_response(version, http::status::bad_request, R"({"error":"invalid JSON"})");
    } catch (const std::exception &) {
      return make_response(version, http::status::internal_server_error,
                            R"({"error":"could not queue selection"})");
    }
  }

  asio::io_context context_;
  tcp::acceptor acceptor_;
  tcp::endpoint endpoint_;
  SelectionHandler on_selection_;
  PlaybackHandler on_playback_;
  std::set<std::string> allowed_origins_;
  std::thread worker_;
};

LocalApiServer::LocalApiServer(std::string host, unsigned short port,
                               std::vector<std::string> allowed_origins,
                               SelectionHandler on_selection, PlaybackHandler on_playback)
    : impl_(std::make_unique<Impl>(std::move(host), port, std::move(allowed_origins),
                                   std::move(on_selection), std::move(on_playback))) {}

LocalApiServer::~LocalApiServer() = default;

unsigned short LocalApiServer::port() const { return impl_->port(); }

}  // namespace tts_host
