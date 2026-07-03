#include "QRCodeForScreen.h"

#include <chrono>
#include <thread>
#include <utility>

#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

#include "AsyncLogger.h"
#include "QRScanner.h"
#include "ScreenScan.h"
#include "ScreenShotDXGI.hpp"

#define DELAYED 200

QRCodeForScreen::QRCodeForScreen(QObject* parent) :
    QThread(parent),
    m_stop(false)
{
    m_config = &ConfigDate::getInstance();
}

QRCodeForScreen::~QRCodeForScreen()
{
    if (!this->isInterruptionRequested())
    {
        m_stop.store(false);
    }
    this->requestInterruption();
    this->wait();
}

void QRCodeForScreen::setLoginInfo(const std::string& uid, const std::string& token)
{
    this->uid = uid;
    this->gameToken = token;
    this->stoken.clear();
    this->mid.clear();
    this->passportQrPayload = {};
}

void QRCodeForScreen::setLoginInfo(const std::string& uid, const std::string& gameToken, const std::string& stoken, const std::string& mid)
{
    this->uid = uid;
    this->gameToken = gameToken;
    this->stoken = stoken;
    this->mid = mid;
    this->passportQrPayload = {};
}

void QRCodeForScreen::setLoginInfo(const std::string& uid, const std::string& token, const std::string& name)
{
    this->uid = uid;
    this->gameToken = token;
    this->m_name = name;
    this->stoken.clear();
    this->mid.clear();
    this->passportQrPayload = {};
}

void QRCodeForScreen::LoginOfficial()
{
    LogInfo("屏幕官服扫码循环开始");
    QThreadPool threadPool;
    threadPool.setMaxThreadCount(threadNumber);
    std::mutex mtx;
    ScreenShotDXGI screenshotdxgi;
    int w{ 0 };
    int h{ 0 };
    screenshotdxgi.InitDevice();
    screenshotdxgi.InitDupl(0, w, h);
    LogInfo("屏幕采集初始化完成，width=" + std::to_string(w) +
            ", height=" + std::to_string(h));
    long mBufferSize = w * h * 4;
    uint8_t* mBuffer = new UCHAR[mBufferSize];
    while (m_stop.load())
    {
        screenshotdxgi.getFrame(100);
        screenshotdxgi.copyFrameToBuffer(&mBuffer, mBufferSize);
        cv::Mat img;
        cv::resize(cv::Mat(h, w, CV_8UC4, mBuffer), img, { 1280, 720 });
#ifndef SHOW
        cv::imshow("Video_Stream", img);
        cv::waitKey(1);
#endif
        threadPool.tryStart([&, img = std::move(img)]() {
            thread_local QRScanner qrScanners;
            std::string str;
            qrScanners.decodeSingle(img, str);
            if (str.size() < 85)
            {
                return;
            }
            std::string_view view(str.c_str() + 79, 3);
            if (!setGameType.contains(view))
            {
                return;
            }
            setGameType[view]();
            const std::string_view ticket(str.data() + str.size() - 24, 24);
            if (lastTicket == ticket)
            {
                return;
            }
            LogInfo("屏幕识别到二维码，gameType=" + ToString(gameType) +
                    ", ticket=" + MaskSensitive(ticket));
            if (mtx.try_lock())
            {
                if (!m_stop.load())
                {
                    mtx.unlock();
                    return;
                }
                const ScanQRCodeResult scanResult = ScanQRLoginDetailed(scanUrl.data(), ticket, gameType);
                bool scanSubmitted = scanResult.success;
                PassportQRCodePayload nextPassportPayload{};
                if (scanResult.hasPassportPayload())
                {
                    LogInfo("屏幕游戏二维码返回通行证二段确认，ticket=" + MaskSensitive(scanResult.passportTicket));
                    scanSubmitted = ScanPassportQRCode(scanResult.passportTicket, scanResult.passportTokenTypes, this->stoken, this->mid);
                    if (scanSubmitted)
                    {
                        nextPassportPayload.ticket = scanResult.passportTicket;
                        nextPassportPayload.tokenTypes = scanResult.passportTokenTypes;
                    }
                }
                if (scanSubmitted)
                {
                    lastTicket = ticket;
                    passportQrPayload = std::move(nextPassportPayload);
                    nlohmann::json config = nlohmann::json::parse(m_config->getConfig());
                    if (config["auto_login"])
                    {
                        LogInfo("已开启自动登录，继续确认屏幕二维码登录，gameType=" + ToString(gameType));
                        continueLastLogin();
                    }
                    else
                    {
                        LogInfo("等待用户确认屏幕二维码登录，gameType=" + ToString(gameType));
                        emit loginConfirm(gameType, true);
                    }
                }
                else
                {
                    LogWarning("屏幕二维码扫码失败，gameType=" + ToString(gameType) +
                               ", ticket=" + MaskSensitive(ticket));
                    emit loginResults(ScanRet::FAILURE_1);
                }
                stop();
                mtx.unlock();
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAYED));
        screenshotdxgi.doneWithFrame();
    }
    delete[] mBuffer;
    LogInfo("屏幕官服扫码循环结束");
}

void QRCodeForScreen::LoginBH3BiliBili()
{
    LogInfo("屏幕崩坏3 B服扫码循环开始");
    QThreadPool threadPool;
    threadPool.setMaxThreadCount(threadNumber);
    std::mutex mtx;
    ScreenShotDXGI screenshotdxgi;
    int w{ 0 };
    int h{ 0 };
    screenshotdxgi.InitDevice();
    screenshotdxgi.InitDupl(0, w, h);
    LogInfo("屏幕采集初始化完成，width=" + std::to_string(w) +
            ", height=" + std::to_string(h));
    long mBufferSize = w * h * 4;
    uint8_t* mBuffer = new UCHAR[mBufferSize];
    while (m_stop.load())
    {
        screenshotdxgi.getFrame(100);
        screenshotdxgi.copyFrameToBuffer(&mBuffer, mBufferSize);
        cv::Mat img;
        cv::resize(cv::Mat(h, w, CV_8UC4, mBuffer), img, { 1280, 720 });
#ifndef SHOW
        cv::imshow("Video_Stream", img);
        cv::waitKey(1);
#endif
        threadPool.tryStart([&, img = std::move(img)]() {
            thread_local QRScanner qrScanners;
            std::string str;
            qrScanners.decodeSingle(img, str);
            if (str.size() < 85)
            {
                return;
            }
            if (std::string_view view(str.c_str() + 79, 3); view != "8F3")
            {
                return;
            }
            const std::string& ticket = str.substr(str.length() - 24);
            if (lastTicket == ticket)
            {
                return;
            }
            LogInfo("屏幕识别到崩坏3 B服二维码，ticket=" + MaskSensitive(ticket));
            if (mtx.try_lock())
            {
                if (!m_stop.load())
                {
                    mtx.unlock();
                    return;
                }
                if (ret = scanCheck(ticket, scanUrl); ret == ScanRet::SUCCESS)
                {
                    lastTicket = ticket;
                    nlohmann::json config = nlohmann::json::parse(m_config->getConfig());
                    if (config["auto_login"])
                    {
                        LogInfo("已开启自动登录，继续确认屏幕崩坏3 B服二维码登录");
                        continueLastLogin();
                    }
                    else
                    {
                        LogInfo("等待用户确认屏幕崩坏3 B服二维码登录");
                        emit loginConfirm(GameType::Honkai3_BiliBili, true);
                    }
                }
                else
                {
                    LogWarning("屏幕崩坏3 B服二维码扫码失败，ticket=" + MaskSensitive(ticket) +
                               ", result=" + ToString(ret));
                    emit loginResults(ScanRet::FAILURE_1);
                }
                stop();
                mtx.unlock();
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAYED));
        screenshotdxgi.doneWithFrame();
    }
    delete[] mBuffer;
    LogInfo("屏幕崩坏3 B服扫码循环结束");
}

void QRCodeForScreen::continueLastLogin()
{
    LogInfo("继续确认屏幕二维码登录，serverType=" + ToString(servertype) +
            ", gameType=" + ToString(gameType) +
            ", ticket=" + MaskSensitive(lastTicket));
    switch (servertype)
    {
        using enum ServerType;
    case Official:
    {
        bool b = false;
        if (passportQrPayload.valid())
        {
            b = ConfirmPassportQRCode(passportQrPayload.ticket, passportQrPayload.tokenTypes, this->stoken, this->mid);
        }
        else
        {
            std::string confirmGameToken = gameToken;
            if (confirmGameToken.empty())
            {
                auto [code, fetchedGameToken] = GetGameTokenByStoken(this->stoken, this->mid);
                if (code != 0)
                {
                    LogWarning("屏幕官服二维码登录确认获取 game_token 失败，uid=" + MaskSensitive(uid) +
                               ", code=" + std::to_string(code));
                    Q_EMIT loginResults(ScanRet::FAILURE_2);
                    return;
                }
                confirmGameToken = std::move(fetchedGameToken);
            }
            b = ConfirmQRLogin(confirmUrl, uid, confirmGameToken, lastTicket, gameType);
        }
        if (b)
        {
            LogInfo("屏幕官服二维码登录确认成功，gameType=" + ToString(gameType));
            Q_EMIT loginResults(ScanRet::SUCCESS);
        }
        else
        {
            LogWarning("屏幕官服二维码登录确认失败，gameType=" + ToString(gameType));
            Q_EMIT loginResults(ScanRet::FAILURE_2);
        }
    }
    break;
    case BH3_BiliBili:
    {
        ret = scanConfirm(lastTicket, uid, gameToken, m_name, confirmUrl);
        LogInfo("屏幕崩坏3 B服二维码登录确认完成，result=" + ToString(ret));
        Q_EMIT loginResults(ret);
    }
    break;
    default:
        break;
    }
}

void QRCodeForScreen::run()
{
    ret = ScanRet::UNKNOW;
    m_stop.store(true);
    LogInfo("屏幕扫码线程启动，serverType=" + ToString(servertype));
#ifndef SHOW
    cv::namedWindow("Video_Stream", cv::WINDOW_AUTOSIZE);
#endif
    switch (servertype)
    {
    case ServerType::Official:
        LoginOfficial();
        break;
    case ServerType::BH3_BiliBili:
        LoginBH3BiliBili();
        break;
    default:
        LogWarning("屏幕扫码线程遇到未知 serverType=" + ToString(servertype));
        break;
    }
#ifndef SHOW
    cv::destroyWindow("Video_Stream");
#endif
    LogInfo("屏幕扫码线程结束，result=" + ToString(ret));
}

void QRCodeForScreen::stop()
{
    m_stop.store(false);
}

void QRCodeForScreen::setServerType(const ServerType servertype)
{
    this->servertype = servertype;
}
