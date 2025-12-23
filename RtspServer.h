#pragma once
#include <memory>
#include <mutex>
#include <unordered_map>

class EventPoller;
class RtspSession;

class RtspServer :  public std::enable_shared_from_this<RtspServer> {
public:
    explicit RtspServer(EventPoller* pEventPoller);
    /// <summary>
    /// 启动服务器
    /// </summary>
    /// <param name="szHost">主机</param>
    /// <param name="uPort">端口</param>
    /// <returns></returns>
    bool Start(const std::string& szHost, uint16_t uPort);
    //停止服务器运行
    void Stop();
    //关闭指定的session连接
    void CloseSession(int fd);

private:
    EventPoller* m_pEventPoller = nullptr;    //监听事件的实例
    std::string m_szHost;   //服务器监听ip
    uint16_t m_uPort = 0;   //服务器端口
    int m_fd = -1;          //服务器套接字
    std::unordered_map<int, std::shared_ptr<RtspSession>> m_mapSession; //保存所有的连接session
    std::mutex m_mtxSession; //保护session容器的互斥量
};