# RtspServer

## 使用效果举例:

运行程序代码后，打开cmd，执行以下代码进行桌面推流:
**ffmpeg -f gdigrab -i desktop -c:v libx264 -f  rtsp -rtsp_transport tcp rtsp://127.0.0.1/live/test**

这会将流推送给流媒体，创建了一条流媒体的转发流rtsp://127.0.0.1/live/test。
随后使用potplayer或者vlcplayer拉取rtsp://127.0.0.1/live/test这条流，即可成功播放转发流。


> 注意，这个流媒体是简单的rtsp流媒体转发程序，不做rtp和rtcp包的分析和重构处理，仅原封不动地将发送至流媒体的rtsp流转发出去。

## 开篇:

作为流媒体，他的并发能力很重要，因此从搭建之初我就引入了wepoll库（很小，就两个文件），以其为核心做开发。

最初项目结构为

```cpp
main.cpp 主程序
wepoll.h 三方库
wepoll.c 三方库
```
随后开始扩展
过程1:对wepoll进行封装，做套接字的并发处理，从而产生一个新类EventPoller。  
过程2:建立RtspServer类，接入EventPoller的并发循环，开始accpet客户端。  
过程3:得到推流到RtspServer的客户端连接，将其命名为RtspSession类使用。  
过程4:新建SdpParser类来解析推流的SDP交互报文，确定流的通道参数，给RtspSession分析流通道的数据。  
过程5:得到向RtspServer申请拉流的客户端连接，同样是RtspSession类实例。  
过程6:新建MediaPusherManager类，用于追踪管理和区分推/拉流的RtspSession。  
总项目结构完成，5个类完成一个简单的流媒体转发服务器。  

```cpp
main.cpp

wepoll.h
wepoll.c

EventPoller.h
EventPoller.cpp

RtspServer.h
RtspServer.cpp

RtspSession.h
RtspSession.cpp

SdpParser.h
SdpParser.cpp

MediaPusherManager.h
MediaPusherManager.cpp
```

