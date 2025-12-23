#include "EventPoller.h"

#include "wepoll.h"
#include "Windows.h"

//可管理的最大套接字数量，对标select的最大管理数量，但其实这个值并没有用，传进epoll_create里会被忽略
#define EPOLL_SIZE 1024

EventPoller::EventPoller()
{
    m_vecSharedBuffer.resize(32 * 4 * 1024);//128k共享缓存
    //给本例创建对应的epoll
    m_hEpoll = epoll_create(EPOLL_SIZE);
}

EventPoller::~EventPoller()
{
}

bool EventPoller::AddEvent(int fd, int event, PollEventCB cb)
{
    //如果是当前线程进行的事件添加则直接放进map里
    struct epoll_event ev = { 0 };
    ev.events = event;
    ev.data.fd = fd;
    const int iRet = epoll_ctl(m_hEpoll, EPOLL_CTL_ADD, fd, &ev);
    if (-1 == iRet) {
        return false;
    }
    //记录要监听的套接字和其对应的处理回调
    m_mapEventCB.emplace(fd, std::make_shared<PollEventCB>(std::move(cb)));
    return true;
}

bool EventPoller::DelEvent(int fd) {

    int iRet = -1;
    //仅删除监听中的fd，如果没删除成功则意味着对应的fd在逻辑上已不受监听而无需DEL。
    //如果后续发现在监听回调里出现了不在监听中的fd，则在监听回调里再进行DEL，同时忽略那次的监听执行。
    if (m_mapEventCB.erase(fd)) {
        iRet = epoll_ctl(m_hEpoll, EPOLL_CTL_DEL, fd, nullptr);
    }
    return -1 != iRet;
}

void EventPoller::Exec()
{
    m_isRun = true;
    int iMinDelay = 5000;//等待事件的超时毫秒数
    struct epoll_event *events = new struct epoll_event[EPOLL_SIZE];
    while (m_isRun) {
        int ret = epoll_wait(m_hEpoll, events, EPOLL_SIZE, iMinDelay);
        if (ret <= 0) 
            continue;

        for (int i = 0; i < ret; ++i) {
            struct epoll_event& ev = events[i];
            int fd = ev.data.fd;

            //找找要被监听的事件map里有没有这个fd，没有就移除掉这个残留的监听
            auto it = m_mapEventCB.find(fd);
            if (it == m_mapEventCB.end()) {
                epoll_ctl(m_hEpoll, EPOLL_CTL_DEL, fd, nullptr);
                continue;
            }
            (*(it->second))(ev.events);
        }
    }
    delete[] events;
}

std::vector<char>& EventPoller::GetSharedBuffer()
{
    return m_vecSharedBuffer;
}
