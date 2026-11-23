#include "offsets.h"
#include "http.h"
#include <regex>
#include <mutex>

namespace offsets
{
    static std::unordered_map<std::string, std::unordered_map<std::string, uintptr_t>> table;
    static std::string version = "unknown";
    static bool ready = false;
    static std::mutex mtx;

    static void parse(const std::string& data)
    {
        std::smatch vm;
        static const std::regex verRe(R"rx(ClientVersion = "([^"]+)")rx");
        if (std::regex_search(data, vm, verRe)) version = vm[1].str();

        static const std::regex classRe(R"rx(public static class (\w+)\s*\{([\s\S]*?)\})rx");
        static const std::regex fieldRe(R"rx(public const long\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+);)rx");

        for (std::sregex_iterator it(data.begin(), data.end(), classRe), end; it != end; ++it)
        {
            std::string cls = (*it)[1].str();
            std::string body = (*it)[2].str();
            for (std::sregex_iterator fit(body.begin(), body.end(), fieldRe), fend; fit != fend; ++fit)
            {
                std::string name = (*fit)[1].str();
                std::string val = (*fit)[2].str();
                uintptr_t v = val.rfind("0x", 0) == 0 ? std::stoull(val, nullptr, 16) : std::stoull(val);
                table[cls][name] = v;
            }
        }
    }

    bool Fetch()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (ready) return true;

        // theo's dumper moved to offsets.imtheo.lol (the old host 308-redirects)
        std::string data = http::Get("https://offsets.imtheo.lol/Offsets.cs");
        if (data.empty()) return false;

        parse(data);
        ready = !table.empty();
        return ready;
    }

    uintptr_t Get(const char* cls, const char* field)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto ci = table.find(cls);
        if (ci == table.end()) return 0;
        auto fi = ci->second.find(field);
        return fi == ci->second.end() ? 0 : fi->second;
    }

    bool Ready() { return ready; }
    const std::string& ClientVersion() { return version; }
}
