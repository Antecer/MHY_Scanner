#include <iostream>
#include <string>
#include "src/Core/LiveStreamLink.h"

int main()
{
    std::cout << "Testing Live Stream Link Retrieval\n";
    std::cout << "==================================\n\n";

    // Test room ID from the link provided by user
    // https://live.bilibili.com/24548629
    std::string room_id = "24548629";

    std::cout << "Room ID: " << room_id << "\n\n";

    // Test BiliBili
    std::cout << "Testing BiliBili Platform...\n";
    auto info = GetLiveInfo(LivePlatform::BiliBili, room_id);

    std::cout << "Status: ";
    switch (info.status)
    {
    case LiveStreamStatus::Normal:
        std::cout << "NORMAL (Live streaming)\n";
        break;
    case LiveStreamStatus::Absent:
        std::cout << "ABSENT (Room not exist)\n";
        break;
    case LiveStreamStatus::NotLive:
        std::cout << "NOT_LIVE (Not broadcasting)\n";
        break;
    case LiveStreamStatus::Error:
        std::cout << "ERROR (Failed to get info)\n";
        break;
    }

    if (info.status == LiveStreamStatus::Normal)
    {
        std::cout << "Stream Link (first 100 chars):\n";
        std::cout << info.link.substr(0, 100) << "...\n";
        std::cout << "\nFull link length: " << info.link.length() << " chars\n";
    }

    return 0;
}
