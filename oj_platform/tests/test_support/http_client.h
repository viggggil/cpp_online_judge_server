#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace oj::test {

struct HttpResponse {
  int status_code{0};
  std::string raw;
  std::string body;
};

inline void send_all(int fd, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) {
      throw std::runtime_error("send failed");
    }
    sent += static_cast<std::size_t>(n);
  }
}

inline HttpResponse http_post_json(const std::string& host,
                                   int port,
                                   const std::string& path,
                                   const std::string& body) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("socket failed");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    throw std::runtime_error("invalid ipv4 address: " + host);
  }

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    throw std::runtime_error("connect failed");
  }

  std::string request;
  request += "POST " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  request += "Connection: close\r\n";
  request += "\r\n";
  request += body;

  send_all(fd, request);

  std::string raw;
  char buffer[4096];

  while (true) {
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n < 0) {
      ::close(fd);
      throw std::runtime_error("recv failed");
    }
    if (n == 0) {
      break;
    }
    raw.append(buffer, static_cast<std::size_t>(n));
  }

  ::close(fd);

  HttpResponse response;
  response.raw = raw;

  const auto first_space = raw.find(' ');
  if (first_space != std::string::npos && first_space + 4 <= raw.size()) {
    response.status_code = std::stoi(raw.substr(first_space + 1, 3));
  }

  const auto body_pos = raw.find("\r\n\r\n");
  if (body_pos != std::string::npos) {
    response.body = raw.substr(body_pos + 4);
  }

  return response;
}

}  // namespace oj::test
