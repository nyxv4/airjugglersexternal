#include "http.h"
#include <Windows.h>
#include <wininet.h>
#include <string>
#pragma comment(lib, "wininet.lib")

namespace http
{
    std::string Get(const std::string& url)
    {
        // minimal parse: https://host/path
        if (url.rfind("https://", 0) != 0) return "";
        auto rest = url.substr(8);
        auto slash = rest.find('/');
        std::wstring host(rest.begin(), rest.begin() + (slash == std::string::npos ? rest.size() : slash));
        std::wstring path(slash == std::string::npos ? L"/" : std::wstring(rest.begin() + slash, rest.end()));
        HINTERNET inet = InternetOpenA("NYX/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!inet) return "";
        std::string hostN(host.begin(), host.end());
        HINTERNET conn = InternetConnectA(inet, hostN.c_str(), INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        if (!conn) { InternetCloseHandle(inet); return ""; }
        std::string pathN(path.begin(), path.end());
        HINTERNET req = HttpOpenRequestA(conn, "GET", pathN.c_str(), nullptr, nullptr, nullptr,
            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        std::string result;
        if (req && HttpSendRequestA(req, nullptr, 0, nullptr, 0))
        {
            char buf[8192]; DWORD read = 0;
            while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
                result.append(buf, read);
        }
        if (req) InternetCloseHandle(req);
        InternetCloseHandle(conn);
        InternetCloseHandle(inet);
        return result;
    }
}
