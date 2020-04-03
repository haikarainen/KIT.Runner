#include "Command_ImportTexture.hpp"
#include "Utils.hpp"

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>

#include <vulkan/vulkan.h>

#include "stb_image.h"

Command_ImportTexture::~Command_ImportTexture()
{

}

std::string const Command_ImportTexture::name() const
{
  return "import_texture";
}

bool Command_ImportTexture::execute(std::vector<std::string> args) const
{
  auto colorSpace = args[2];
  auto filtering = args[3];
  auto clampMode = args[4];

  auto inputFile = wir::File(args[5]);
  if (!inputFile.exist())
  {
    LogError("Input file does not exist (%s)", inputFile.path().c_str());
    return false;
  }

  auto outputFile = wir::File(args[6]);
  if (!outputFile.createPath())
  {
    LogError("Failed to create path for output file (%s)", outputFile.path().c_str());
    return false;
  }

  int x = 0, y = 0, comp = 0;
  //stbi_set_flip_vertically_on_load(true);
  uint8_t *dataPtr = stbi_load(inputFile.path().c_str(), &x, &y, &comp, 4);
  if (!dataPtr)
  {
    LogError("stbi_load failed");
    return false;
  }

  uint64_t dataSize = x * y * 4;

  wir::Stream dataStream;

  glm::uvec3 extents;
  extents.x = x;
  extents.y = y;
  extents.z = 1;

  uint32_t mipLevels = 1;
  uint32_t arrayLayers = 1;
  int64_t format = colorSpace == "linear" ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_SRGB;
  int64_t imageType = VK_IMAGE_TYPE_2D;
  int64_t viewType = VK_IMAGE_VIEW_TYPE_2D;
  int64_t samplerMin = filtering == "nearest" ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  int64_t samplerMag = filtering == "nearest" ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  int64_t samplerMip = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  int64_t samplerAddress = clampMode == "repeat" ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  dataStream << extents << mipLevels << arrayLayers << format << imageType << viewType << samplerMin << samplerMag << samplerMip << samplerAddress;

  dataStream << dataSize;
  dataStream.write(dataPtr, dataSize);

  stbi_image_free(dataPtr);

  if (!utils::writeAsset(outputFile.path(), "kit::Texture", dataStream))
  {
    LogError("writeAsset failed");
    return false;
  }

  LogNotice("Imported texture %s (%ux%u)", inputFile.path().c_str(), x, y);

  return true;
}

uint64_t Command_ImportTexture::requiredArguments() const
{
  return 7; // 2  + colorspace + filtering + clampmode + inputfile + outputfile
}
