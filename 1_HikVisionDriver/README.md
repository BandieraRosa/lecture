# 海康 USB 相机

C++17、MVS SDK、OpenCV core。面向一台 USB 相机，只做连续采集，不创建线程，不自动重连。真实 SDK 位于 hikSDK/，从本机 /opt/MVS 复制，当前架构为 Linux x86_64。

## 编译

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/hik_capture           # 自动打开相机，采集 10 帧
./build/hik_capture --formats # 查询相机像素格式
~~~

## 接入算法

~~~cpp
#include <hik_camera/camera.hpp>

int main() {
    hik::Camera camera;
    hik::Config config;
    config.exposure_us = 1000;
    config.gain_db = 15;

    if (!camera.Open(config) || !camera.Start()) {
        return 1;
    }

    hik::Frame frame;
    while (camera.Read(frame)) {
        // 在此调用算法：detector.detect(frame.image)，图像为 CV_8UC3、BGR。
    }
} // 自动关闭相机
~~~

下游 CMake：

~~~cmake
add_subdirectory(/path/to/hik_ws hik_camera_build)
target_link_libraries(your_algorithm PRIVATE hik_camera::hik_camera)
~~~

所有操作统一返回 bool。SDK 错误直接打印到 stderr，返回 false，不需要 Status、错误枚举或额外查询。

## 接口

| 调用                            | 含义                                                          |
| ------------------------------- | ------------------------------------------------------------- |
| camera.Open(config)             | 自动打开 USB 相机并配置；尚未开始采集，config 可省略          |
| camera.Start()                  | 开始连续采集                                                  |
| camera.Read(frame)              | 等待下一帧，普通超时内部继续等待，错误时打印并返回 false      |
| camera.Read(frame, timeout)     | 单次限时取帧，timeout 为 chrono::milliseconds，0 表示立即查询 |
| camera.PixelFormats(formats)    | 获取已打开相机当前支持的像素格式名称                          |
| camera.Stop()                   | 停止采集，可再次 Start                                        |
| camera.Close()                  | 释放设备，析构也会执行                                        |
| camera.IsOpen() / IsStreaming() | 查询本地生命周期状态                                          |

限时 Read 返回 false 表示本次未取得帧：普通超时保持静默，错误会打印。默认 Read 会持续等待，直到得到图像或发生错误；需要定期检查应用退出条件时使用限时读取。使用一个 Camera 实例，并将所有操作串行执行；Open/Close 直接管理 SDK 初始化和释放。

## 参数与图像

Config 包含曝光、增益、可选 Gamma、帧率限制、自动白平衡和像素格式。可选项不设置则保留相机原值；pixel_format 留空也保留当前格式。SDK 固定采用 3 个图像缓冲节点和最新帧策略。

Frame 包含 image、sequence、received_at、device_timestamp_ticks。image 独立持有 BGR 像素内存，后续读取和关闭相机不会覆盖已保存的帧。复制 Frame/cv::Mat 共享像素，需要独立修改时使用 clone()。received_at 是主机单调时钟的接收时间，设备时间戳保留原始 tick，不把它当成纳秒或曝光时刻。

~~~cpp
std::vector<std::string> formats;
if (camera.PixelFormats(formats)) {
    for (const auto& name : formats) {
        std::cout << name << '\n';
    }
}
~~~

格式列表从相机获取，转换由 MV_CC_ConvertPixelTypeEx 完成，输出统一为 BGR8；原图已是 BGR8 时直接复制。相机可输出的格式和 SDK 可转换的格式不一定相同，转换失败会打印 SDK 错误并返回 false。

本次单相机简化后编译通过，使用真实 SDK 验证了未打开时的调用、打开失败清理、重复打开尝试和重复关闭。当前未发现相机，尚未验证本次实机采集。此前使用真实 SDK 与离线合成输入验证过七种图像格式的通道顺序和图像所有权。

## 参考

参考 [vision_dev 海康实现](https://github.com/QDU-VRobot/vision_dev/blob/e17e3c2aaf813f9112a36c4fa2297391ce2f60ad/src/hik_camera/src/hik_camera_node.cpp)，仅 HTTP 查阅单文件，未拉取仓库。项目代码适用 LICENSE；hikSDK/ 内厂商文件适用其自身许可。MVS 内部传输线程由厂商 SDK 管理。
