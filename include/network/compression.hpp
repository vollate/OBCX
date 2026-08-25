#ifndef OBCX_INCLUDE_NETWORK_COMPRESSION_HPP_
#define OBCX_INCLUDE_NETWORK_COMPRESSION_HPP_

#include <boost/beast/http.hpp>

namespace obcx::network {

namespace http = boost::beast::http;

void decompress_inplace(http::response<http::string_body> &res);

} // namespace obcx::network

#endif // OBCX_INCLUDE_NETWORK_COMPRESSION_HPP_
