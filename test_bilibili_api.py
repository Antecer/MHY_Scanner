#!/usr/bin/env python3
"""
Test script to verify Bilibili live stream retrieval
"""
import requests
import json

# Test room ID from the link: https://live.bilibili.com/24548629
ROOM_ID = "24548629"
ROOM_INIT_URL = f"https://api.live.bilibili.com/room/v1/Room/room_init?id={ROOM_ID}"
PLAY_INFO_URL = "https://api.live.bilibili.com/xlive/web-room/v2/index/getRoomPlayInfo"

print("=" * 60)
print("Bilibili Live Stream Link Retrieval Test")
print("=" * 60)
print(f"Testing Room ID: {ROOM_ID}\n")

# Test 1: room_init API without User-Agent
print("[Test 1] room_init API without User-Agent")
print(f"URL: {ROOM_INIT_URL}")
try:
    response = requests.get(ROOM_INIT_URL, timeout=5)
    print(f"Status Code: {response.status_code}")
    if response.status_code == 200:
        data = response.json()
        code = data.get("code", -1)
        print(f"Response code: {code}")
        if code == 0:
            room_info = data.get("data", {})
            live_status = room_info.get("live_status", 0)
            real_room_id = room_info.get("room_id", ROOM_ID)
            print(f"Live Status: {live_status} (1=live, 0=offline)")
            print(f"Real Room ID: {real_room_id}")
        else:
            print(f"Error: {data.get('message', 'Unknown error')}")
    else:
        print(f"HTTP Error: {response.text[:200]}")
except Exception as e:
    print(f"Error: {e}\n")

print("\n" + "-" * 60)

# Test 2: room_init API with User-Agent
print("[Test 2] room_init API with User-Agent")
headers = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/92.0.4515.159 Safari/537.36"
}
try:
    response = requests.get(ROOM_INIT_URL, headers=headers, timeout=5)
    print(f"Status Code: {response.status_code}")
    if response.status_code == 200:
        data = response.json()
        code = data.get("code", -1)
        print(f"Response code: {code}")
        if code == 0:
            room_info = data.get("data", {})
            live_status = room_info.get("live_status", 0)
            real_room_id = room_info.get("room_id", ROOM_ID)
            print(f"Live Status: {live_status} (1=live, 0=offline)")
            print(f"Real Room ID: {real_room_id}")

            # If live, try to get stream link
            if live_status == 1:
                print("\n" + "-" * 60)
                print("[Test 3] getRoomPlayInfo API")
                params = {
                    "room_id": real_room_id,
                    "protocol": "0,1",
                    "format": "0,2",
                    "codec": "0",
                    "qn": "10000",
                    "only_audio": "0",
                    "only_video": "0"
                }
                try:
                    play_response = requests.get(PLAY_INFO_URL, params=params, headers=headers, timeout=5)
                    print(f"Status Code: {play_response.status_code}")
                    if play_response.status_code == 200:
                        play_data = play_response.json()
                        play_code = play_data.get("code", -1)
                        print(f"Response code: {play_code}")
                        if play_code == 0:
                            playurl = play_data.get("data", {}).get("playurl_info", {}).get("playurl", {})
                            if playurl:
                                stream = playurl.get("stream", [])
                                if stream:
                                    format_obj = stream[0].get("format", [])
                                    if format_obj:
                                        codec = format_obj[0].get("codec", [])
                                        if codec:
                                            base_url = codec[0].get("base_url", "")
                                            url_info = codec[0].get("url_info", [])
                                            if url_info:
                                                host = url_info[0].get("host", "")
                                                extra = url_info[0].get("extra", "")
                                                stream_url = host + base_url + extra
                                                print(f"Stream URL (first 100 chars): {stream_url[:100]}...")
                                                print(f"Stream URL length: {len(stream_url)}")
                                            else:
                                                print("No url_info found")
                                        else:
                                            print("No codec found")
                                    else:
                                        print("No format found")
                                else:
                                    print("No stream found")
                            else:
                                print("No playurl found in response")
                        else:
                            print(f"Error: {play_data.get('message', 'Unknown error')}")
                    else:
                        print(f"HTTP Error: {play_response.text[:200]}")
                except Exception as e:
                    print(f"Error: {e}")
        else:
            print(f"Error: {data.get('message', 'Unknown error')}")
    else:
        print(f"HTTP Error: {response.text[:200]}")
except Exception as e:
    print(f"Error: {e}")

print("\n" + "=" * 60)
