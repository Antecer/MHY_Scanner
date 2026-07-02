#pragma once

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

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#include "ApiDefs.hpp"
#include "CreateUUID.hpp"
#include "CryptoKit.h"
#include "UtilString.hpp"
#include "TimeStamp.hpp"
#include "AsyncLogger.h"

static const std::string device_id{ CreateUUID::CreateUUID4() };
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
    constexpr std::string_view nextKeys[]{ "app_name=", "bbs=", "biz_key=", "expire=", "ticket=" };

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
    std::size_t valueEnd = url.find('&', valueBegin);
    if (valueEnd == std::string_view::npos)
    {
        valueEnd = url.size();
    }
    return std::string{ url.substr(valueBegin, valueEnd - valueBegin) };
}

struct LoginQRCodeInfo
{
    std::string url{};
    std::string ticket{};
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

inline LoginQRCodeInfo CreateLoginQRCode()
{
    LogInfo("请求账号登录二维码，gameType=" + ToString(loginType));
    const std::string appId{ std::to_string(static_cast<int>(loginType)) };
    auto res = cpr::Post(
        cpr::Url{ api::mhy::hk4e::qrcode_fetch },
        cpr::Body{ nlohmann::json{
            { "app_id", appId },
            { "device", device_id } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (res.error || res.status_code != 200 || res.text.empty())
    {
        LogWarning("账号登录二维码请求异常，gameType=" + ToString(loginType) +
                   ", status=" + std::to_string(res.status_code) +
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
            LogWarning("账号登录二维码请求失败，gameType=" + ToString(loginType) +
                       ", retcode=" + std::to_string(retcode));
            return {};
        }
        result.url = data["data"]["url"].get<std::string>();
    }
    catch (const std::exception& e)
    {
        LogError("账号登录二维码响应解析异常，gameType=" + ToString(loginType) +
                 ", error=" + e.what());
        return {};
    }

    result.url = NormalizeLoginQrcodeUrl(std::move(result.url));
    result.ticket = ExtractUrlQueryValue(result.url, "ticket");
    if (result.url.empty() || result.ticket.empty())
    {
        LogWarning("账号登录二维码响应缺少 URL 或 ticket，gameType=" + ToString(loginType) +
                   ", urlLength=" +
                   std::to_string(result.url.size()) +
                   ", ticketLength=" + std::to_string(result.ticket.size()));
        return {};
    }

    LogInfo("账号登录二维码已生成，gameType=" + ToString(loginType) +
            ", urlLength=" + std::to_string(result.url.size()) +
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

inline std::string getMysUserName(const std::string_view uid)
{
    static constexpr std::string_view url = api::mhy::mys::userinfo;
    const auto response = cpr::Get(
        cpr::Url{ std::format("{}?uid={}", url, uid) });

    const auto data = nlohmann::json::parse(response.text);
    return data["data"]["user_info"]["nickname"].get<std::string>();
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

    const auto response = cpr::Post(
        cpr::Url{ api::mhy::takumi::game_token_stoken },
        cpr::Body{ body },
        cpr::Header{ reqHeaders });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("通过 game_token 换取 STOKEN 请求异常，uid=" + MaskSensitive(uid) +
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
                 ", error=" + e.what());
        return { -1, {}, {} };
    }
    const int retcode = j.value("retcode", -1);

    if (retcode != 0)
    {
        LogWarning("通过 game_token 换取 STOKEN 失败，uid=" + MaskSensitive(uid) +
                   ", retcode=" + std::to_string(retcode) +
                   ", message=" + j.value("message", ""));
        return { retcode, {}, {} };
    }

    try
    {
        const std::string mid{ j["data"]["user_info"]["mid"].get<std::string>() };
        const std::string stoken{ j["data"]["token"]["token"].get<std::string>() };
        LogInfo("通过 game_token 换取 STOKEN 成功，uid=" + MaskSensitive(uid) +
                ", mid=" + MaskSensitive(mid));
        return { 0, mid, stoken };
    }
    catch (const std::exception& e)
    {
        LogError("通过 game_token 换取 STOKEN 数据结构异常，uid=" + MaskSensitive(uid) +
                 ", error=" + e.what());
        return { -1, {}, {} };
    }
}

inline std::tuple<int, std::string> GetGameTokenByStoken(
    const std::string_view stoken,
    const std::string_view mid)
{
    const auto response = cpr::Get(
        cpr::Url{ api::mhy::takumi::game_token },
        cpr::Parameters{
            { "stoken", stoken.data() },
            { "mid", mid.data() } });

    const auto j = nlohmann::json::parse(response.text);
    const int retcode = j.value("retcode", -1);

    if (retcode != 0)
        return { retcode, {} };

    return { 0, j["data"]["game_token"].get<std::string>() };
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

inline bool ScanQRLogin(const std::string_view url, const std::string_view ticket, GameType gameType)
{
    LogInfo("提交二维码扫码请求，gameType=" + ToString(gameType) +
            ", ticket=" + MaskSensitive(ticket));
    const auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Body{ nlohmann::json{
            { "app_id", static_cast<int>(gameType) },
            { "device", device_id },
            { "ticket", ticket } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        LogWarning("二维码扫码请求异常，gameType=" + ToString(gameType) +
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
            LogInfo("二维码扫码成功，gameType=" + ToString(gameType) +
                    ", ticket=" + MaskSensitive(ticket));
            return true;
        }
        LogWarning("二维码扫码失败，gameType=" + ToString(gameType) +
                   ", ticket=" + MaskSensitive(ticket) +
                   ", retcode=" + std::to_string(retcode));
        return false;
    }
    catch (const std::exception& e)
    {
        LogError("二维码扫码响应解析异常，gameType=" + ToString(gameType) +
                 ", ticket=" + MaskSensitive(ticket) +
                 ", error=" + e.what());
        return false;
    }
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
