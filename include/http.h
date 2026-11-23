#pragma once
#include <string>

namespace http
{
    // HTTPS GET via WinINet. Returns empty string on failure.
    std::string Get(const std::string& url);
}
