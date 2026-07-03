#include "QRCodeForStream.h"

#include <string>
#include <string_view>
#include <utility>

#include "AsyncLogger.h"
#include "QRScanner.h"
#include "MhyApi.hpp"

namespace
{
std::string AvErrorText(const int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) == 0)
    {
        return buffer;
    }
    return "unknown error " + std::to_string(errorCode);
}
}

QRCodeForStream::QRCodeForStream(QObject* parent) :
    QThread(parent),
    pAvdictionary(nullptr),
    pAVFormatContext(nullptr),
    pSwsContext(nullptr),
    pAVFrame(nullptr),
    pAVPacket(nullptr),
    pAVCodecContext(nullptr),
    m_stop(false),
    servertype(ServerType::Official)

{
    av_log_set_level(AV_LOG_FATAL);
    m_config = &(ConfigDate::getInstance());
}

QRCodeForStream::~QRCodeForStream()
{
    if (!this->isInterruptionRequested())
    {
        m_stop.store(false);
    }
    this->requestInterruption();
    this->wait();
}

void QRCodeForStream::setLoginInfo(const std::string_view uid, const std::string_view gameToken)
{
    this->uid = uid;
    this->gameToken = gameToken;
    this->stoken.clear();
    this->mid.clear();
    this->passportQrPayload = {};
}

void QRCodeForStream::setLoginInfo(const std::string_view uid, const std::string_view gameToken, const std::string_view stoken, const std::string_view mid)
{
    this->uid = uid;
    this->gameToken = gameToken;
    this->stoken = stoken;
    this->mid = mid;
    this->passportQrPayload = {};
}

void QRCodeForStream::setLoginInfo(const std::string_view uid, const std::string_view gameToken, const std::string& name)
{
    this->uid = uid;
    this->gameToken = gameToken;
    this->m_name = name;
    this->stoken.clear();
    this->mid.clear();
    this->passportQrPayload = {};
}

void QRCodeForStream::setServerType(const ServerType servertype)
{
    this->servertype = servertype;
}

void QRCodeForStream::LoginOfficial()
{
    LogInfo("直播流官服扫码循环开始");
    while (m_stop.load())
    {
        if (av_read_frame(pAVFormatContext, pAVPacket) < 0)
        {
            LogWarning("读取直播流帧失败或直播已中断");
            ret = ScanRet::LIVESTOP;
            break;
        }
        if (pAVPacket->stream_index != videoStreamIndex)
        {
            continue;
        }
        avcodec_send_packet(pAVCodecContext, pAVPacket);
        if (pAVFrame == nullptr)
        {
            LogError("直播流解码帧分配失败");
            std::cerr << "Error allocating frame" << std::endl;
            ret = ScanRet::LIVESTOP;
            break;
        }
        while (avcodec_receive_frame(pAVCodecContext, pAVFrame) == 0)
        {
            cv::Mat img(videoStreamHeight, videoStreamWidth, CV_8UC3);
            uint8_t* dstData[1] = { img.data };
            const int dstLinesize[1] = { static_cast<int>(img.step) };
            sws_scale(pSwsContext, pAVFrame->data, pAVFrame->linesize, 0, pAVFrame->height,
                      dstData, dstLinesize);
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
                const std::string_view ticket(str.data() + str.size() - 24, 24);
                setGameType[view]();
                if (lastTicket == ticket)
                {
                    return;
                }
                LogInfo("直播流识别到二维码，gameType=" + ToString(gameType) +
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
                        LogInfo("直播流游戏二维码返回通行证二段确认，ticket=" + MaskSensitive(scanResult.passportTicket));
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
                            LogInfo("已开启自动登录，继续确认直播流二维码登录，gameType=" + ToString(gameType));
                            continueLastLogin();
                        }
                        else
                        {
                            LogInfo("等待用户确认直播流二维码登录，gameType=" + ToString(gameType));
                            Q_EMIT loginConfirm(gameType, false);
                        }
                    }
                    else
                    {
                        LogWarning("直播流二维码扫码失败，gameType=" + ToString(gameType) +
                                   ", ticket=" + MaskSensitive(ticket));
                        Q_EMIT loginResults(ScanRet::FAILURE_1);
                    }
                    stop();
                    mtx.unlock();
                }
            });
        }
        av_frame_unref(pAVFrame);
        av_packet_unref(pAVPacket);
    }
}

void QRCodeForStream::LoginBH3BiliBili()
{
    LogInfo("直播流崩坏3 B服扫码循环开始");
    while (m_stop.load())
    {
        if (av_read_frame(pAVFormatContext, pAVPacket) < 0)
        {
            LogWarning("读取直播流帧失败或直播已中断");
            ret = ScanRet::LIVESTOP;
            break;
        }
        if (pAVPacket->stream_index != videoStreamIndex)
        {
            continue;
        }
        avcodec_send_packet(pAVCodecContext, pAVPacket);
        if (pAVFrame == nullptr)
        {
            LogError("直播流解码帧分配失败");
            std::cerr << "Error allocating frame" << std::endl;
            ret = ScanRet::LIVESTOP;
            break;
        }

        while (avcodec_receive_frame(pAVCodecContext, pAVFrame) == 0)
        {
            cv::Mat img(videoStreamHeight, videoStreamWidth, CV_8UC3);
            uint8_t* dstData[1] = { img.data };
            const int dstLinesize[1] = { static_cast<int>(img.step) };
            sws_scale(pSwsContext, pAVFrame->data, pAVFrame->linesize, 0, pAVFrame->height,
                      dstData, dstLinesize);
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
                LogInfo("直播流识别到崩坏3 B服二维码，ticket=" + MaskSensitive(ticket));
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
                            LogInfo("已开启自动登录，继续确认直播流崩坏3 B服二维码登录");
                            continueLastLogin();
                        }
                        else
                        {
                            LogInfo("等待用户确认直播流崩坏3 B服二维码登录");
                            Q_EMIT loginConfirm(GameType::Honkai3_BiliBili, false);
                        }
                    }
                    else
                    {
                        LogWarning("直播流崩坏3 B服二维码扫码失败，ticket=" + MaskSensitive(ticket) +
                                   ", result=" + ToString(ret));
                        Q_EMIT loginResults(ret);
                    }
                    stop();
                    mtx.unlock();
                }
            });
        }
        av_frame_unref(pAVFrame);
        av_packet_unref(pAVPacket);
    }
}

void QRCodeForStream::setStreamHW()
{
    if (pAVCodecContext->width < pAVCodecContext->height ||
        pAVCodecContext->height == 480 ||
        pAVCodecContext->height == 720)
    {
        videoStreamWidth = pAVCodecContext->width;
        videoStreamHeight = pAVCodecContext->height;
    }
    else
    {
        videoStreamWidth = pAVCodecContext->width / 1.5;
        videoStreamHeight = pAVCodecContext->height / 1.5;
    }
}

void QRCodeForStream::stop()
{
    m_stop.store(false);
}

void QRCodeForStream::setUrl(const std::string& url, const std::map<std::string, std::string> heard)
{
    streamUrl = url;
    av_dict_free(&pAvdictionary);
    LogInfo("设置直播流地址，linkLength=" + std::to_string(streamUrl.length()) +
            ", headerCount=" + std::to_string(heard.size()));
    for (const auto& it : heard)
    {
        av_dict_set(&pAvdictionary, it.first.c_str(), it.second.c_str(), 0);
    }
    av_dict_set(&pAvdictionary, "max_delay", "0", 0);
    av_dict_set(&pAvdictionary, "probesize", "1024", 0);
    av_dict_set(&pAvdictionary, "analyzeduration", "0", 0);
    av_dict_set(&pAvdictionary, "fflags", "nobuffer", 0);
    av_dict_set(&pAvdictionary, "flags", "low_delay", 0);
    av_dict_set(&pAvdictionary, "rw_timeout", "10000000", 0);
}

auto QRCodeForStream::init() -> bool
{
    LogInfo("开始初始化直播流，linkLength=" + std::to_string(streamUrl.length()));
    avformat_close_input(&pAVFormatContext);
    avcodec_free_context(&pAVCodecContext);
    sws_freeContext(pSwsContext);
    av_frame_free(&pAVFrame);
    av_packet_free(&pAVPacket);
    pSwsContext = nullptr;
    pAVFormatContext = avformat_alloc_context();
    const int openRet = avformat_open_input(&pAVFormatContext, streamUrl.c_str(), NULL, &pAvdictionary);
    if (openRet < 0)
    {
        LogError("打开直播流失败，error=" + AvErrorText(openRet));
        std::cerr << "Error opening input file" << std::endl;
        return false;
    }
    const int streamInfoRet = avformat_find_stream_info(pAVFormatContext, NULL);
    if (streamInfoRet < 0)
    {
        LogError("读取直播流信息失败，error=" + AvErrorText(streamInfoRet));
        std::cerr << "Error finding stream information" << std::endl;
        return false;
    }
    AVStream* videoStream = nullptr;
    for (int i = 0; i < pAVFormatContext->nb_streams; i++)
    {
        if (pAVFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = pAVFormatContext->streams[i];
            break;
        }
    }
    if (videoStream == nullptr)
    {
        LogError("直播流中未找到视频流");
        std::cerr << "No video stream found" << std::endl;
        return false;
    }
    videoStreamIndex = videoStream->index;
    const AVCodec* decoder{ avcodec_find_decoder(videoStream->codecpar->codec_id) };
    if (decoder == nullptr)
    {
        LogError("直播流视频解码器未找到");
        std::cerr << "Codec not found" << std::endl;
        return false;
    }
    pAVCodecContext = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(pAVCodecContext, videoStream->codecpar);
    if (avcodec_open2(pAVCodecContext, decoder, NULL) < 0)
    {
        LogError("打开直播流视频解码器失败");
        std::cerr << "Error opening codec" << std::endl;
        return false;
    }
    setStreamHW();
    LogInfo("直播流初始化成功，sourceWidth=" + std::to_string(pAVCodecContext->width) +
            ", sourceHeight=" + std::to_string(pAVCodecContext->height) +
            ", scanWidth=" + std::to_string(videoStreamWidth) +
            ", scanHeight=" + std::to_string(videoStreamHeight));
    pSwsContext = sws_getContext(
        pAVCodecContext->width, pAVCodecContext->height, pAVCodecContext->pix_fmt,
        videoStreamWidth, videoStreamHeight, AV_PIX_FMT_BGR24, SWS_BILINEAR, NULL, NULL, NULL);
    pAVPacket = av_packet_alloc();
    pAVFrame = av_frame_alloc();
    return true;
}

void QRCodeForStream::continueLastLogin()
{
    LogInfo("继续确认直播流二维码登录，serverType=" + ToString(servertype) +
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
                    LogWarning("直播流官服二维码登录确认获取 game_token 失败，uid=" + MaskSensitive(uid) +
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
            LogInfo("直播流官服二维码登录确认成功，gameType=" + ToString(gameType));
            Q_EMIT loginResults(ScanRet::SUCCESS);
        }
        else
        {
            LogWarning("直播流官服二维码登录确认失败，gameType=" + ToString(gameType));
            Q_EMIT loginResults(ScanRet::FAILURE_2);
        }
    }
    break;
    case BH3_BiliBili:
    {
        ret = scanConfirm(lastTicket, uid, gameToken, m_name, confirmUrl);
        LogInfo("直播流崩坏3 B服二维码登录确认完成，result=" + ToString(ret));
        Q_EMIT loginResults(ret);
    }
    break;
    default:
        break;
    }
}

void QRCodeForStream::run()
{
    threadPool.setMaxThreadCount(threadNumber);
    m_stop.store(true);
    ret = ScanRet::UNKNOW;
    LogInfo("直播流扫码线程启动，serverType=" + ToString(servertype));
    //TODO 获取直播流地址放在这里
    if (init())
    {
#ifndef SHOW
        cv::namedWindow("Video_Stream", cv::WINDOW_AUTOSIZE);
        cv::resizeWindow("Video_Stream", videoStreamWidth / 2, videoStreamHeight / 2);
#endif
        switch (servertype)
        {
            using enum ServerType;
        case Official:
            LoginOfficial();
            break;
        case BH3_BiliBili:
            LoginBH3BiliBili();
            break;
        default:
            LogWarning("直播流扫码线程遇到未知 serverType=" + ToString(servertype));
            break;
        }
    }
    else
    {
        ret = ScanRet::STREAMERROR;
        LogError("直播流初始化失败，扫码线程结束");
    }
    if (ret == ScanRet::LIVESTOP)
    {
        emit loginResults(ret);
    }
    else if (ret == ScanRet::STREAMERROR)
    {
        emit loginResults(ret);
    }
#ifndef SHOW
    cv::destroyWindow("Video_Stream");
#endif
    avformat_close_input(&pAVFormatContext);
    avcodec_free_context(&pAVCodecContext);
    sws_freeContext(pSwsContext);
    av_dict_free(&pAvdictionary);
    av_frame_free(&pAVFrame);
    av_packet_free(&pAVPacket);
    pAVFormatContext = nullptr;
    pAVCodecContext = nullptr;
    pSwsContext = nullptr;
    pAvdictionary = nullptr;
    pAVFrame = nullptr;
    pAVPacket = nullptr;
    LogInfo("直播流扫码线程结束，result=" + ToString(ret));
}
