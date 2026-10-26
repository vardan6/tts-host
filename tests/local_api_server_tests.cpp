#include "tts_host/local_api_server.hpp"
#include "tts_host/playback_controller.hpp"
#include <nlohmann/json.hpp>

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
namespace websocket = beast::websocket;
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

void conflicting_listener_fails_clearly() {
  tts_host::LocalApiServer first("127.0.0.1", 0, {}, [](std::string) {});
  bool rejected = false;
  try {
    tts_host::LocalApiServer second("127.0.0.1", first.port(), {}, [](std::string) {});
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    rejected = message.find("could not bind local API to 127.0.0.1:" +
                             std::to_string(first.port())) != std::string::npos;
  }
  require(rejected, "second listener did not fail with the conflicting endpoint");
  require(request(first.port(), http::verb::post, "", R"({"text":"still owned"})").result()
              == http::status::accepted, "collision disturbed the first listener");
#ifdef _WIN32
  asio::io_context context;
  tcp::acceptor competing(context);
  competing.open(tcp::v4());
  competing.set_option(tcp::acceptor::reuse_address(true));
  boost::system::error_code error;
  competing.bind({asio::ip::address_v4::loopback(), first.port()}, error);
  require(static_cast<bool>(error), "SO_REUSEADDR bypassed exclusive ownership");
#endif
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
void playback_controls_and_connection_isolation() {
  tts_host::PlaybackController controller;
  auto control = controller.begin();
  controller.mark_playing(control.get());
  controller.set_generated_seconds(control.get(), 12.0);
  control->update_position(3.0);
  int commands = 0;
  auto server = std::make_unique<tts_host::LocalApiServer>(
      "127.0.0.1", static_cast<unsigned short>(0), std::vector<std::string>{"chrome-extension://approved"},
      [](std::string) {}, [&controller, &commands](const std::string &command) {
        ++commands;
        if (command == "pause") controller.pause();
        if (command == "resume") controller.resume();
        if (command == "stop") controller.stop();
        return controller.status();
      });
  asio::io_context context;
  // CORS does not protect a WebSocket handshake: reject the actual Origin.
  for (const std::string origin : {"chrome-extension://other", "https://untrusted.example"}) {
    websocket::stream<tcp::socket> denied(context);
    denied.next_layer().connect({asio::ip::address_v4::loopback(), server->port()});
    denied.set_option(websocket::stream_base::decorator([origin](websocket::request_type &req) {
      req.set(http::field::origin, origin);
    }));
    websocket::response_type response;
    boost::system::error_code error;
    denied.handshake(response, "127.0.0.1", "/v1/playback", error);
    require(error && response.result() == http::status::forbidden,
            "unlisted WebSocket origin was accepted");
  }
  require(commands == 0, "rejected handshake changed playback");
  websocket::stream<tcp::socket> ws(context);
  ws.next_layer().connect({asio::ip::address_v4::loopback(), server->port()});
  ws.set_option(websocket::stream_base::decorator([](websocket::request_type &req) {
    req.set(http::field::origin, "chrome-extension://approved");
  }));
  ws.handshake("127.0.0.1", "/v1/playback");
  auto exchange = [&ws](const std::string &message) {
    ws.write(asio::buffer(message));
    beast::flat_buffer buffer;
    ws.read(buffer);
    return nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  };
  auto status = exchange(R"({"command":"status"})");
  require(status.at("positionSeconds") == 3.0 && status.at("generatedSeconds") == 12.0,
          "status lost source-audio position");
  require(status.at("state") == "playing" && status.at("error") == "", "status names its state");
  require(exchange(R"({"command":"pause"})").at("state") == "paused", "pause reply names the paused state");
  require(control->paused(), "browser pause did not pause active playback");
  exchange(R"({"command":"resume"})");
  require(!control->paused(), "browser resume did not resume active playback");
  const int before_invalid = commands;
  for (const std::string message : {"{", R"({"command":"seek"})", R"({"command":3})"}) {
    require(exchange(message).contains("error"), "invalid command was accepted");
  }
  ws.binary(true);
  require(exchange(R"({"command":"stop"})").contains("error"), "binary command was accepted");
  ws.text(true);
  require(commands == before_invalid && !control->cancelled(),
          "invalid command reached playback handler");
  auto accepted = request(server->port(), http::verb::post, "chrome-extension://approved",
                          R"({"text":"concurrent selection"})");
  require(accepted.result() == http::status::accepted,
          "open WebSocket blocked selection ingress");
  exchange(R"({"command":"stop"})");
  require(control->cancelled(), "browser stop did not cancel playback");
  // Closing the browser control page must leave the utterance alone.
  auto next = controller.begin();
  ws.close(websocket::close_code::normal);
  require(!next->cancelled(), "closing control page cancelled speech");
  websocket::stream<tcp::socket> idle(context);
  idle.next_layer().connect({asio::ip::address_v4::loopback(), server->port()});
  idle.handshake("127.0.0.1", "/v1/playback");
  // Host shutdown must also finish with an idle upgraded socket and a partial HTTP request.
  tcp::socket partial(context);
  partial.connect({asio::ip::address_v4::loopback(), server->port()});
  asio::write(partial, asio::buffer(std::string("GET /v1/selection HTTP/1.1\r\n")));
  server.reset();
}
}  // namespace

int main() {
  try {
    conflicting_listener_fails_clearly();
    only_allowlisted_extension_can_submit_selection();
    playback_controls_and_connection_isolation();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
