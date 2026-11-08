#include <hik_camera/camera.hpp>
#include <iostream>

int main(int argc, char** argv)
{
  const bool FORMATS_ONLY = argc > 1 && std::string(argv[1]) == "--formats";
  if (argc > 2 || (argc == 2 && !FORMATS_ONLY))
  {
    std::cerr << "Usage: hik_capture [--formats]\n";
    return 1;
  }
  hik::Camera camera;
  if (!camera.Open())
  {
    return 1;
  }
  if (FORMATS_ONLY)
  {
    std::vector<std::string> formats;
    if (!camera.PixelFormats(formats))
    {
      return 1;
    }
    for (const auto& name : formats)
    {
      std::cout << name << '\n';
    }
    return 0;
  }
  if (!camera.Start())
  {
    return 1;
  }

  hik::Frame frame;
  int count = 0;
  while (camera.Read(frame))
  {
    // 在此调用算法，图像为 BGR8。
    // belike: process(frame.image);
    std::cout << "frame=" << frame.sequence << " " << frame.image.cols << "x"
              << frame.image.rows << '\n';
    ++count;
  }
  return camera.Close() ? 0 : 1;
}
