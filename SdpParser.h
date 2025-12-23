#pragma once

#include <string>
#include <vector>
#include <map>

//通道类型
enum class ETrackType {
    INVALID = -1,  //无效通道
    VIDEO = 0,     //视频通道
    AUDIO,         //音频通道
    TITLE,         //原始通道
    MIN = INVALID,
    MAX = TITLE,
};

namespace Rtsp {
    //RTSP的RTP类型
    enum class ERtpType {
        INVALID = -1,
        TCP = 0,
        UDP = 1,
        MULTICAST = 2,
    };
};

//一个通道的具体属性
struct SdpTrack {
    std::multimap<std::string, std::string> map_attr;  //通道属性
    ETrackType type = ETrackType::INVALID; //这个track的类型
    std::string szControl;//媒体流的控制属性。当存在属性control时可用
    uint8_t uInterleaved = 0;   //rtp的通道号
    bool isInited = false;   //该通道是否已经被setup过

    std::string GetControlUrl(const std::string& szUrl) const;
};

class SdpParser {
public:
    SdpParser(const std::string& szSdp);
    ~SdpParser();
    //获取第一个指定类型的通道
    SdpTrack* GetTrack(ETrackType type) const;
    //取第一个有效的视频通道和音频通道，最多两个元素
    std::vector<SdpTrack*> GetAvailableTrack() const;
    //获取sdp的解析原文
    const std::string GetSdp() const { return m_szSdp; }
    //解析参数为map
    static std::map<std::string, std::string> ParseArgs(const std::string& str, const char* pair_delim, const char* key_delim);

private:
    std::string m_szSdp;
    std::vector<SdpTrack*> m_vecTrack;
};