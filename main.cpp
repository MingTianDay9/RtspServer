#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment (lib, "Ws2_32.lib")
#pragma comment(lib,"Iphlpapi.lib")
#include <string>
#include <map>
#include <vector>
#include <iostream>

#include "EventPoller.h"
#include "RtspServer.h"

int main()
{
	bool isRet;
	//检查网络WSA是否可用，不可用则退出
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	isRet = (0 == WSAStartup(wVersionRequested, &wsaData));
	if(false == isRet)
		return 1;
	EventPoller eventPoller;
	auto rtspServer = std::make_shared<RtspServer>(&eventPoller);
	if (rtspServer->Start("127.0.0.1", 554)) {
		std::cout << "已就绪，请试着用ffmpeg运行如下命令进行推流:ffmpeg -f gdigrab -i desktop -c:v libx264 -f  rtsp -rtsp_transport tcp rtsp://127.0.0.1/live/test\n";
		std::cout << "推流成功后，可以使用某个播放器(比如VlcPlayer/PotPlayer)对rtsp://127.0.0.1/live/test进行拉流以查看转发效果\n";
		eventPoller.Exec();
		rtspServer->Stop();
	}
	rtspServer.reset();

	WSACleanup();
	return 0;
}