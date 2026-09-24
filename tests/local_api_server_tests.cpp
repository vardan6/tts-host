#include "tts_host/local_api_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <future>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

http::response<http::string_body> request(unsigned short port,
                                          http::verb method,
                                          const std::string &origin,
                                          const std::string &body = {}) {
  asio::io_context context;
  tcp::socket socket(context);
  socket.connect({asio::ip::address_v4::loopback(), port});
  http::request<http::string_body> message{method, "/v1/selection", 11};
  message.set(http::field::host, "127.0.0.1");
  message.set(http::field::origin, origin);
  if (method == http::verb::post) {
    message.set(http::field::content_type, "application/json");
    message.body() = body;
    message.prepare_payload();
  }
  http::write(socket, message);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  return response;
}

void only_allowlisted_extension_can_submit_selection() {
  std::promise<std::string> captured;
  auto captured_text = captured.get_future();
  tts_host::LocalApiServer server(
      "127.0.0.1", 0, {"chrome-extension://approved"},
      [&captured](std::string text) { captured.set_value(std::move(text)); });

  auto accepted = request(server.port(), http::verb::post, "chrome-extension://approved",
                          R"({"text":"selected paragraph"})");
  require(accepted.result() == http::status::accepted,
          "allowlisted extension selection was not accepted");
  require(accepted[http::field::access_control_allow_origin] ==
              "chrome-extension://approved",
          "host did not echo the explicit allowed origin");
  require(captured_text.wait_for(std::chrono::seconds(1)) == std::future_status::ready &&
              captured_text.get() == "selected paragraph",
          "selection text did not reach the host callback");

  auto rejected = request(server.port(), http::verb::post, "chrome-extension://other",
                          R"({"text":"must not interrupt"})");
  require(rejected.result() == http::status::forbidden,
          "unlisted extension origin was accepted");
  require(rejected.find(http::field::access_control_allow_origin) == rejected.end(),
          "host sent CORS permission to an unlisted origin");
}
}  // namespace

int main() {
  try {
    only_allowlisted_extension_can_submit_selection();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
