#pragma once

#include <string>
#include <functional>
#include <mutex>

typedef void* HANDLE;

/// <summary>
/// epoll处理线程:这是对epoll三方库的进一步封装，它为网络套接字服务，负责单线程内处理千万级以上的套接字连接
/// 每个类实例都封装了一条线程用于接收并处理套接字的相关业务
/// </summary>
class EventPoller {
public:
    using PollEventCB = std::function<void(int event)>;         //套接字的监听回调

    //提供给wepoll的监听事件类型
    typedef enum {
        Event_Read = 1 << 0,    // 读事件，对标wepoll.h的EPOLLIN
        Event_Write = 1 << 2,   // 写事件，对标wepoll.h的EPOLLOUT
        Event_Error = 1 << 3,   // 错误事件，对标wepoll.h的EPOLLERR
    } Poll_Event;

    /// <summary>
    /// 构造
    /// </summary>
    EventPoller();
    ~EventPoller();

    /// <summary>
    /// 添加事件监听
    /// </summary>
    /// <param name="fd">监听的套接字</param>
    /// <param name="event">事件类型，例如 Event_Read | Event_Write</param>
    /// <param name="cb">事件回调functional</param>
    /// <returns>是否成功</returns>
    bool AddEvent(int fd, int event, PollEventCB cb);

    /// <summary>
    /// 删除事件监听
    /// </summary>
    /// <param name="fd">监听的套接字</param>
    /// <returns>是否成功</returns>
    bool DelEvent(int fd);

    /// <summary>
    /// 执行事件轮询死循环
    /// </summary>
    void Exec();

    /// <summary>
    /// 获取当前线程下所有socket共享的读缓存
    /// </summary>
    /// <returns></returns>
    std::vector<char>& GetSharedBuffer();
private:
    bool m_isRun = false;               //处理线程是否正在执行
    HANDLE m_hEpoll = NULL;             //本例中管控socket的epoll的fd
    std::unordered_map<int, std::shared_ptr<PollEventCB>> m_mapEventCB; //监听的套接字和他的处理回调 <套接字，处理回调>

    std::vector<char> m_vecSharedBuffer;            //当前线程下所有socket共享的读缓存
};