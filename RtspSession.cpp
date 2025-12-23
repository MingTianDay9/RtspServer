#include "RtspSession.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#include <string>
#include <map>
#include <vector>
#include <random>
#include <iomanip>
#include <unordered_map>
#include <sstream>

#include "EventPoller.h"
#include "MediaPusherManager.h"
#include "RtspServer.h"

using namespace std;

std::string makeRandStr(size_t uLen)
{
	static constexpr char CCH[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static constexpr size_t CCH_LEN = sizeof(CCH) - 1; // 预计算长度，避免重复计算
	string ret;
	ret.resize(uLen);
	thread_local std::mt19937 rng(std::random_device{}());
	for (size_t i = 0; i < uLen; ++i) {
		ret[i] = CCH[rng() % (sizeof(CCH) - 1)];
	}
	return ret;
}

//获取当前时间的字符串
static string dateStr() {
	char buf[64];
	time_t tt = time(NULL);
	tm t;
	gmtime_s(&t, &tt);
	strftime(buf, sizeof buf, "%a, %b %d %Y %H:%M:%S GMT", &t);
	return buf;
}

RtspSession::RtspSession(std::weak_ptr<RtspServer> pServer, EventPoller* pEventPoller, int fd)
	:m_pServer(pServer)
	, m_pEventPoller(pEventPoller)
	, m_fd(fd)
{
	unsigned long ul = 1; //设置为非阻塞模式
	ioctlsocket(fd, FIONBIO, &ul);
	int opt = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, static_cast<socklen_t>(sizeof(opt)));
	int size = 262144;
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size));
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&size, sizeof(size));
	linger m_sLinger;
	//在调用closesocket()时还有数据未发送完，允许等待
	// 若m_sLinger.l_onoff=0;则调用closesocket()后强制关闭 
	m_sLinger.l_onoff = false;
	m_sLinger.l_linger = 0; //设置等待时间为x秒
	setsockopt(fd, SOL_SOCKET, SO_LINGER, (char*)&m_sLinger, sizeof(linger));
}

RtspSession::~RtspSession()
{
	if (m_isPusher)MediaPusherManager::Instance().DelPusher(m_szFullUrl);
	else MediaPusherManager::Instance().DelRecvier(m_szFullUrl, this);
}

bool RtspSession::StartSession()
{
	//给客户端套接字接入监听
	std::weak_ptr<RtspSession> weak_self = shared_from_this();
	bool isRet = m_pEventPoller->AddEvent(m_fd, EventPoller::Event_Read | EventPoller::Event_Error
		, [weak_self](int event) {
			auto strong_self = weak_self.lock();
			if (!strong_self) {
				//来到这里，意味着客户端本身已经析构，这次回调只是队列残留，忽略即可
				return;
			}
			if (event & EventPoller::Event_Read) {
				strong_self->onRead(strong_self->m_pEventPoller->GetSharedBuffer());
			}
			if (event & EventPoller::Event_Error) {
				strong_self->CloseSession();
			}
		});
	return isRet;
}

void RtspSession::CloseSession()
{
	m_pEventPoller->DelEvent(m_fd);
	auto pServer = m_pServer.lock();
	if (nullptr == pServer)
		return;
	//通知服务器把自己关了
	pServer->CloseSession(m_fd);
}

bool RtspSession::SendData(const char* buffer, size_t uSize)
{
	const int iSendRet = ::send(m_fd, buffer, uSize, 0);
	return iSendRet > 0;
}

size_t RtspSession::onRead(std::vector<char>& buffer)
{
	size_t uReadSizeAll = 0, uReadSize = 0, count = 0;
	while (true) {
		uReadSize = (size_t)recv(m_fd, buffer.data(), buffer.size() - 1, 0);
		if (uReadSize == 0) {
			//来到这里意味着套接字已被对方关闭，正常关闭回收就行
			CloseSession();
			return uReadSizeAll;
		}
		if (uReadSize == -1) {
			int iRetWSA = WSAGetLastError();
			if (iRetWSA != WSAEWOULDBLOCK) {
				CloseSession();
			}
			return uReadSizeAll;
		}
		buffer[uReadSize] = '\0';
		uReadSizeAll += uReadSize;
		//交给子类实现具体处理这些数据
		onRecv(buffer.data(), uReadSize);
	}
}

void RtspSession::onRecv(char* buffer, size_t uSize)
{
	size_t uIndex = 0;


	//进入后续处理前，至少有4个缓存字节用于判断
	if (m_vecRecv.size() < 4) {
		size_t uLen = min(4 - m_vecRecv.size(), uSize);
		m_vecRecv.insert(m_vecRecv.end(), buffer, buffer + uLen);
		if (m_vecRecv.size() < 4)
			return;//给定的数据不足，则记录已有的后就直接返回
		uSize -= uLen;
		buffer += uLen;
	}

	//检查是否是要处理rtp包
	if (m_vecRecv[0] == '$') {
		char* data = m_vecRecv.data();
		size_t uDataLen = m_vecRecv.size();
		//确定rtp包的接收大小
		if (0 == m_uRtpPacketLength) {
			m_uRtpPacketLength = ((((uint8_t*)data)[2] << 8) | ((uint8_t*)data)[3]) + 4;
		}

		size_t uLen = min(m_uRtpPacketLength - uDataLen, uSize);
		m_vecRecv.insert(m_vecRecv.end(), buffer, buffer + uLen);
		uSize -= uLen;
		buffer += uLen;

		//是否已经足够一个rtp包可以开始处理了
		if (m_vecRecv.size() == m_uRtpPacketLength) {
			handleRtpPacket();
			if (uSize > 0)//把剩下的数据也处理掉
				onRecv(buffer, uSize);
		}
		return;
	}

	//处理请求，而非rtp包
	if (m_isHeader) {
		while (uIndex < uSize) {
			m_vecRecv.push_back(buffer[uIndex]);
			if (buffer[uIndex] == '\n') {
				//检查是否以rnrn结尾
				const size_t vec_size = m_vecRecv.size();
				if (m_vecRecv[vec_size - 1] == '\n'
					&& m_vecRecv[vec_size - 2] == '\r'
					&& m_vecRecv[vec_size - 3] == '\n'
					&& m_vecRecv[vec_size - 4] == '\r') {
					m_vecRecv.push_back('\0');
					m_vecHeader.swap(m_vecRecv);
					m_vecRecv.clear();
					++uIndex;
					//解析请求头
					if (parseHeader()) {
						//如果有附带内容，则继续解析
						if (m_iContentLength > 0) {
							m_isHeader = false;
						}
						else {//否则直接使用该请求头进行请求处理
							handleReq();
						}
					}
					else {
						m_vecHeader.clear();
					}
					break;//请求头获取结束
				}
			}
			++uIndex;
		}
	}
	if (false == m_isHeader) {
		while (uIndex < uSize) {
			m_vecRecv.push_back(buffer[uIndex]);
			//如果已经接收到了足够的内容
			if (m_vecRecv.size() == m_iContentLength) {
				m_vecRecv.push_back('\0');
				m_vecContent.swap(m_vecRecv);
				m_vecRecv.clear();
				m_mapHeader["reqContent"] = m_vecContent.data();
				m_isHeader = true;
				++uIndex;
				handleReq();
				break;//请求体获取结束
			}
			++uIndex;
		}
	}
	//如果发现还有数据则进行递归处理
	if (uSize - uIndex != 0)
		onRecv(buffer + uIndex, uSize - uIndex);
}

bool RtspSession::parseHeader()
{
	/*
	OPTIONS rtsp://192.168.114.114:554/live/test RTSP/1.0
	CSeq: 1
	User-Agent: Lavf58.76.100
	*/
	m_mapHeader.clear();
	std::istringstream stream(m_vecHeader.data());
	stream >> m_mapHeader["reqMethod"];
	stream >> m_mapHeader["reqUrl"];
	stream >> m_mapHeader["reqProtocol"];
	std::string szUrl = m_mapHeader.at("reqUrl");
	size_t uSplitIndex = szUrl.find('?');
	if (std::string::npos != uSplitIndex) {
		m_mapHeader["reqUrl_Short"] = szUrl.substr(0, uSplitIndex);
		m_mapHeader["reqUrl_Args"] = szUrl.substr(uSplitIndex + 1);
	}
	else {
		m_mapHeader["reqUrl_Short"] = szUrl;
	}
#define RM_R(STR) STR = STR.substr(0, STR.size() - 1)
	std::string line;
	while (std::getline(stream, line)) {
		size_t pos = line.find(':');
		if (pos != std::string::npos) {
			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);
			// 去掉首尾空格
			key.erase(0, key.find_first_not_of(" \t"));
			key.erase(key.find_last_not_of(" \t") + 1);
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t") + 1);
			RM_R(value);
			m_mapHeader[key] = value;
		}
	}
#undef RM_R
	if (0 == m_mapHeader.size()) {
		return false;
	}
	m_iContentLength = atoi(m_mapHeader["Content-Length"].c_str());
	return true;
}

void RtspSession::handleReq()
{
	auto method = m_mapHeader["reqMethod"];
	m_iCseq = atoi(m_mapHeader["CSeq"].data());
	if (m_szFullUrl.empty()) {
		m_szFullUrl = m_mapHeader.at("reqUrl");
	}
	using rtsp_request_handler = void (RtspSession::*)(const std::unordered_map<std::string, std::string>& parser);
	static unordered_map<string, rtsp_request_handler> s_cmd_functions{
		{"OPTIONS", &RtspSession::handleReq_Options},//拉流推流都会用到
		{"DESCRIBE", &RtspSession::handleReq_Describe},//拉流会用到
		{"ANNOUNCE", &RtspSession::handleReq_ANNOUNCE},//推流会用到
		{"RECORD", &RtspSession::handleReq_RECORD},//推流会用到
		{"SETUP", &RtspSession::handleReq_SETUP},//拉流推流都会用到
		{"PLAY", &RtspSession::handleReq_PLAY},
		{"TEARDOWN", &RtspSession::handleReq_TEARDOWN},//拉流推流都会用到
		{"GET_PARAMETER", &RtspSession::handleReq_SET_PARAMETER},//保活
	};
	auto it = s_cmd_functions.find(method);
	if (it == s_cmd_functions.end()) {
		sendRtspResponse("403 Forbidden");
	}
	else {
		(this->*(it->second))(m_mapHeader);
	}
	m_vecHeader.clear();
	m_vecContent.clear();
	m_iContentLength = 0;
}

void RtspSession::handleRtpPacket()
{
	////("{}字节rtp包到达", m_vecRecv.size());
	char* data = m_vecRecv.data();
	size_t uDataLen = m_vecRecv.size();

	//直接转发
	MediaPusherManager::Instance().SendDataToRecvier(m_szFullUrl, data, uDataLen);

	m_vecRecv.clear();
	m_uRtpPacketLength = 0;
}

void RtspSession::handleReq_Options(const std::unordered_map<std::string, std::string>& parser) {
	/*
	21444][2025-12-12 10:38:06.986][13696][RtspSession.cpp:21, onRecv][I]   OPTIONS rtsp://192.168.114.114:554/live/test RTSP/1.0
	CSeq: 1
	User-Agent: Lavf58.76.100
	*/
	//支持这些命令
	std::multimap<std::string, std::string> header;
	header.emplace("Public", "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, ANNOUNCE, RECORD, SET_PARAMETER, GET_PARAMETER");
	sendRtspResponse("200 OK", header);
}

void RtspSession::handleReq_Describe(const std::unordered_map<std::string, std::string>& parser) {
	m_pPusherRtspSession = dynamic_cast<RtspSession*>(MediaPusherManager::Instance().AddRecvier(m_szFullUrl, this));
	if (nullptr == m_pPusherRtspSession) {
		static constexpr auto err = "该流在服务器上不存在";
		std::multimap<std::string, std::string> header;
		header.emplace("Content-Type", "text/plain");
		sendRtspResponse("406 Not Acceptable", header, err);
		//("{}:{}", err, m_szFullUrl);
		return;
	}
	m_isPusher = false;
	//("观看流拉流成功:{}", m_szFullUrl);
	//找到了相应的rtsp流
	m_pSdpParser = new SdpParser(m_pPusherRtspSession->m_pSdpParser->GetSdp());
	m_vecSdpTrack = m_pSdpParser->GetAvailableTrack();
	if (m_vecSdpTrack.empty()) {
		//该流无效
		static constexpr auto err = "sdp中无有效track，该流无效";
		std::multimap<std::string, std::string> header;
		header.emplace("Content-Type", "text/plain");
		sendRtspResponse("406 Not Acceptable", header, err);
		//("{}:{}", err, m_szFullUrl);
		return;
	}
	m_szSessionId = makeRandStr(12);

	std::multimap<std::string, std::string> header;
	header.emplace("Content-Base", m_szFullUrl + "/");
	header.emplace("x-Accept-Retransmit", "our-retransmit");
	header.emplace("x-Accept-Dynamic-Rate", "1");
	sendRtspResponse("200 OK", header, m_pSdpParser->GetSdp());
}

void RtspSession::handleReq_ANNOUNCE(const std::unordered_map<std::string, std::string>& parser)
{
	/*
	[21444][2025-12-12 10:38:10.129][13696][RtspSession.cpp:21, onRecv][I]   ANNOUNCE rtsp://192.168.114.114:554/live/test RTSP/1.0
	Content-Type: application/sdp
	CSeq: 2
	User-Agent: Lavf58.76.100
	Content-Length: 296

	v=0
	o=- 0 0 IN IP4 127.0.0.1
	s=No Name
	c=IN IP4 192.168.114.114
	t=0 0
	a=tool:libavformat 58.76.100
	m=video 0 RTP/AVP 96
	a=rtpmap:96 H264/90000
	a=fmtp:96 packetization-mode=1; sprop-parameter-sets=Z/QAMpGbKAeAET8TCAAAH0gAB1MAeMGMsA==,aOvjxEhE; profile-level-id=F40032
	a=control:streamid=0
	*/

	m_szFullUrl = parser.at("reqUrl");
	if (false == MediaPusherManager::Instance().AddPusher(m_szFullUrl, this)) {
		static constexpr auto err = "该流在服务器上已存在";
		std::multimap<std::string, std::string> header;
		header.emplace("Content-Type", "text/plain");
		sendRtspResponse("406 Not Acceptable", header, err);
		//("{}:{}", err, m_szFullUrl);
		return;
	}
	m_isPusher = true;
	//("流添加成功:{}", m_szFullUrl);

	m_pSdpParser = new SdpParser(parser.at("reqContent"));
	m_szSessionId = makeRandStr(12);
	m_vecSdpTrack = m_pSdpParser->GetAvailableTrack();
	if (m_vecSdpTrack.empty()) {
		static constexpr auto err = "sdp中无有效track";
		std::multimap<std::string, std::string> header;
		header.emplace("Content-Type", "text/plain");
		sendRtspResponse("403 Forbidden", header, err);
		//("{}:{}", err, m_szFullUrl);
		return;
	}
	sendRtspResponse("200 OK");
}

void RtspSession::handleReq_RECORD(const std::unordered_map<std::string, std::string>& parser)
{
	if (m_vecSdpTrack.empty() || parser.at("Session") != m_szSessionId) {
		std::multimap<std::string, std::string> header;
		header.emplace("Connection", "Close");
		sendRtspResponse("454 Session Not Found", header);
		//("{}:{}", "454 Session Not Found", m_szFullUrl);
		return;
	}

	stringstream rtp_info;
	for (auto& track : m_vecSdpTrack) {
		if (track->isInited == false) {
			//还有track没有setup
			static constexpr auto err = "track not setuped";
			std::multimap<std::string, std::string> header;
			header.emplace("Content-Type", "text/plain");
			sendRtspResponse("403 Forbidden", header, err);
			//("{}:{}", err, m_szFullUrl);
			return;
		}
		rtp_info << "url=" << track->GetControlUrl(m_szFullUrl) << ",";
	}
	auto rtpStr = rtp_info.str();
	rtpStr.pop_back();
	std::multimap<std::string, std::string> header;
	header.emplace("RTP-Info", rtpStr);
	sendRtspResponse("200 OK", header);
}

void RtspSession::handleReq_SETUP(const std::unordered_map<std::string, std::string>& parser)
{
	/*
	SETUP rtsp://192.168.114.114:554/live/test/streamid=0 RTSP/1.0
	Transport: RTP/AVP/TCP;unicast;interleaved=0-1;mode=record
	CSeq: 3
	User-Agent: Lavf58.76.100
	Session: uMPQjlcMcI7J
	*/
	//处理setup命令，该函数可能进入多次m_szFullUrl

	int trackIdx = getTrackIndexByControlUrl(parser.at("reqUrl"));
	SdpTrack* trackRef = m_vecSdpTrack[trackIdx];
	if (trackRef->isInited) {
		//已经初始化过该Track
		static constexpr auto err = "不允许对同一个track进行两次setup";
		std::multimap<std::string, std::string> header;
		header.emplace("Content-Type", "text/plain");
		sendRtspResponse("403 Forbidden", header, err);
		//("{}:{}", err, m_mediaTuple.ShortUrl());
		return;
	}

	static auto getRtpTypeStr = [](const Rtsp::ERtpType type) {
		switch (type)
		{
		case Rtsp::ERtpType::TCP:
			return "TCP";
		case Rtsp::ERtpType::UDP:
			return "UDP";
		case Rtsp::ERtpType::MULTICAST:
			return "MULTICAST";
		default:
			return "Invalid";
		}
		};

	if (m_eRtpType == Rtsp::ERtpType::INVALID) {
		auto& strTransport = parser.at("Transport");
		auto rtpType = Rtsp::ERtpType::INVALID;
		if (strTransport.find("TCP") != string::npos) {
			rtpType = Rtsp::ERtpType::TCP;
		}
		else if (strTransport.find("multicast") != string::npos) {
			//rtpType = Rtsp::ERtpType::MULTICAST;
		}
		else {
			//rtpType = Rtsp::ERtpType::UDP;
		}
		if (Rtsp::ERtpType::INVALID == rtpType) {
			sendRtspResponse("461 Unsupported transport");
			return;
		}
		m_eRtpType = rtpType;
	}

	trackRef->isInited = true; //现在初始化

	{
		// rtsp推流时，interleaved由推流者决定
		auto key_values = SdpParser::ParseArgs(parser.at("Transport"), ";", "=");
		int interleaved_rtp = -1, interleaved_rtcp = -1;
		if (2 == sscanf_s(key_values["interleaved"].data(), "%d-%d", &interleaved_rtp, &interleaved_rtcp)) {
			trackRef->uInterleaved = interleaved_rtp;
		}
		else {
			static constexpr auto err = "can not find interleaved when setup of rtp over tcp";
			std::multimap<std::string, std::string> header;
			header.emplace("Content-Type", "text/plain");
			sendRtspResponse("403 Forbidden", header, err);
			//("{}:{}", err, m_mediaTuple.ShortUrl());
			return;
		}

		stringstream sdpResponse;
		sdpResponse << "RTP/AVP/TCP;unicast;"
			<< "interleaved=" << (int)trackRef->uInterleaved << "-"
			<< (int)trackRef->uInterleaved + 1 << ";"
			<< "ssrc=00000000";

		std::multimap<std::string, std::string> header;
		header.emplace("Transport", sdpResponse.str());
		header.emplace("x-Transport-Options", "late-tolerance=1.400000");
		header.emplace("x-Dynamic-Rate", "1");

		sendRtspResponse("200 OK", header);
	}
}

void RtspSession::handleReq_PLAY(const std::unordered_map<std::string, std::string>& parser) {
	if (m_vecSdpTrack.empty() || parser.at("Session") != m_szSessionId) {
		std::multimap<std::string, std::string> header;
		header.emplace("Connection", "Close");
		sendRtspResponse("454 Session Not Found", header);
		//("{}:{}", "454 Session Not Found", m_szFullUrl);
		return;
	}

	std::multimap<std::string, std::string> res_header;

	vector<ETrackType> inited_tracks;
	stringstream rtp_info;
	for (auto& track : m_vecSdpTrack) {
		if (track->isInited == false) {
			//为支持播放器播放单一track, 不校验没有发setup的track
			continue;
		}
		inited_tracks.emplace_back(track->type);

		rtp_info << "url=" << track->GetControlUrl(m_szFullUrl) << ";"
			<< "seq=0;"
			<< "rtptime=0" << ",";
	}

	auto rtpStr = rtp_info.str();
	rtpStr.pop_back();

	res_header.emplace("RTP-Info", rtpStr);
	//已存在Range时不覆盖
	stringstream szPlayRange;
	szPlayRange << "npt=" << setiosflags(ios::fixed) << setprecision(2) << 0;
	res_header.emplace("Range", szPlayRange.str());
	sendRtspResponse("200 OK", res_header);
}

void RtspSession::handleReq_TEARDOWN(const std::unordered_map<std::string, std::string>& parser)
{
	sendRtspResponse("200 OK");
	CloseSession();
}

void RtspSession::handleReq_SET_PARAMETER(const std::unordered_map<std::string, std::string>& parser)
{
	sendRtspResponse("200 OK");
}

bool RtspSession::sendRtspResponse(const std::string& res_code, std::multimap<std::string, std::string> header, const std::string& sdp, const char* protocol)
{
	header.emplace("CSeq", to_string(m_iCseq));
	if (!m_szSessionId.empty()) {
		header.emplace("Session", m_szSessionId);
	}

	header.emplace("Server", "服务器名字");
	header.emplace("Date", dateStr());

	if (!sdp.empty()) {
		header.emplace("Content-Length", to_string(sdp.size()));
		header.emplace("Content-Type", "application/sdp");
	}

	stringstream printer;
	printer << protocol << " " << res_code << "\r\n";
	for (auto& pr : header) {
		printer << pr.first << ": " << pr.second << "\r\n";
	}

	printer << "\r\n";

	if (!sdp.empty()) {
		printer << sdp;
	}
	const int iSendRet = ::send(m_fd, printer.str().data(), printer.str().size(), 0);
	return iSendRet > 0;
}

size_t RtspSession::getTrackIndexByControlUrl(const std::string& control_url)
{
	for (size_t i = 0; i < m_vecSdpTrack.size(); ++i) {
		if (control_url.find(m_vecSdpTrack[i]->GetControlUrl(m_szFullUrl)) == 0) {
			return i;
		}
	}
	if (m_vecSdpTrack.size() == 1) {
		return 0;
	}
	DebugBreak();
}

