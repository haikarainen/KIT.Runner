#pragma once

#include <WIR/Stream.hpp>

namespace utils
{
  std::string getVulkanSDKPath();

  bool writeAsset(std::string const &outputFile, std::string const &assetClass, wir::Stream &dataStream);

  bool readFileToString(std::string const &file, std::string &outString);
} // namespace utils