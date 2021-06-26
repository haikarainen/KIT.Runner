
#include "Utils.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <SDKDDKVer.h>
#include <Windows.h>

#include <WIR/Error.hpp>
#include <WIR/Filesystem.hpp>
#include <WIR/String.hpp>

std::string utils::getVulkanSDKPath()
{
  char buffer[2048];
  DWORD dataSize = 2048;
  auto status = RegGetValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\LunarG\\VulkanSDK", "VK_SDK_PATHs", RRF_RT_REG_SZ, nullptr, &buffer, &dataSize);
  if (status == ERROR_SUCCESS)
  {
    std::string returner = buffer;

    auto parts = wir::split(returner, {';'});
    return parts[0];
  }

  LogError("Failed to find Vulkan SDK path");
  return "";
}

bool utils::writeAsset(std::string const &outputFile, std::string const &assetClass, wir::Stream &dataStream)
{
  auto outFile = wir::File(outputFile);
  if (!outFile.createPath())
  {
    LogError("Failed to create path for output file (%s)", outFile.path().c_str());
    return false;
  }

  wir::Stream compressedStream;
  dataStream.compress(compressedStream);

  wir::Stream outputStream;

  constexpr static uint8_t magic[] = "KitAsset;)<3";
  constexpr static size_t magicSize = sizeof(magic) - 1;
  outputStream.write(magic, magicSize);

  // Write version
  outputStream << uint64_t(0);

  outputStream << assetClass;
  outputStream << dataStream.size();
  outputStream << compressedStream;

  if (!outputStream.writeFile(outFile.path()))
  {
    LogError("Failed to write data to file");
    return false;
  }

  LogNotice("Sucessfully wrote asset! (%s)", outFile.path().c_str());
  return true;
}

bool utils::readFileToString(std::string const &file, std::string &outString)
{
  std::string line;
  std::ifstream handle(file);
  if (!handle)
  {
    LogError("Could not open file for reading (%s)", file.c_str());
    return false;
  }

  while (std::getline(handle, line))
  {
    outString += line + "\n";
  }

  handle.close();

  return true;
}
