#pragma once

#include <chrono>
#include <cstdint>
#include <opencv2/core/mat.hpp>
#include <optional>
#include <string>
#include <vector>

namespace hik
{
struct Config
{
  double exposure_us{1000.0};
  double gain_db{15.0};
  std::optional<double> gamma;
  std::optional<double> frame_rate_hz;
  std::optional<bool> auto_white_balance;
  std::string pixel_format;  // 留空时保留相机当前的像素格式。
};

struct Frame
{
  cv::Mat image;  // 独立持有 BGR8 图像，后续取帧或关闭相机不会覆盖它。
  std::uint32_t sequence{0};
  std::uint64_t device_timestamp_ticks{0};  // 设备原始时钟
  std::chrono::steady_clock::time_point received_at{};
};

class Camera
{
 public:
  Camera() = default;
  ~Camera();
  Camera(const Camera&) = delete;
  Camera& operator=(const Camera&) = delete;

  bool Open(const Config& config = {});
  bool PixelFormats(std::vector<std::string>& formats) const;
  bool Start();
  // 返回 false 表示未取得图像；SDK 错误会打印，普通超时保持静默。
  bool Read(Frame& frame,
            std::chrono::milliseconds timeout = std::chrono::milliseconds{-1});
  bool Stop();
  bool Close();
  bool IsOpen() const noexcept { return opened_; }
  bool IsStreaming() const noexcept { return streaming_; }

 private:
  bool Configure(const Config& config);
  void* handle_{nullptr};
  bool opened_{false};
  bool streaming_{false};
  bool sdk_initialized_{false};
};
}  // namespace hik
