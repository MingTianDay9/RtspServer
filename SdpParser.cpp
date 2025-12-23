#include "SdpParser.h"
#include <algorithm>

using namespace std;

static int GetClockRate(int pt)
{
    switch (pt) {
    case 0:  return 8000;   // PCMU
    case 3:  return 8000;   // GSM
    case 4:  return 8000;   // G723
    case 5:  return 8000;   // DVI4_8000
    case 6:  return 16000;  // DVI4_16000
    case 7:  return 8000;   // LPC
    case 8:  return 8000;   // PCMA
    case 9:  return 16000;  // G722
    case 10: return 44100;  // L16_Stereo
    case 11: return 44100;  // L16_Mono
    case 12: return 8000;   // QCELP
    case 13: return 8000;   // CN
    case 14: return 44100;  // MP3
    case 15: return 8000;   // G728
    case 16: return 11025;  // DVI4_11025
    case 17: return 22050;  // DVI4_22050
    case 18: return 8000;   // G729
    case 25: return 90000;  // CelB (视频)
    case 26: return 90000;  // JPEG (视频)
    case 28: return 90000;  // nv (视频)
    case 31: return 90000;  // H261 (视频)
    case 32: return 90000;  // MPV (视频)
    case 33: return 90000;  // MP2T (视频)
    case 34: return 90000;  // H263 (视频)
    default: return 90000;  // 默认返回90000（视频默认采样率/时钟频率）
    }
}

static vector<string> split(const string& s, const char* delim) {
    vector<string> ret;
    size_t last = 0;
    auto index = s.find(delim, last);
    while (index != string::npos) {
        if (index - last > 0) {
            ret.push_back(s.substr(last, index - last));
        }
        last = index + strlen(delim);
        index = s.find(delim, last);
    }
    if (!s.size() || s.size() - last > 0) {
        ret.push_back(s.substr(last));
    }
    return ret;
}

//去除前后的空格、回车符、制表符
static string& trim(string& s, const string& chars = " \r\n\t") {
    string map(0xFF, '\0');
    for (auto& ch : chars) {
        map[(unsigned char&)ch] = '\1';
    }
    while (s.size() && map.at((unsigned char&)s.back())) s.pop_back();
    while (s.size() && map.at((unsigned char&)s.front())) s.erase(0, 1);
    return s;
}

static string findSubString(const char* buf, const char* start, const char* end, size_t buf_size = 0) {
    if (buf_size <= 0) {
        buf_size = strlen(buf);
    }
    auto msg_start = buf;
    auto msg_end = buf + buf_size;
    size_t len = 0;
    if (start != NULL) {
        len = strlen(start);
        msg_start = strstr(buf, start);
    }
    if (msg_start == NULL) {
        return "";
    }
    msg_start += len;
    if (end != NULL) {
        msg_end = strstr(msg_start, end);
        if (msg_end == NULL) {
            return "";
        }
    }
    return string(msg_start, msg_end);
}

static ETrackType toTrackType(const string& str) {
    if (str == "") {
        return ETrackType::TITLE;
    }

    if (str == "video") {
        return ETrackType::VIDEO;
    }

    if (str == "audio") {
        return ETrackType::AUDIO;
    }

    return ETrackType::INVALID;
}

SdpParser::SdpParser(const std::string& szSdp)
    :m_szSdp(szSdp)
{
    /*****
    v=0 #SDP 的版本号，目前固定为 0，是唯一的有效值。
    o=- 0 0 IN IP4 127.0.0.1 #o=<用户名:匿名> <会话ID:默认> <版本号:默认> <网络类型:互联网（IN）> <地址类型: IPv4> <地址:本地回环>
    s=No Name #	会话的名称
    c=IN IP4 192.168.114.114 #媒体流的传输地址
    t=0 0 #表示会话永久有效
    a=tool:libavformat 58.76.100 #生成这份 SDP 的工具是libavformat（FFmpeg 的核心格式处理库），版本号为 58.76.100
    m=video 0 RTP/AVP 96 #m=<媒体类型> <端口> <传输协议> <编码载荷类型:1-95 为标准载荷，96 + 为自定义>
    a=rtpmap:96 H264/90000 #关联载荷类型和具体编码格式
    a=fmtp:96 packetization-mode=1; sprop-parameter-sets=Z/QAMpGbKAeAET8TCAAAH0gAB1MAeMGMsA==,aOvjxEhE; profile-level-id=F40032
    a=control:streamid=0
    *****/

    SdpTrack* track = new SdpTrack;
    track->type = ETrackType::TITLE;
    m_vecTrack.emplace_back(track);

    auto lines = split(m_szSdp, "\n");
    for (auto& line : lines) {
        trim(line);
        if (line.size() < 2 || line[1] != '=') {
            continue;
        }
        char opt = line[0];
        string opt_val = line.substr(2);
        switch (opt) {
        case 'm': {
            track = new SdpTrack;
            m_vecTrack.emplace_back(track);
            int pt, port, port_count;
            char rtp[16] = { 0 }, type[16] = { 0 };
            if (4 == sscanf_s(opt_val.data(), " %15[^ ] %d %15[^ ] %d", type, (unsigned)_countof(type), &port, rtp, (unsigned)_countof(rtp), &pt) ||
                5 == sscanf_s(opt_val.data(), " %15[^ ] %d/%d %15[^ ] %d", type, (unsigned)_countof(type), &port, &port_count, rtp, (unsigned)_countof(rtp), &pt)) {
                track->type = toTrackType(type);
            }
            break;
        }
        case 'a': {
            string attr = findSubString(opt_val.data(), nullptr, ":");
            if (attr.empty()) {
                track->map_attr.emplace(opt_val, "");
            }
            else {
                track->map_attr.emplace(attr, findSubString(opt_val.data(), ":", nullptr));
            }
            break;
        }
        }
    }

    for (auto& track_ptr : m_vecTrack) {
        auto& track = *track_ptr;
        auto it = track.map_attr.find("control");
        if (it != track.map_attr.end()) {
            track.szControl = it->second;
        }
    }
}

SdpParser::~SdpParser()
{
    for (auto one : m_vecTrack)
        delete one;
    m_vecTrack.clear();
}


SdpTrack* SdpParser::GetTrack(ETrackType type) const
{
    for (auto& track : m_vecTrack) {
        if (track->type == type) {
            return track;
        }
    }
    return nullptr;
}

std::vector<SdpTrack*> SdpParser::GetAvailableTrack() const
{
    vector<SdpTrack*> ret;
    bool audio_added = false;
    bool video_added = false;
    for (auto& track : m_vecTrack) {
        if (track->type == ETrackType::AUDIO) {
            if (!audio_added) {
                ret.emplace_back(track);
                audio_added = true;
            }
            continue;
        }

        if (track->type == ETrackType::VIDEO) {
            if (!video_added) {
                ret.emplace_back(track);
                video_added = true;
            }
            continue;
        }
    }
    return ret;
}

std::map<std::string, std::string> SdpParser::ParseArgs(const string& str, const char* pair_delim, const char* key_delim)
{
    map<string, string> ret;
    auto arg_vec = split(str, pair_delim);
    for (auto& key_val : arg_vec) {
        if (key_val.empty()) {
            // 忽略
            continue;
        }
        auto pos = key_val.find(key_delim);
        if (pos != string::npos) {
            std::string key(key_val, 0, pos);
            std::string val(key_val.substr(pos + strlen(key_delim)));
            trim(key);
            trim(val);
            ret.emplace(std::move(key), std::move(val));
        }
        else {
            trim(key_val);
            if (!key_val.empty()) {
                ret.emplace(std::move(key_val), "");
            }
        }
    }
    return ret;
}

std::string SdpTrack::GetControlUrl(const std::string& szUrl) const
{
    if (szControl.find("://") != string::npos) {
        // 以rtsp://开头
        return szControl;
    }
    return szUrl + "/" + szControl;
}
