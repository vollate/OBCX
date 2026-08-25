#include "network/proxy_http_client.hpp"
#include "common/logger.hpp"
#include "network/compression.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <openssl/ssl.h>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

ProxyHttpClient::ProxyHttpClient(asio::io_context &ioc,
                                 ProxyConfig proxy_config,
                                 const common::ConnectionConfig &config)
    : HttpClient(ioc, config), ioc_(ioc),
      proxy_config_(std::move(proxy_config)), target_host_(config.host),
      target_port_(config.port) {
  OBCX_INFO("ProxyHttpClient created, proxy: {}:{} -> target: {}:{}",
            proxy_config_.host, proxy_config_.port, target_host_, target_port_);
}

auto ProxyHttpClient::post(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  auto submission_state = HttpRequestSubmissionState::DefinitelyNotSubmitted;
  try {
    auto tunnel_stream = co_await connect_through_proxy_async();

    http::request<http::string_body> req{http::verb::post, std::string(path),
                                         11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");
    req.set(http::field::content_type, "application/json");

    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    if (!body.empty()) {
      req.body() = body;
      req.prepare_payload();
    }

    HttpResponse response;

    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(tunnel_stream),
                                                      ssl_ctx);

      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_WARN("Failed to set SNI for target: {}", target_host_);
      }

      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                          asio::use_awaitable);

      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      submission_state = HttpRequestSubmissionState::PossiblySubmitted;
      co_await http::async_write(ssl_stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_read(ssl_stream, buffer, parser,
                                asio::use_awaitable);
      auto res = parser.release();

      decompress_inplace(res);
      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      tunnel_stream.expires_after(get_timeout());
      submission_state = HttpRequestSubmissionState::PossiblySubmitted;
      co_await http::async_write(tunnel_stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_read(tunnel_stream, buffer, parser,
                                asio::use_awaitable);
      auto res = parser.release();

      decompress_inplace(res);
      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient POST request failed: {}", e.what());
    throw HttpClientError(std::string("HTTP POST request failed: ") + e.what(),
                          submission_state);
  }
}

auto ProxyHttpClient::get(std::string_view path,
                          const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  try {
    auto tunnel_stream = co_await connect_through_proxy_async();

    http::request<http::string_body> req{http::verb::get, std::string(path),
                                         11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");

    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    HttpResponse response;

    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(tunnel_stream),
                                                      ssl_ctx);

      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_WARN("Failed to set SNI for target: {}", target_host_);
      }

      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                          asio::use_awaitable);

      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_write(ssl_stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_read(ssl_stream, buffer, parser,
                                asio::use_awaitable);
      auto res = parser.release();

      decompress_inplace(res);
      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_write(tunnel_stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_read(tunnel_stream, buffer, parser,
                                asio::use_awaitable);
      auto res = parser.release();

      decompress_inplace(res);
      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient GET request failed: {}", e.what());
    throw HttpClientError(std::string("HTTP GET request failed: ") + e.what());
  }
}

auto ProxyHttpClient::head(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  try {
    auto tunnel_stream = co_await connect_through_proxy_async();

    http::request<http::string_body> req{http::verb::head, std::string(path),
                                         11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");

    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    HttpResponse response;

    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(tunnel_stream),
                                                      ssl_ctx);

      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_WARN("Failed to set SNI for target: {}", target_host_);
      }

      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                          asio::use_awaitable);

      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_write(ssl_stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      parser.skip(true);
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());

      boost::system::error_code ec;
      co_await http::async_read(ssl_stream, buffer, parser,
                                asio::redirect_error(asio::use_awaitable, ec));

      if (ec && ec != http::error::end_of_stream &&
          ec != http::error::partial_message) {
        throw boost::system::system_error(ec);
      }
      auto res = parser.release();

      decompress_inplace(res);
      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_write(tunnel_stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      parser.skip(true);
      tunnel_stream.expires_after(get_timeout());

      boost::system::error_code ec;
      co_await http::async_read(tunnel_stream, buffer, parser,
                                asio::redirect_error(asio::use_awaitable, ec));

      if (ec && ec != http::error::end_of_stream &&
          ec != http::error::partial_message) {
        throw boost::system::system_error(ec);
      }
      auto res = parser.release();

      decompress_inplace(res);
      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient GET request failed: {}", e.what());
    throw HttpClientError(std::string("HTTP HEAD request failed: ") + e.what());
  }
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

auto ProxyHttpClient::post_sync(
    std::string_view path, std::string_view body,
    const std::map<std::string, std::string> &headers) -> HttpResponse {

  try {
    auto tunnel_socket = connect_through_proxy();

    return send_http_request(tunnel_socket, "POST", std::string(path),
                             std::string(body), headers);
  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient POST request failed: {}", e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    return error_response;
  }
}

auto ProxyHttpClient::get_sync(
    std::string_view path, const std::map<std::string, std::string> &headers)
    -> HttpResponse {

  try {
    auto tunnel_socket = connect_through_proxy();

    return send_http_request(tunnel_socket, "GET", std::string(path), "",
                             headers);
  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient GET request failed: {}", e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    return error_response;
  }
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void ProxyHttpClient::close() { HttpClient::close(); }

auto ProxyHttpClient::send_http_request(
    tcp::socket &tunnel_socket, const std::string &method,
    const std::string &path, const std::string &body,
    const std::map<std::string, std::string> &headers) -> HttpResponse {
  try {
    http::verb verb_type =
        (method == "GET") ? http::verb::get : http::verb::post;
    http::request<http::string_body> req{verb_type, path, 11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");

    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    if (!body.empty()) {
      req.set(http::field::content_type, "application/json");
      req.body() = body;
      req.prepare_payload();
    }

    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      auto timeout_sec =
          std::chrono::duration_cast<std::chrono::seconds>(get_timeout())
              .count();
      SSL_CTX_set_timeout(ssl_ctx.native_handle(),
                          static_cast<long>(timeout_sec));

      ssl::stream<tcp::socket> ssl_stream{std::move(tunnel_socket), ssl_ctx};

      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_WARN("Failed to set SNI for target: {}", target_host_);
      }

      boost::system::error_code ec;
      int max_retries = 3;
      for (int retry = 0; retry < max_retries; ++retry) {
        static_cast<void>(
            ssl_stream.handshake( // NOLINT(bugprone-unused-return-value)
                ssl::stream_base::client, ec));
        if (!ec) {
          OBCX_TRACE("SSL handshake success (retry {})", retry);
          break;
        }

        OBCX_WARN("SSL handshake failed (retry {}/{}): {}", retry + 1,
                  max_retries, ec.message());

        if (retry < max_retries - 1) {
          auto wait_time = std::chrono::milliseconds(1000 << retry);
          OBCX_TRACE("Waiting {}ms before retry", wait_time.count());
          std::this_thread::sleep_for(wait_time);

          if (ec.message().find("stream truncated") != std::string::npos) {
            OBCX_TRACE("Stream truncated error detected, might need to rebuild "
                       "tunnel");
          }
        } else {
          throw std::runtime_error(
              fmt::format("SSL handshake failed after {} retries: {}",
                          max_retries, ec.message()));
        }
      }

      boost::system::error_code write_ec;
      http::write(ssl_stream, req, write_ec);
      if (write_ec) {
        throw std::runtime_error(fmt::format(
            "SSL failed to send HTTP request: {}", write_ec.message()));
      }

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      boost::system::error_code read_ec;
      http::read(ssl_stream, buffer, parser, read_ec);
      if (read_ec) {
        throw std::runtime_error(fmt::format(
            "SSL failed to read HTTP response: {}", read_ec.message()));
      }
      auto res = parser.release();

      HttpResponse result;
      result.status_code = static_cast<unsigned int>(res.result_int());
      decompress_inplace(res);
      result.body = res.body();
      result.raw_response = std::move(res);

      return result;
    } else {
      http::write(tunnel_socket, req);

      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit(response_body_limit());
      http::read(tunnel_socket, buffer, parser);
      auto res = parser.release();

      HttpResponse result;
      result.status_code = static_cast<unsigned int>(res.result_int());
      decompress_inplace(res);
      result.body = res.body();
      result.raw_response = std::move(res);

      return result;
    }
  } catch (const std::exception &e) {
    throw std::runtime_error(
        fmt::format("Failed to send HTTP request: {}", e.what()));
  }
}

} // namespace obcx::network
