#pragma once

#include <string>
#include <map>
#include <list>
#include <mutex>

class RtspSession;

//具体推流类
class MediaPusher {
public:
	MediaPusher(const std::string& szUrl, RtspSession* pRtspSession);
	~MediaPusher();
	//添加一个接收端
	void AddRecvier(RtspSession* pRtspSession);
	//删除一个接收端
	void DelRecvier(RtspSession* pRtspSession);
	//发送数据给所有接收端
	bool SendDataToRecvier(const char* pData, size_t uSize);
	//提供给拉流实例，用于获取推流对应的tcp连接中存储的各项信息，比如sdp
	RtspSession* GetRtspSession() const { return m_pRtspSession; }
private:
	std::string m_szUrl;				//推流地址
	RtspSession* m_pRtspSession = nullptr; //推流对应的tcp连接	
	std::list<RtspSession*> m_listRtspSession;
};

//全局媒体推流管理类
class MediaPusherManager {
public:
	static MediaPusherManager& Instance() {
		static MediaPusherManager instance;
		return instance;
	}
	//添加一个推流地址，返回是否添加成功
	bool AddPusher(const std::string& szUrl, RtspSession* pRtspSession);
	//删除一个推流地址，返回是否删除成功
	bool DelPusher(const std::string& szUrl);
	//判断推流地址是否存在
	bool IsExist(const std::string& szUrl);
	//添加一个接收端，返回对应的tcp连接session，失败返回nullptr
	RtspSession* AddRecvier(const std::string& szUrl, RtspSession* pRtspSession);
	//删除一个接收端，返回是否删除成功
	bool DelRecvier(const std::string& szUrl, RtspSession* pRtspSession);
	//发送数据给所有接收端
	bool SendDataToRecvier(const std::string& szUrl, const char* pData, size_t uSize);
private:
	std::map<const std::string, MediaPusher*> m_mapMediaPusher;
	std::mutex m_mtxMediaPusher;
};