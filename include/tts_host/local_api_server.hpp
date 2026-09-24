#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tts_host {

class LocalApiServer {
 public:
  using SelectionHandler = std::function<void(std::string)>;

  LocalApiServer(std::string host, unsigned short port, std::vector<std::string> allowed_origins,
                 SelectionHandler on_selection);
  ~LocalApiServer();
  LocalApiServer(const LocalApiServer &) = delete;
  LocalApiServer &operator=(const LocalApiServer &) = delete;
  unsigned short port() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tts_host
