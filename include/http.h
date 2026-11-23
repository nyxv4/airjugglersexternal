#pragma once
#include <string>

namespace http
{
    // HTTPS GET via WinINet. Returns empty string on failure.
    std::string Get(const std::string& url);

    // HTTPS download to a local file. Returns true on success.
    bool Download(const std::string& url, const std::string& outPath);
}
