#include "http.h"
#include <Windows.h>
#include <wininet.h>
#include <string>
#pragma comment(lib, "wininet.lib")

namespace http
{
    // wininet defaults are ~60s+ - a hung host would freeze the overlay
    // render thread on every offsets retry. cap everything at 5s.
    static void SetTimeouts(HINTERNET inet)
    {
        DWORD t = 5000;
        InternetSetOptionA(inet, INTERNET_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
        InternetSetOptionA(inet, INTERNET_OPTION_SEND_TIMEOUT, &t, sizeof(t));
        InternetSetOptionA(inet, INTERNET_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    }

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
        SetTimeouts(inet);
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

    bool Download(const std::string& url, const std::string& outPath)
    {
        if (url.rfind("https://", 0) != 0) return false;
        auto rest = url.substr(8);
        auto slash = rest.find('/');
        std::wstring host(rest.begin(), rest.begin() + (slash == std::string::npos ? rest.size() : slash));
        std::wstring path(slash == std::string::npos ? L"/" : std::wstring(rest.begin() + slash, rest.end()));
        HINTERNET inet = InternetOpenA("NYX/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!inet) return false;
        SetTimeouts(inet);
        std::string hostN(host.begin(), host.end());
        HINTERNET conn = InternetConnectA(inet, hostN.c_str(), INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        if (!conn) { InternetCloseHandle(inet); return false; }
        std::string pathN(path.begin(), path.end());
        HINTERNET req = HttpOpenRequestA(conn, "GET", pathN.c_str(), nullptr, nullptr, nullptr,
            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        bool ok = false;
        if (req && HttpSendRequestA(req, nullptr, 0, nullptr, 0))
        {
            HANDLE f = CreateFileA(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE)
            {
                char buf[16384]; DWORD read = 0; DWORD wrote = 0;
                ok = true;
                while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
                    if (!WriteFile(f, buf, read, &wrote, nullptr) || wrote != read) { ok = false; break; }
                CloseHandle(f);
            }
        }
        if (req) InternetCloseHandle(req);
        InternetCloseHandle(conn);
        InternetCloseHandle(inet);
        return ok;
    }
}
