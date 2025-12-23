#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include "SdpParser.h"

#pragma once

class EventPoller;
class RtspServer;

/// <summary>
/// 对于发往RtspServer的每一个连接，都会对应一个RtspSession实例，用于保存该连接的状态信息
/// </summary>
class RtspSession : public std::enable_shared_from_this<RtspSession> {
public:
    RtspSession(std::weak_ptr<RtspServer> pServer, EventPoller* pEventPoller, int fd);
    virtual ~RtspSession();
    //附加事件监听从而启动该session的处理
    bool StartSession();
    //向服务器通知关闭该session
    void CloseSession();
    //发送数据给该session对应的fd
    bool SendData(const char* buffer, size_t uSize);
protected:
    //处理给定的套接字的读取请求，尽可能地获取数据后交给onRecv进一步处理
    size_t onRead(std::vector<char>& buffer);
    //接收并处理fd对应的数据
    void onRecv(char* buffer, size_t uSize);
private:
    //解析请求头
    bool parseHeader();
    //处理一次完整的请求
    void handleReq();
    //处理rtp包
    void handleRtpPacket();
private:
    // 处理options方法,获取服务器能力
    void handleReq_Options(const std::unordered_map<std::string, std::string>& parser);
    // 处理describe方法，请求服务器rtsp sdp信息 
    void handleReq_Describe(const std::unordered_map<std::string, std::string>& parser);
    // 处理ANNOUNCE方法，请求推流，附带sdp 
    void handleReq_ANNOUNCE(const std::unordered_map<std::string, std::string>& parser);
    // 处理record方法，开始推流 
    void handleReq_RECORD(const std::unordered_map<std::string, std::string>& parser);
    // 处理setup方法，播放和推流协商rtp传输方式用 
    void handleReq_SETUP(const std::unordered_map<std::string, std::string>& parser);
    // 处理play方法，开始或恢复播放 
    void handleReq_PLAY(const std::unordered_map<std::string, std::string>& parser);
    // 处理teardown方法，结束播放 
    void handleReq_TEARDOWN(const std::unordered_map<std::string, std::string>& parser);
    // 处理SET_PARAMETER、GET_PARAMETER方法，一般用于心跳保活
    void handleReq_SET_PARAMETER(const std::unordered_map<std::string, std::string>& parser);


    // 回复客户端
    bool sendRtspResponse(const std::string& res_code, std::multimap<std::string, std::string> header = {}, const std::string& sdp = "", const char* protocol = "RTSP/1.0");

    // 获取track下标 
    size_t getTrackIndexByControlUrl(const std::string& control_url);

private:
    std::weak_ptr<RtspServer> m_pServer;         //session所属的服务器
    EventPoller* m_pEventPoller = nullptr;      //监听事件的实例
    int m_fd = -1;	                            //连接所对应的套接字
    std::vector<char> m_vecRecv;    //接收缓冲
    std::vector<char> m_vecHeader;  //接收结果:请求头
    std::vector<char> m_vecContent; //接收结果:请求内容
    bool m_isHeader = true;         //当前是否仍在接收请求头
    int m_iContentLength = 0;		//附带的内容长度
    size_t m_uRtpPacketLength = 0;	//正在接收的rtp包长度
    std::unordered_map<std::string, std::string> m_mapHeader;	//请求头的map
    // 收到的seq，回复时一致
    int m_iCseq = 0;
    // Session号
    std::string m_szSessionId;
    // 解析sdp的实例
    SdpParser* m_pSdpParser = nullptr;
    // sdp里面有效的track,包含音频或视频(每样最多一个)
    std::vector<SdpTrack*> m_vecSdpTrack;
    //完整链接（含后面参数）
    std::string m_szFullUrl;
    //该链接是否是推流方
    bool m_isPusher = false;
    Rtsp::ERtpType m_eRtpType = Rtsp::ERtpType::INVALID;

    //////////////////拉流时的session使用变量
    RtspSession* m_pPusherRtspSession = nullptr;    //在拉取谁的推流
};