#pragma once
#include "RtspServer.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment (lib, "Ws2_32.lib")
#pragma comment(lib,"Iphlpapi.lib")
#include <string>
#include <map>
#include <vector>
#include <thread>

#include "EventPoller.h"
#include "RtspSession.h"

using namespace std;

RtspServer::RtspServer(EventPoller* pEventPoller)
{
	m_pEventPoller = pEventPoller;
}

bool RtspServer::Start(const std::string& szHost, uint16_t uPort)
{
    m_szHost = szHost;
    m_uPort = uPort;

    m_fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    int opt = 1;
    //设置套接字属性，此处处理服务器快速重启后的以前使用的套接字还在TIME_WAIT的问题，配置后可以直接接着使用
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, static_cast<socklen_t>(sizeof(opt)));
    unsigned long ul = 1; //设置为非阻塞模式
    ioctlsocket(m_fd, FIONBIO, &ul);

    struct sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uPort);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(m_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        DebugBreak();
    }

    if (::listen(m_fd, 1024) == -1) {
        DebugBreak();
    }
    //weak_self的存在是为了防止发生回调时，本类的实例已经析构的情况
    //这是因为m_pEventPoller->DelEvent只是停止监听新事件，但队列里的事件仍然有可能在实例析构后仍触发该回调
    //此时必须忽略该回调，否则会访问野指针
    std::weak_ptr<RtspServer> weak_self = shared_from_this();
    //这个事件处理主要用于accpet新的套接字
    bool isRet = m_pEventPoller->AddEvent(m_fd, EventPoller::Event_Read | EventPoller::Event_Error, [weak_self](int event) {
        auto strong_self = weak_self.lock();
        if (nullptr == strong_self)
            return;
        const int fdServer = strong_self->m_fd;
        int fd;
        struct sockaddr_storage peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        //这个while主要是用来尝试一次获取多个accpet新连接
        while (true) {
            if (event & EventPoller::Event_Read) {
                fd = (int)accept(fdServer, (struct sockaddr*)&peer_addr, &addr_len);
                if (fd == -1) {// accept失败
                    int iRet = WSAGetLastError();
                    if (iRet == WSAEWOULDBLOCK) {
                        //没有新的连接accept进来了
                        return;
                    }
                    DebugBreak();
                    return;
                }
                auto session = std::make_shared<RtspSession>(strong_self, strong_self->m_pEventPoller, fd);
                if (session->StartSession()) {
                    strong_self->m_mtxSession.lock();
                    strong_self->m_mapSession[fd] = session;
                    strong_self->m_mtxSession.unlock();
                }
            }
            if (event & EventPoller::Event_Error) {
                return;
            }
        }
        });
    //没附加事件处理成功就直接停止服务器运行
    if (false == isRet)
        Stop();
    return isRet;
}

void RtspServer::Stop()
{
    if (-1 != m_fd) {
        m_pEventPoller->DelEvent(m_fd);
        closesocket(m_fd);
        m_fd = -1;
    }
}

void RtspServer::CloseSession(int fd)
{
    thread task([this,fd]() {
        m_mtxSession.lock();
        m_mapSession.erase(fd);
        m_mtxSession.unlock();
        });
    task.detach();
}

