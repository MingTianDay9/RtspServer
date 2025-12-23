#include "MediaPusherManager.h"
#include "RtspSession.h"

bool MediaPusherManager::AddPusher(const std::string& szUrl, RtspSession* pRtspSession)
{
    {
        std::lock_guard lck(m_mtxMediaPusher);
        if (m_mapMediaPusher.find(szUrl) == m_mapMediaPusher.end()) {
            m_mapMediaPusher[szUrl] = new MediaPusher(szUrl, pRtspSession);
            return true;
        }
    }
    return false;
}

bool MediaPusherManager::DelPusher(const std::string& szUrl)
{
    {
        std::lock_guard lck(m_mtxMediaPusher);
        if (m_mapMediaPusher.find(szUrl) != m_mapMediaPusher.end()) {
            delete m_mapMediaPusher.at(szUrl);
            m_mapMediaPusher.erase(szUrl);
            return true;
        }
    }
    return false;
}

bool MediaPusherManager::IsExist(const std::string& szUrl)
{
    std::lock_guard lck(m_mtxMediaPusher);
    return m_mapMediaPusher.find(szUrl) != m_mapMediaPusher.end();
}

RtspSession* MediaPusherManager::AddRecvier(const std::string& szUrl, RtspSession* pRtspSession)
{
    {
        std::lock_guard lck(m_mtxMediaPusher);
        if (m_mapMediaPusher.find(szUrl) != m_mapMediaPusher.end()) {
            auto pusher = m_mapMediaPusher.at(szUrl);
            pusher->AddRecvier(pRtspSession);
            return pusher->GetRtspSession();
        }
    }
    return nullptr;
}

bool MediaPusherManager::DelRecvier(const std::string& szUrl, RtspSession* pRtspSession)
{
    {
        std::lock_guard lck(m_mtxMediaPusher);
        if (m_mapMediaPusher.find(szUrl) != m_mapMediaPusher.end()) {
            m_mapMediaPusher.at(szUrl)->DelRecvier(pRtspSession);
            return true;
        }
    }
    return false;
}

bool MediaPusherManager::SendDataToRecvier(const std::string& szUrl, const char* pData, size_t uSize)
{
    {
        std::lock_guard lck(m_mtxMediaPusher);
        if (m_mapMediaPusher.find(szUrl) != m_mapMediaPusher.end()) {
            m_mapMediaPusher.at(szUrl)->SendDataToRecvier(pData, uSize);
            return true;
        }
    }
    return false;
}

MediaPusher::MediaPusher(const std::string& szUrl, RtspSession* pRtspSession)
    :m_szUrl(szUrl)
    , m_pRtspSession(pRtspSession)
{
}

MediaPusher::~MediaPusher()
{
}

void MediaPusher::AddRecvier(RtspSession* pRtspSession)
{
    m_listRtspSession.push_back(pRtspSession);
}

void MediaPusher::DelRecvier(RtspSession* pRtspSession)
{
    m_listRtspSession.remove(pRtspSession);
}

bool MediaPusher::SendDataToRecvier(const char* pData, size_t uSize)
{
    decltype(m_listRtspSession) removeList;
    for (auto one : m_listRtspSession) {
        if (false == one->SendData(pData, uSize)) {
            removeList.push_back(one);
        }
    }
    for (auto one : removeList) {
        this->DelRecvier(one);
        one->CloseSession();
    }
    return true;
}

