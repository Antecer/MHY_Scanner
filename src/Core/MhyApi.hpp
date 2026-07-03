#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <format>
#include <exception>
#include <random>
#include <sstream>
#include <optional>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#include "ApiDefs.hpp"
#include "CreateUUID.hpp"
#include "CryptoKit.h"
#include "UtilString.hpp"
#include "TimeStamp.hpp"
#include "AsyncLogger.h"

inline std::string CreateLowerAndNumberString(const std::size_t length)
{
    static constexpr std::string_view chars{ "0123456789abcdefghijklmnopqrstuvwxyz" };
    std::random_device rd{};
    std::mt19937 gen{ rd() };
    std::uniform_int_distribution<std::size_t> dist(0, chars.size() - 1);

    std::string result{};
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        result.push_back(chars[dist(gen)]);
    }
    return result;
}

inline std::string CreateLowerHexString(const std::size_t length)
{
    static constexpr std::string_view chars{ "0123456789abcdef" };
    std::random_device rd{};
    std::mt19937 gen{ rd() };
    std::uniform_int_distribution<std::size_t> dist(0, chars.size() - 1);

    std::string result{};
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        result.push_back(chars[dist(gen)]);
    }
    return result;
}

static const std::string device_id{ CreateUUID::CreateUUID4() };
static const std::string device_fp{ CreateLowerHexString(13) };
static const std::string hoyoplay_device_id{ CreateLowerAndNumberString(53) };
static GameType loginType{ GameType::Honkai3 };

inline void ReplaceAll(std::string& value, const std::string_view from, const std::string_view to)
{
    if (from.empty())
    {
        return;
    }

    std::size_t pos{};
    while ((pos = value.find(from, pos)) != std::string::npos)
    {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline std::string NormalizeLoginQrcodeUrl(std::string url)
{
    constexpr std::string_view separators[]{ R"(\u0026)", "u0026", "0026" };
    constexpr std::string_view nextKeys[]{ "app_name=", "bbs=", "biz_key=", "expire=", "ticket=", "tk=", "token_types=" };

    for (const auto separator : separators)
    {
        for (const auto nextKey : nextKeys)
        {
            const std::string pattern{ std::string(separator) + std::string(nextKey) };
            const std::string replacement{ "&" + std::string(nextKey) };
            ReplaceAll(url, pattern, replacement);
        }
    }

    return url;
}

inline std::string ExtractUrlQueryValue(const std::string_view url, const std::string_view key)
{
    const std::string pattern{ std::string(key) + "=" };
    const std::size_t begin = url.find(pattern);
    if (begin == std::string_view::npos)
    {
        return {};
    }

    const std::size_t valueBegin = begin + pattern.size();
    std::size_t valueEnd = url.find_first_of("&#", valueBegin);
    if (valueEnd == std::string_view::npos)
    {
        valueEnd = url.size();
    }
    return std::string{ url.substr(valueBegin, valueEnd - valueBegin) };
}

struct ScanQRCodeResult
{
    bool success{};
    std::string passportTicket{};
    std::vector<std::string> passportTokenTypes{};

    [[nodiscard]] bool hasPassportPayload() const
    {
        return !passportTicket.empty();
    }
};

inline std::string TrimCopy(const std::string_view value)
{
    std::size_t begin{};
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }
    std::size_t end{ value.size() };
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return std::string{ value.substr(begin, end - begin) };
}

inline std::vector<std::string> ParseTokenTypes(const std::string_view value)
{
    std::vector<std::string> result{};
    std::size_t begin{};
    while (begin <= value.size())
    {
        const std::size_t end = value.find(',', begin);
        const std::size_t tokenEnd = end == std::string_view::npos ? value.size() : end;
        std::string token = TrimCopy(value.substr(begin, tokenEnd - begin));
        if (!token.empty())
        {
            result.push_back(std::move(token));
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        begin = end + 1;
    }
    if (result.empty())
    {
        result.push_back("1");
    }
    return result;
}

inline std::vector<std::string> NormalizeTokenTypes(const std::vector<std::string>& tokenTypes)
{
    std::vector<std::string> result{};
    result.reserve(tokenTypes.size());
    for (const auto& tokenType : tokenTypes)
    {
        std::string value = TrimCopy(tokenType);
        if (!value.empty())
        {
            result.push_back(std::move(value));
        }
    }
    if (result.empty())
    {
        result.push_back("1");
    }
    return result;
}

inline std::string BuildStokenCookie(const std::string_view stoken, const std::string_view mid)
{
    return "stoken=" + TrimCopy(stoken) + ";mid=" + TrimCopy(mid);
}

inline ScanQRCodeResult ParseScanQRCodeResult(const nlohmann::json& response)
{
    ScanQRCodeResult result{};
    result.success = true;
    std::string passportUrl{};
    if (const auto data = response.find("data"); data != response.end() && data->is_object())
    {
        passportUrl = data->value("passport_qr_url", "");
    }
    if (passportUrl.empty())
    {
        return result;
    }

    const std::string normalizedUrl = NormalizeLoginQrcodeUrl(passportUrl);
    result.passportTicket = ExtractUrlQueryValue(normalizedUrl, "tk");
    if (result.passportTicket.empty())
    {
        result.passportTicket = ExtractUrlQueryValue(normalizedUrl, "ticket");
    }
    result.passportTokenTypes = ParseTokenTypes(ExtractUrlQueryValue(normalizedUrl, "token_types"));
    return result;
}

struct LoginQRCodeInfo
{
    std::string url{};
    std::string ticket{};
};

struct LoginQRCodeSession
{
    LoginQRCodeState state{ LoginQRCodeState::Expired };
    std::string uid{};
    std::string mid{};
    std::string stoken{};
};

[[nodiscard]] inline std::string DataSignAlgorithmVersionGen1()
{
    return "";
}

[[nodiscard]] inline std::string DataSignAlgorithmVersionGen2(const std::string_view body, const std::string_view query)
{
    const std::string time_now{ std::to_string(GetUnixTimeStampSeconds()) };
    std::random_device rd{};
    std::mt19937 gen{ rd() };
    int lower_bound{ 100001 };
    int upper_bound{ 200000 };
    std::uniform_int_distribution<int> dist(lower_bound, upper_bound);
    const std::string rand{ std::to_string(dist(gen)) };
    std::string m{ "salt=" + std::string(mihoyobbs_salt_x6) + "&t=" + time_now + "&r=" + rand + "&b=" + std::string(body) + "&q=" + std::string(query) };
    return time_now + "," + rand + "," + Md5(m);
}

[[nodiscard]] inline std::string DataSignAlgorithmVersionGen2Prod(const std::string_view body)
{
    const std::string time_now{ std::to_string(GetUnixTimeStampSeconds()) };
    const std::string rand{ CreateLowerAndNumberString(6) };
    const std::string m{ "salt=" + std::string(mihoyobbs_salt_prod) + "&t=" + time_now + "&r=" + rand + "&b=" + std::string(body) + "&q=" };
    return time_now + "," + rand + "," + Md5(m);
}

inline std::string Encrypt(const std::string_view source)
{
    static constinit const char* PublicKey{
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDDvekdPMHN3AYhm/vktJT+YJr7"
        "cI5DcsNKqdsx5DZX0gDuWFuIjzdwButrIYPNmRJ1G8ybDIF7oDW2eEpm5sMbL9zs"
        "9ExXCdvqrn51qELbqj0XxtMTIpaCHFSI50PfPpTFV9Xt/hmyVwokoOXFlAEgCn+Q"
        "CgGs52bFoYMtyi+xEQIDAQAB\n"
        "-----END PUBLIC KEY-----"
    };
    return rsaEncrypt(source.data(), PublicKey);
}

inline cpr::Header GetRequestHeader()
{
    return cpr::Header{
        { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) miHoYoBBS/2.76.1" },
        { "Accept", "application/json" },
        { "x-rpc-app_id", "bll8iq97cem8" },
        { "x-rpc-app_version", "2.76.1" },
        { "x-rpc-client_type", "2" },
        { "x-rpc-device_id", device_id },
        { "x-rpc-device_name", "" },
        { "x-rpc-game_biz", "bbs_cn" },
        { "x-rpc-sdk_version", "2.16.0" }
    };
}

inline cpr::Header GetQRCodeRequestHeader()
{
    return cpr::Header{
        { "User-Agent", "okhttp/4.9.3" },
        { "Accept", "application/json" },
        { "Content-Type", "application/json" },
        { "x-rpc-aigis", "" },
        { "x-rpc-app_id", "bll8iq97cem8" },
        { "x-rpc-app_version", "2.76.1" },
        { "x-rpc-client_type", "4" },
        { "x-rpc-device_id", device_id },
        { "x-rpc-device_name", "Xiaomi MI 6" },
        { "x-rpc-device_model", "MI 6" },
        { "x-rpc-game_biz", "bbs_cn" },
        { "x-rpc-sys_version", "12" }
    };
}

inline cpr::Header GetHoyoPlayRequestHeader()
{
    return cpr::Header{
        { "User-Agent", "HYPContainer/1.1.4.133" },
        { "Accept", "application/json" },
        { "Content-Type", "application/json" },
        { "x-rpc-app_id", "ddxf5dufpuyo" },
        { "x-rpc-client_type", "3" },
        { "x-rpc-device_id", hoyoplay_device_id }
    };
}

inline LoginQRCodeInfo CreateLoginQRCode()
{
    LogInfo("请求账号登录二维码");
    auto res = cpr::Post(
        cpr::Url{ api::mhy::passport::create_qr_login },
        cpr::Body{ nlohmann::json::object().dump() },
        GetHoyoPlayRequestHeader());

    if (res.error || res.status_code != 200 || res.text.empty())
    {
        LogWarning("账号登录二维码请求异常，status=" + std::to_string(res.status_code) +
                   ", error=" + res.error.message);
        return {};
    }

    LoginQRCodeInfo result{};
    try
    {
        const auto data = nlohmann::json::parse(res.text);
        const int retcode = data.value("retcode", -1);
        if (retcode != 0)
        {
            LogWarning("账号登录二维码请求失败，retcode=" + std::to_string(retcode) +
                       ", message=" + data.value("message", ""));
            return {};
        }
        const auto responseData = data.value("data", nlohmann::json::object());
        result.url = responseData.value("url", "");
        result.ticket = responseData.value("ticket", "");
    }
    catch (const std::exception& e)
    {
        LogError("账号登录二维码响应解析异常，error=" + std::string(e.what()));
        return {};
    }

    result.url = NormalizeLoginQrcodeUrl(std::move(result.url));
    if (result.ticket.empty())
    {
        result.ticket = ExtractUrlQueryValue(result.url, "ticket");
    }
    if (result.ticket.empty())
    {
        result.ticket = ExtractUrlQueryValue(result.url, "tk");
    }
    if (result.url.empty() || result.ticket.empty())
    {
        LogWarning("账号登录二维码响应缺少 URL 或 ticket，urlLength=" +
                   std::to_string(result.url.size()) +
                   ", ticketLength=" + std::to_string(result.ticket.size()));
        return {};
    }

    LogInfo("账号登录二维码已生成，urlLength=" + std::to_string(result.url.size()) +
            ", ticket=" + MaskSensitive(result.ticket));
    return result;
}

inline std::string GetLoginQrcodeUrl(const GameType type = loginType)
{
    loginType = type;
    return CreateLoginQRCode().url;
}

inline std::tuple<LoginQRCodeState, std::string, std::string> GetQRCodeState(
    const std::string_view ticket,
    const GameType type = loginType)
{
    const std::string appId{ std::to_string(static_cast<int>(type)) };
    const auto response = cpr::Post(
        cpr::Url{ api::mhy::hk4e::qrcode_query },
        cpr::Body{ nlohmann::json{
            { "app_id", appId },
            { "device", device_id },
            { "ticket", ticket } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("账号登录二维码状态查询异常，gameType=" + ToString(type) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
        return { LoginQRCodeState::Expired, {}, {} };
    }

    nlohmann::json data;
    try
    {
        data = nlohmann::json::parse(response.text);
    }
    catch (const std::exception& e)
    {
        LogError("账号登录二维码状态响应解析异常，gameType=" + ToString(type) +
                 ", ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return { LoginQRCodeState::Expired, {}, {} };
    }

    if (data.value("retcode", -1) != 0)
    {
        LogWarning("账号登录二维码状态查询失败，gameType=" + ToString(type) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(data.value("retcode", -1)));
        return { LoginQRCodeState::Expired, {}, {} };
    }

    static const std::unordered_map<std::string, LoginQRCodeState> stateMap{
        { "Init", LoginQRCodeState::Init },
        { "Scanned", LoginQRCodeState::Scanned },
        { "Confirmed", LoginQRCodeState::Confirmed },
    };

    const auto stat = data["data"]["stat"].get<std::string>();
    const auto it = stateMap.find(stat);

    if (it == stateMap.end())
    {
        LogWarning("账号登录二维码状态未知，gameType=" + ToString(type) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", stat=" + stat);
        return { LoginQRCodeState::Expired, {}, {} };
    }

    if (it->second == LoginQRCodeState::Confirmed)
    {
        LogInfo("账号登录二维码已确认，gameType=" + ToString(type) +
                ", ticket=" + MaskSensitive(ticket));
        try
        {
            const auto payload = nlohmann::json::parse(
                data["data"]["payload"]["raw"].get<std::string>());
            return { LoginQRCodeState::Confirmed,
                     payload["uid"].get<std::string>(),
                     payload["token"].get<std::string>() };
        }
        catch (const std::exception& e)
        {
            LogError("账号登录二维码确认载荷解析异常，gameType=" + ToString(type) +
                     ", ticket=" + MaskSensitive(ticket) +
                     ", error=" + e.what());
            return { LoginQRCodeState::Expired, {}, {} };
        }
    }

    if (it->second == LoginQRCodeState::Scanned)
    {
        LogInfo("账号登录二维码已扫码，gameType=" + ToString(type) +
                ", ticket=" + MaskSensitive(ticket));
    }

    return { it->second, {}, {} };
}

inline LoginQRCodeSession GetLoginQRCodeSession(const std::string_view ticket)
{
    const auto response = cpr::Post(
        cpr::Url{ api::mhy::passport::query_qr_login_status },
        cpr::Body{ nlohmann::json{ { "ticket", std::string(ticket) } }.dump() },
        GetHoyoPlayRequestHeader());

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("账号登录二维码状态查询异常，ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
        return {};
    }

    nlohmann::json data;
    try
    {
        data = nlohmann::json::parse(response.text);
    }
    catch (const std::exception& e)
    {
        LogError("账号登录二维码状态响应解析异常，ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return {};
    }

    const int retcode = data.value("retcode", -1);
    if (retcode != 0)
    {
        LogWarning("账号登录二维码状态查询失败，ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode) +
                   ", message=" + data.value("message", ""));
        return {};
    }

    LoginQRCodeSession session{};
    nlohmann::json responseData{};
    std::string status{};
    try
    {
        responseData = data.value("data", nlohmann::json::object());
        status = responseData.value("status", "");
    }
    catch (const std::exception& e)
    {
        LogError("账号登录二维码状态数据结构异常，ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return {};
    }

    if (status == "Created" || status == "Init")
    {
        session.state = LoginQRCodeState::Init;
        return session;
    }
    if (status == "Scanned")
    {
        LogInfo("账号登录二维码已扫码，ticket=" + MaskSensitive(ticket));
        session.state = LoginQRCodeState::Scanned;
        return session;
    }
    if (status != "Confirmed")
    {
        LogWarning("账号登录二维码状态未知，ticket=" + MaskSensitive(ticket) +
                   ", status=" + status);
        return session;
    }

    session.state = LoginQRCodeState::Confirmed;
    LogInfo("账号登录二维码已确认，ticket=" + MaskSensitive(ticket));
    try
    {
        const auto userInfo = responseData.value("user_info", nlohmann::json::object());
        session.uid = userInfo.value("aid", "");
        session.mid = userInfo.value("mid", "");
        const auto tokens = responseData.value("tokens", nlohmann::json::array());
        for (const auto& token : tokens)
        {
            if (token.value("token_type", 0) == 1)
            {
                session.stoken = token.value("token", "");
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        LogError("账号登录二维码确认数据结构异常，ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return session;
    }

    if (session.uid.empty() || session.mid.empty() || session.stoken.empty())
    {
        LogWarning("账号登录二维码确认缺少可保存的 STOKEN 信息，uid=" + MaskSensitive(session.uid) +
                   ", mid=" + MaskSensitive(session.mid) +
                   ", hasStoken=" + std::string(session.stoken.empty() ? "false" : "true"));
        return session;
    }

    return session;
}

inline std::string getMysUserName(const std::string_view uid)
{
    static constexpr std::string_view url = api::mhy::mys::userinfo;
    const auto response = cpr::Get(
        cpr::Url{ std::format("{}?uid={}", url, uid) });

    const auto data = nlohmann::json::parse(response.text);
    return data["data"]["user_info"]["nickname"].get<std::string>();
}

inline std::tuple<int, std::string, std::string> RequestStokenByGameToken(
    const std::string_view endpoint,
    const std::string_view uid,
    const std::string& body,
    const cpr::Header& headers)
{
    const auto response = cpr::Post(
        cpr::Url{ std::string(endpoint) },
        cpr::Body{ body },
        cpr::Header{ headers });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("通过 game_token 换取 STOKEN 请求异常，uid=" + MaskSensitive(uid) +
                   ", endpoint=" + std::string(endpoint) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
        return { -1, {}, {} };
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(response.text);
    }
    catch (const std::exception& e)
    {
        LogError("通过 game_token 换取 STOKEN 响应解析异常，uid=" + MaskSensitive(uid) +
                 ", endpoint=" + std::string(endpoint) +
                 ", error=" + e.what());
        return { -1, {}, {} };
    }
    const int retcode = j.value("retcode", -1);

    if (retcode != 0)
    {
        LogWarning("通过 game_token 换取 STOKEN 失败，uid=" + MaskSensitive(uid) +
                   ", endpoint=" + std::string(endpoint) +
                   ", retcode=" + std::to_string(retcode) +
                   ", message=" + j.value("message", ""));
        return { retcode, {}, {} };
    }

    try
    {
        const std::string mid{ j["data"]["user_info"]["mid"].get<std::string>() };
        const std::string stoken{ j["data"]["token"]["token"].get<std::string>() };
        LogInfo("通过 game_token 换取 STOKEN 成功，uid=" + MaskSensitive(uid) +
                ", endpoint=" + std::string(endpoint) +
                ", mid=" + MaskSensitive(mid));
        return { 0, mid, stoken };
    }
    catch (const std::exception& e)
    {
        LogError("通过 game_token 换取 STOKEN 数据结构异常，uid=" + MaskSensitive(uid) +
                 ", endpoint=" + std::string(endpoint) +
                 ", error=" + e.what());
        return { -1, {}, {} };
    }
}

inline std::tuple<int, std::string, std::string> GetStokenByGameToken(
    const std::string_view uid,
    const std::string_view game_token)
{
    LogInfo("通过 game_token 换取 STOKEN，uid=" + MaskSensitive(uid));
    long long accountId{};
    try
    {
        accountId = std::stoll(std::string(uid));
    }
    catch (const std::exception& e)
    {
        LogError("通过 game_token 换取 STOKEN 前置参数异常，uid=" + MaskSensitive(uid) +
                 ", error=" + e.what());
        return { -1, {}, {} };
    }

    const std::string body{ nlohmann::json{ { "account_id", accountId }, { "game_token", std::string(game_token) } }.dump() };
    cpr::Header reqHeaders{ GetQRCodeRequestHeader() };
    reqHeaders["DS"] = DataSignAlgorithmVersionGen2(body, "");

    for (const std::string_view endpoint : { std::string_view{ api::mhy::takumi::game_token_stoken }, std::string_view{ api::mhy::passport::game_token_stoken } })
    {
        auto [code, mid, stoken] = RequestStokenByGameToken(endpoint, uid, body, reqHeaders);
        if (code == 0)
        {
            return { code, mid, stoken };
        }
    }
    return { -1, {}, {} };
}

inline std::tuple<int, std::string> GetGameTokenByStoken(
    const std::string_view stoken,
    const std::string_view mid)
{
    const auto response = cpr::Get(
        cpr::Url{ api::mhy::takumi::game_token },
        cpr::Parameters{
            { "stoken", TrimCopy(stoken) },
            { "mid", TrimCopy(mid) } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("通过 STOKEN 获取 game_token 请求异常，mid=" + MaskSensitive(mid) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
        return { -1, {} };
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(response.text);
    }
    catch (const std::exception& e)
    {
        LogError("通过 STOKEN 获取 game_token 响应解析异常，mid=" + MaskSensitive(mid) +
                 ", error=" + e.what());
        return { -1, {} };
    }

    const int retcode = j.value("retcode", -1);
    if (retcode != 0)
    {
        LogWarning("通过 STOKEN 获取 game_token 失败，mid=" + MaskSensitive(mid) +
                   ", retcode=" + std::to_string(retcode) +
                   ", message=" + j.value("message", ""));
        return { retcode, {} };
    }

    try
    {
        return { 0, j["data"]["game_token"].get<std::string>() };
    }
    catch (const std::exception& e)
    {
        LogError("通过 STOKEN 获取 game_token 数据结构异常，mid=" + MaskSensitive(mid) +
                 ", error=" + e.what());
        return { -1, {} };
    }
}

inline std::tuple<int, GeetestData> CreateLoginCaptcha(
    const std::string_view mobile,
    const std::string_view aigis = "")
{
    const std::string body{ nlohmann::json{
        { "area_code", Encrypt("+86") },
        { "mobile", Encrypt(mobile) } }
                                .dump() };
    cpr::Header reqHeaders{ GetRequestHeader() };
    reqHeaders["DS"] = DataSignAlgorithmVersionGen2(body, "");
    if (!aigis.empty())
        reqHeaders["X-Rpc-Aigis"] = aigis;
    const auto response = cpr::Post(
        cpr::Url{ api::mhy::passport::login_by_mobile_captcha },
        cpr::Body{ body },
        cpr::Header{ reqHeaders });

    const auto j = nlohmann::json::parse(response.text);
    const int retcode = j.value("retcode", -1);
    GeetestData result{};
    if (retcode == 0)
    {
        result.action_type = j["data"]["action_type"].get<std::string>();
        return { retcode, result };
    }
    if (retcode == -3101)
    {
        const auto it = response.header.find("X-Rpc-Aigis");
        if (it != response.header.end())
        {
            const auto aigisJson = nlohmann::json::parse(it->second);
            const auto captchaJson = nlohmann::json::parse(aigisJson["data"].get<std::string>());

            result.session_id = aigisJson["session_id"].get<std::string>();
            result.mmt_type = aigisJson["mmt_type"].get<int>();
            result.gt = captchaJson["gt"].get<std::string>();
            result.challenge = captchaJson["challenge"].get<std::string>();
            result.GeeTestType = ServerType::Official;
        }
    }
    return { retcode, result };
}

inline auto LoginByMobileCaptcha(const std::string_view actionType, const std::string_view mobile, const std::string_view captcha, const std::string_view aigis = "")
{
    struct
    {
        int retcode{};
        struct
        {
            std::string V2Token{};
            std::string aid{};
            std::string mid{};
        } data;
    } result;
#if 0
	const std::string RequestBody{ std::format(R"({{"area_code":"{}","action_type":"{}","captcha":"{}","mobile":"{}"}})", Encrypt("+86"), actionType, captcha, Encrypt(mobile)) };
    std::map<std::string, std::string> headers{ GetRequestHeader() };
    headers["DS"] = DataSignAlgorithmVersionGen2(RequestBody, "");
    if (!aigis.empty())
    {
        headers["X-Rpc-Aigis"] = aigis;
    }
    HttpClient h;
    std::string s;
    h.PostRequest(s, URL_LoginByMobileCaptcha, RequestBody, headers);
    //std::cout << s << std::endl;
    json::Json j{};
    j.parse(s);
    result.retcode = j["retcode"];
    if (result.retcode == -3205)
    {
        return result;
    }
    else if (result.retcode == 0)
    {
        result.data.V2Token = j["data"]["token"]["token"];
        result.data.aid = j["data"]["user_info"]["aid"];
        result.data.mid = j["data"]["user_info"]["mid"];
    }
#endif
    return result;
}

inline bool SubmitPassportQRCode(
    const std::string_view url,
    const std::string_view ticket,
    const std::vector<std::string>& tokenTypes,
    const std::string_view stoken,
    const std::string_view mid,
    const std::string_view action)
{
    if (TrimCopy(stoken).empty() || TrimCopy(mid).empty())
    {
        LogWarning("通行证二维码" + std::string(action) + "缺少 STOKEN 或 mid，ticket=" + MaskSensitive(ticket));
        return false;
    }

    const auto normalizedTokenTypes = NormalizeTokenTypes(tokenTypes);
    const std::string body{ nlohmann::json{
        { "ticket", std::string(ticket) },
        { "token_types", normalizedTokenTypes } }
                                .dump() };
    cpr::Header headers{
        { "Content-Type", "application/json" },
        { "Accept", "application/json" },
        { "Cookie", BuildStokenCookie(stoken, mid) },
        { "DS", DataSignAlgorithmVersionGen2Prod(body) },
        { "x-rpc-app_id", "bll8iq97cem8" },
        { "x-rpc-client_type", "2" },
        { "x-rpc-device_id", device_id },
        { "x-rpc-device_fp", device_fp },
        { "x-rpc-device_name", "Windows" },
        { "x-rpc-device_model", "Windows" },
        { "x-rpc-sys_version", "Windows 10" },
        { "x-rpc-game_biz", "bbs_cn" },
        { "x-rpc-app_version", "2.107.0" },
        { "x-rpc-sdk_version", "2.42.0" },
        { "x-rpc-account_version", "2.42.0" }
    };

    LogInfo("提交通行证二维码" + std::string(action) + "请求，ticket=" + MaskSensitive(ticket));
    const auto response = cpr::Post(
        cpr::Url{ std::string(url) },
        cpr::Body{ body },
        cpr::Header{ headers });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("通行证二维码" + std::string(action) + "请求异常，ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
        return false;
    }

    try
    {
        const auto j = nlohmann::json::parse(response.text);
        const int retcode = j.value("retcode", -1);
        if (retcode == 0)
        {
            LogInfo("通行证二维码" + std::string(action) + "成功，ticket=" + MaskSensitive(ticket));
            return true;
        }
        LogWarning("通行证二维码" + std::string(action) + "失败，ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode) +
                   ", message=" + j.value("message", ""));
        return false;
    }
    catch (const std::exception& e)
    {
        LogError("通行证二维码" + std::string(action) + "响应解析异常，ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return false;
    }
}

inline bool ScanPassportQRCode(
    const std::string_view ticket,
    const std::vector<std::string>& tokenTypes,
    const std::string_view stoken,
    const std::string_view mid)
{
    return SubmitPassportQRCode(api::mhy::passport::scan_qr_login, ticket, tokenTypes, stoken, mid, "扫码");
}

inline bool ConfirmPassportQRCode(
    const std::string_view ticket,
    const std::vector<std::string>& tokenTypes,
    const std::string_view stoken,
    const std::string_view mid)
{
    return SubmitPassportQRCode(api::mhy::passport::confirm_qr_login, ticket, tokenTypes, stoken, mid, "确认登录");
}

inline ScanQRCodeResult ScanQRLoginDetailed(const std::string_view url, const std::string_view ticket, GameType gameType)
{
    LogInfo("提交二维码扫码请求，gameType=" + ToString(gameType) +
            ", ticket=" + MaskSensitive(ticket));
    const auto response = cpr::Post(
        cpr::Url{ std::string(url) },
        cpr::Body{ nlohmann::json{
            { "app_id", static_cast<int>(gameType) },
            { "device", device_id },
            { "passport_app_id", "bll8iq97cem8" },
            { "ts", GetUnixTimeStampMilliseconds() },
            { "ticket", ticket } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("二维码扫码请求异常，gameType=" + ToString(gameType) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
        return {};
    }

    try
    {
        const auto j = nlohmann::json::parse(response.text);
        const int retcode = j.value("retcode", -1);
        if (retcode == 0)
        {
            LogInfo("二维码扫码成功，gameType=" + ToString(gameType) +
                    ", ticket=" + MaskSensitive(ticket));
            return ParseScanQRCodeResult(j);
        }
        LogWarning("二维码扫码失败，gameType=" + ToString(gameType) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode) +
                   ", message=" + j.value("message", ""));
        return {};
    }
    catch (const std::exception& e)
    {
        LogError("二维码扫码响应解析异常，gameType=" + ToString(gameType) +
                 ", ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return {};
    }
}

inline bool ScanQRLogin(const std::string_view url, const std::string_view ticket, GameType gameType)
{
    return ScanQRLoginDetailed(url, ticket, gameType).success;
}

inline bool ConfirmQRLogin(const std::string_view url, const std::string_view uid, const std::string_view gameToken, const std::string_view ticket, GameType gameType)
{
    LogInfo("提交二维码登录确认请求，gameType=" + ToString(gameType) +
            ", uid=" + MaskSensitive(uid) +
            ", ticket=" + MaskSensitive(ticket));
    const auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Body{ nlohmann::json{
            { "app_id", static_cast<int>(gameType) },
            { "device", device_id },
            { "ticket", ticket },
            { "payload", { { "proto", "Account" }, { "raw", nlohmann::json{ { "uid", uid }, { "token", gameToken } }.dump() } } } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("二维码登录确认请求异常，gameType=" + ToString(gameType) +
                   ", uid=" + MaskSensitive(uid) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
    }

    try
    {
        const auto j = nlohmann::json::parse(response.text);
        const int retcode = j.value("retcode", -1);
        if (retcode == 0)
        {
            LogInfo("二维码登录确认成功，gameType=" + ToString(gameType) +
                    ", uid=" + MaskSensitive(uid) +
                    ", ticket=" + MaskSensitive(ticket));
            return true;
        }
        LogWarning("二维码登录确认失败，gameType=" + ToString(gameType) +
                   ", uid=" + MaskSensitive(uid) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode));
        return false;
    }
    catch (const std::exception& e)
    {
        LogError("二维码登录确认响应解析异常，gameType=" + ToString(gameType) +
                 ", uid=" + MaskSensitive(uid) +
                 ", ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return false;
    }
}

inline std::string makeSign(const nlohmann::json& data)
{
    std::string param;
    for (auto& [key, value] : data.items())
    {
        if (key == "sign")
            continue;
        const std::string strVal = value.is_string() ? value.get<std::string>() : value.dump();

        param += key + "=" + strVal + "&";
    }
    if (!param.empty())
        param.pop_back();
#ifdef _DEBUG
    std::cout << "make_param = " << param << std::endl;
#endif
    constexpr std::string_view key = "0ebc517adb1b62c6b408df153331f9aa";
    return HmacSha256(param, std::string(key));
}

inline std::string& getOAString()
{
    static std::string value = []() {
        const auto response = cpr::Get(cpr::Url{ "https://api.v6qbb.cloud/get_bh3_bilibili_oa" });
        if (response.text.empty())
            throw std::runtime_error("");
        return response.text;
    }();
    return value;
}

inline std::tuple<int, std::string, std::string, std::string> GetBH3ExternalLoginInfo(const std::string& uid, const std::string& access_key)
{
    LogInfo("请求崩坏3 B服外部登录信息，uid=" + MaskSensitive(uid));
    const std::string bodyData = std::format(R"({{"access_key":"{}","uid":{}}})", access_key, uid);

    nlohmann::json body{
        { "device", "0000000000000000" },
        { "app_id", 1 },
        { "channel_id", 14 },
        { "data", bodyData }
    };
    body["sign"] = makeSign(body);
    const auto response = cpr::Post(
        cpr::Url{ api::mhy::bh3::v2_login },
        cpr::Header{ { "Content-Type", "application/json" } },
        cpr::Body{ body.dump() });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("崩坏3 B服外部登录信息请求异常，uid=" + MaskSensitive(uid) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(response.text);
    }
    catch (const std::exception& e)
    {
        LogError("崩坏3 B服外部登录信息响应解析异常，uid=" + MaskSensitive(uid) +
                 ", error=" + e.what());
        return { -1, {}, {}, {} };
    }
    const int retcode = j.value("retcode", -1);

#ifdef _DEBUG
    std::cout << "崩坏3验证完成 : " << response.text << std::endl;
#endif

    if (retcode != 0)
    {
        LogWarning("崩坏3 B服外部登录信息请求失败，uid=" + MaskSensitive(uid) +
                   ", retcode=" + std::to_string(retcode));
        return { retcode, {}, {}, {} };
    }

    LogInfo("崩坏3 B服外部登录信息请求成功，uid=" + MaskSensitive(uid));
    return { 0,
             j["data"]["open_id"].get<std::string>(),
             j["data"]["combo_token"].get<std::string>(),
             j["data"]["combo_id"].get<std::string>() };
}

inline ScanRet scanCheck(const std::string& ticket, const std::string_view scanUrl = api::mhy::bh3::qrcode_scan)
{
    LogInfo("提交崩坏3 B服二维码扫码请求，ticket=" + MaskSensitive(ticket));
    const std::string body = nlohmann::json{
        { "app_id", "1" },
        { "device", "0000000000000000" },
        { "ticket", ticket },
        { "ts", GetUnixTimeStampSeconds() }
    }.dump();

    const auto response = cpr::Post(
        cpr::Url{ scanUrl },
        cpr::Body{ body },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("崩坏3 B服二维码扫码请求异常，ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
    }

    try
    {
        const auto j = nlohmann::json::parse(response.text);
        const int retcode = j.value("retcode", -1);
        if (retcode == 0)
        {
            LogInfo("崩坏3 B服二维码扫码成功，ticket=" + MaskSensitive(ticket));
            return ScanRet::SUCCESS;
        }
        LogWarning("崩坏3 B服二维码扫码失败，ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode));
        return ScanRet::FAILURE_1;
    }
    catch (const std::exception& e)
    {
        LogError("崩坏3 B服二维码扫码响应解析异常，ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return ScanRet::FAILURE_1;
    }
}

inline ScanRet scanConfirm(const std::string& ticket, const std::string& uid, const std::string& access_key, const std::string& name, const std::string_view confirmUrl = api::mhy::bh3::qrcode_confirm)
{
    LogInfo("提交崩坏3 B服二维码登录确认请求，uid=" + MaskSensitive(uid) +
            ", ticket=" + MaskSensitive(ticket));
    auto [code, open_id, combo_token, combo_id] = GetBH3ExternalLoginInfo(uid, access_key);
    if (code != 0)
    {
        LogWarning("崩坏3 B服二维码登录确认前置登录信息失败，uid=" + MaskSensitive(uid) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", code=" + std::to_string(code));
        return ScanRet::FAILURE_2;
    }

    const auto raw =
        nlohmann::json{
            { "heartbeat", false },
            { "open_id", open_id },
            { "device_id", "0000000000000000" },
            { "app_id", "1" },
            { "channel_id", "14" },
            { "combo_token", combo_token },
            { "asterisk_name", name },
            { "combo_id", combo_id },
            { "account_type", "2" }
        };

    const auto ext =
        nlohmann::json{
            { "data", nlohmann::json{
                          { "accountType", "2" },
                          { "accountID", "" },
                          { "c", open_id },
                          { "accountToken", combo_token },
                          { "dispatch", getOAString() } } }
        };

    const nlohmann::json postBody{
        { "device", "0000000000000000" },
        { "app_id", 1 },
        { "ts", GetUnixTimeStampSeconds() },
        { "ticket", ticket },
        { "payload", nlohmann::json{
                         { "proto", "Combo" },
                         { "raw", raw.dump() },
                         { "ext", ext.dump() } } }
    };

#ifdef _DEBUG
    std::cout << postBody.dump() << std::endl;
#endif

    const auto response = cpr::Post(
        cpr::Url{ confirmUrl },
        cpr::Header{ { "Content-Type", "application/json" } },
        cpr::Body{ postBody.dump() });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("崩坏3 B服二维码登录确认请求异常，uid=" + MaskSensitive(uid) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", status=" + std::to_string(response.status_code) +
                   ", error=" + response.error.message);
    }

    try
    {
        const auto j = nlohmann::json::parse(response.text);
        const int retcode = j.value("retcode", -1);
        if (retcode == 0)
        {
            LogInfo("崩坏3 B服二维码登录确认成功，uid=" + MaskSensitive(uid) +
                    ", ticket=" + MaskSensitive(ticket));
            return ScanRet::SUCCESS;
        }
        LogWarning("崩坏3 B服二维码登录确认失败，uid=" + MaskSensitive(uid) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode));
        return ScanRet::FAILURE_2;
    }
    catch (const std::exception& e)
    {
        LogError("崩坏3 B服二维码登录确认响应解析异常，uid=" + MaskSensitive(uid) +
                 ", ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return ScanRet::FAILURE_2;
    }
}
