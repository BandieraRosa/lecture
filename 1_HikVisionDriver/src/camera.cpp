#include "hik_camera/camera.hpp"

#include <MvCameraControl.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <opencv2/core.hpp>

namespace hik
{
namespace
{
bool Fail(const char* message)
{
  std::fprintf(stderr, "[hik_camera] %s\n", message);
  return false;
}

bool Check(int code, const char* operation)
{
  if (code == MV_OK)
  {
    return true;
  }
  std::fprintf(stderr, "[hik_camera] %s failed: 0x%08X\n", operation,
               static_cast<unsigned int>(code));
  return false;
}

bool CopyFrame(void* handle, const MV_FRAME_OUT& raw, Frame& frame)
{
  const auto& info = raw.stFrameInfo;
  const auto width = info.nExtendWidth ? info.nExtendWidth : info.nWidth;
  const auto height = info.nExtendHeight ? info.nExtendHeight : info.nHeight;
  const auto length = info.nFrameLenEx ? info.nFrameLenEx : info.nFrameLen;
  if (!raw.pBufAddr || !width || !height || width > INT_MAX || height > INT_MAX ||
      !length || length > UINT_MAX || info.nLostPacket ||
      info.nExtraType != MV_FRAME_EXTRA_NO_INFO)
  {
    return Fail("Invalid or incomplete image");
  }
  const auto bytes = static_cast<std::uint64_t>(width) * height * 3;
  if (bytes > UINT_MAX)
  {
    return Fail("Image exceeds the SDK conversion buffer limit");
  }

  frame.image = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC3);
  frame.sequence = info.nFrameNum;
  frame.device_timestamp_ticks =
      (static_cast<std::uint64_t>(info.nDevTimeStampHigh) << 32) | info.nDevTimeStampLow;
  if (info.enPixelType == PixelType_Gvsp_BGR8_Packed)
  {
    if (length < bytes)
    {
      return Fail("Truncated BGR image");
    }
    std::memcpy(frame.image.data, raw.pBufAddr, static_cast<std::size_t>(bytes));
    return true;
  }
  MV_CC_PIXEL_CONVERT_PARAM_EX conversion{};
  conversion.nWidth = width;
  conversion.nHeight = height;
  conversion.enSrcPixelType = info.enPixelType;
  conversion.pSrcData = raw.pBufAddr;
  conversion.nSrcDataLen = static_cast<unsigned int>(length);
  conversion.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
  conversion.pDstBuffer = frame.image.data;
  conversion.nDstBufferSize = static_cast<unsigned int>(bytes);
  if (!Check(MV_CC_ConvertPixelTypeEx(handle, &conversion), "ConvertPixelType"))
  {
    return false;
  }
  return conversion.nDstLen == bytes || Fail("Unexpected converted image length");
}
}  // namespace

Camera::~Camera() { Close(); }

bool Camera::Open(const Config& config)
{
  if (handle_)
  {
    return Fail("Close the current camera before Open");
  }
  if (!sdk_initialized_)
  {
    if (!Check(MV_CC_Initialize(), "Initialize"))
    {
      return false;
    }
    sdk_initialized_ = true;
  }
  auto fail = [this]
  {
    Close();
    return false;
  };
  MV_CC_DEVICE_INFO_LIST devices{};
  if (!Check(MV_CC_EnumDevices(MV_USB_DEVICE, &devices), "EnumDevices"))
  {
    return fail();
  }
  if (!devices.nDeviceNum || !devices.pDeviceInfo[0])
  {
    Fail("USB camera was not found");
    return fail();
  }
  if (!Check(MV_CC_CreateHandle(&handle_, devices.pDeviceInfo[0]), "CreateHandle"))
  {
    return fail();
  }
  if (!Check(MV_CC_OpenDevice(handle_), "OpenDevice"))
  {
    return fail();
  }
  opened_ = true;
  return Configure(config) || fail();
}

bool Camera::PixelFormats(std::vector<std::string>& formats) const
{
  formats.clear();
  if (!opened_)
  {
    return Fail("Open the camera before querying pixel formats");
  }
  MVCC_ENUMVALUE_EX values{};
  if (!Check(MV_CC_GetEnumValueEx(handle_, "PixelFormat", &values), "Get PixelFormat"))
  {
    return false;
  }
  for (unsigned int i = 0; i < values.nSupportedNum; ++i)
  {
    MVCC_ENUMENTRY entry{};
    entry.nValue = values.nSupportValue[i];
    if (!Check(MV_CC_GetEnumEntrySymbolic(handle_, "PixelFormat", &entry),
               "Get pixel format name"))
    {
      formats.clear();
      return false;
    }
    formats.emplace_back(entry.chSymbolic);
  }
  return true;
}

bool Camera::Configure(const Config& config)
{
  auto set_enum = [this](const char* key, const char* value)
  { return Check(MV_CC_SetEnumValueByString(handle_, key, value), key); };
  auto set_float = [this](const char* key, double value)
  {
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<float>::max())
    {
      return Fail("Invalid numeric camera setting");
    }
    return Check(MV_CC_SetFloatValue(handle_, key, static_cast<float>(value)), key);
  };
  if (!Check(MV_CC_SetImageNodeNum(handle_, 3), "SetImageNodeNum") ||
      !Check(MV_CC_SetGrabStrategy(handle_, MV_GrabStrategy_LatestImagesOnly),
             "SetGrabStrategy") ||
      !set_enum("AcquisitionMode", "Continuous") || !set_enum("TriggerMode", "Off") ||
      !set_enum("ExposureAuto", "Off") || !set_enum("GainAuto", "Off") ||
      !set_float("ExposureTime", config.exposure_us) ||
      !set_float("Gain", config.gain_db))
  {
    return false;
  }
  if (!config.pixel_format.empty() &&
      !set_enum("PixelFormat", config.pixel_format.c_str()))
  {
    return false;
  }
  if (config.gamma && !set_float("Gamma", *config.gamma))
  {
    return false;
  }
  if (config.auto_white_balance &&
      !set_enum("BalanceWhiteAuto", *config.auto_white_balance ? "Continuous" : "Off"))
  {
    return false;
  }
  if (config.frame_rate_hz &&
      (!Check(MV_CC_SetBoolValue(handle_, "AcquisitionFrameRateEnable", true),
              "AcquisitionFrameRateEnable") ||
       !set_float("AcquisitionFrameRate", *config.frame_rate_hz)))
  {
    return false;
  }
  return true;
}

bool Camera::Start()
{
  if (!opened_)
  {
    return Fail("Open the camera before Start");
  }
  if (streaming_)
  {
    return true;
  }
  streaming_ = Check(MV_CC_StartGrabbing(handle_), "StartGrabbing");
  return streaming_;
}

bool Camera::Read(Frame& frame, std::chrono::milliseconds timeout)
{
  frame = {};
  if (!streaming_)
  {
    return Fail("Start the camera before Read");
  }
  if (timeout.count() < -1 || timeout.count() > UINT_MAX)
  {
    return Fail("Invalid read timeout");
  }
  const bool WAIT = timeout.count() == -1;
  MV_FRAME_OUT raw{};
  for (;;)
  {
    const int CODE = MV_CC_GetImageBuffer(
        handle_, &raw, WAIT ? 100 : static_cast<unsigned int>(timeout.count()));
    if (static_cast<unsigned int>(CODE) != MV_E_NODATA &&
        static_cast<unsigned int>(CODE) != MV_E_NOOUTBUF)
    {
      if (!Check(CODE, "GetImageBuffer"))
      {
        return false;
      }
      break;
    }
    if (!MV_CC_IsDeviceConnected(handle_))
    {
      return Fail("Camera disconnected");
    }
    if (!WAIT)
    {
      return false;
    }
  }
  frame.received_at = std::chrono::steady_clock::now();
  bool copied = false;
  try
  {
    copied = CopyFrame(handle_, raw, frame);
  }
  catch (const cv::Exception& exception)
  {
    Fail(exception.what());
  }
  catch (...)
  {
    MV_CC_FreeImageBuffer(handle_, &raw);
    throw;
  }
  const bool RELEASED = Check(MV_CC_FreeImageBuffer(handle_, &raw), "FreeImageBuffer");
  if (!copied || !RELEASED)
  {
    frame = {};
  }
  return copied && RELEASED;
}

bool Camera::Stop()
{
  if (!streaming_)
  {
    return true;
  }
  if (!Check(MV_CC_StopGrabbing(handle_), "StopGrabbing"))
  {
    return false;
  }
  streaming_ = false;
  return true;
}

bool Camera::Close()
{
  bool ok = true;
  if (handle_)
  {
    if (streaming_ && !Stop())
    {
      ok = false;
    }
    if (opened_)
    {
      if (Check(MV_CC_CloseDevice(handle_), "CloseDevice"))
      {
        opened_ = streaming_ = false;
      }
      else
      {
        ok = false;
      }
    }
    if (Check(MV_CC_DestroyHandle(handle_), "DestroyHandle"))
    {
      handle_ = nullptr;
      opened_ = streaming_ = false;
    }
    else
    {
      ok = false;
    }
  }
  if (!handle_ && sdk_initialized_)
  {
    if (!Check(MV_CC_Finalize(), "Finalize"))
    {
      ok = false;
    }
    sdk_initialized_ = false;
  }
  return ok;
}
}  // namespace hik
