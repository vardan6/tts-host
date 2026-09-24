#include "tts_host/local_api_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <set>
#include <stdexcept>
#include <thread>

namespace tts_host {
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
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
       SelectionHandler on_selection)
      : context_(), acceptor_(context_), on_selection_(std::move(on_selection)),
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
    acceptor_.listen(asio::socket_base::max_listen_connections, error);
    if (error) throw std::runtime_error("could not listen on local API socket: " + error.message());
    worker_ = std::thread(&Impl::serve, this);
  }

  ~Impl() {
    stopping_.store(true);
    boost::system::error_code error;
    tcp::socket wake(context_);
    wake.connect(endpoint_, error);
    if (worker_.joinable()) worker_.join();
    acceptor_.close(error);
  }

  unsigned short port() const { return acceptor_.local_endpoint().port(); }

 private:
  bool origin_allowed(const std::string &origin) const {
    return !origin.empty() && allowed_origins_.contains(origin);
  }

  void serve() {
    while (!stopping_.load()) {
      tcp::socket socket(context_);
      boost::system::error_code error;
      acceptor_.accept(socket, error);
      if (stopping_.load()) return;
      if (error) continue;
      serve_one(std::move(socket));
    }
  }

  void serve_one(tcp::socket socket) {
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(5));
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(kMaximumRequestBytes);
    boost::system::error_code error;
    http::read(stream, buffer, parser, error);
    if (error) return;
    const auto request = parser.release();
    const auto origin_field = request.find(http::field::origin);
    const std::string origin = origin_field == request.end() ? "" : std::string(origin_field->value());
    auto response = handle(request, origin);
    if (origin_allowed(origin)) {
      response.set(http::field::access_control_allow_origin, origin);
      response.set(http::field::vary, "Origin");
    }
    stream.expires_after(std::chrono::seconds(5));
    http::write(stream, response, error);
    stream.socket().shutdown(tcp::socket::shutdown_send, error);
  }

  http::response<http::string_body> handle(const http::request<http::string_body> &request,
                                           const std::string &origin) {
    const auto version = request.version();
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
  std::set<std::string> allowed_origins_;
  std::atomic_bool stopping_{false};
  std::thread worker_;
};

LocalApiServer::LocalApiServer(std::string host, unsigned short port,
                               std::vector<std::string> allowed_origins,
                               SelectionHandler on_selection)
    : impl_(std::make_unique<Impl>(std::move(host), port, std::move(allowed_origins),
                                   std::move(on_selection))) {}

LocalApiServer::~LocalApiServer() = default;

unsigned short LocalApiServer::port() const { return impl_->port(); }

}  // namespace tts_host
