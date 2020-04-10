#include "Command_ImportTexture.hpp"
#include "Utils.hpp"

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>


#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLParser.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLAttribute.hpp>

#include <Odin/Format.hpp>
#include <Odin/Filter.hpp>
#include <Odin/EdgeSampling.hpp>

#include <cinttypes>
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
  auto importFile = args[2];

  wir::XMLDocument document;
  wir::XMLParser parser;
  if (!parser.loadFromFile(importFile, document))
  {
    LogError("Failed to parse xml");
    return false;
  }

  auto roots = document.rootElements();
  if (roots.size() != 1)
  {
    LogError("invalid material spec");
    return false;
  }

  auto root = roots[0];

  if (root->name() != "Texture")
  {
    LogError("invalid material spec");
    return false;
  }
  std::string outputFile;
  if (!root->string("OutputFile", outputFile))
  {
    LogError("No output file specified");
    return false;
  }

  auto outputFilef = wir::File(outputFile);
  if (!outputFilef.createPath())
  {
    LogError("Failed to create path for output file (%s)", outputFilef.path().c_str());
    return false;
  }

  std::string sourceFile;
  if (!root->string("SourceFile", sourceFile))
  {
    LogError("No source file specified");
    return false;
  }

  auto sourceFilef = wir::File(sourceFile);
  if (!sourceFilef.exist())
  {
    LogError("Source file doesn't exist");
    return false;
  }

  std::string colorspace = "srgb";
  root->string("Colorspace", colorspace);

  bool srgb = wir::strToLower(colorspace) == "srgb";
  bool hdr = wir::strToLower(sourceFilef.extension()) == ".hdr";
  uint32_t format = hdr ? odin::F_RGBA32_SFLOAT : srgb ? odin::F_RGBA8_SRGB : odin::F_RGBA8_UNORM;

  int64_t levels = 0;
  root->integer("Levels", levels);

  uint32_t loadLevels = glm::max(1U, (uint32_t)levels);


  std::string filter = "anisotropic";
  uint32_t filteri = odin::F_Anisotropic;
  root->string("Filter", filter);
  if (wir::strToLower(filter) == "anisotropic")
  {
    filteri = odin::F_Anisotropic;
  }
  else if (wir::strToLower(filter) == "trilinear")
  {
    filteri = odin::F_Trilinear;
  }
  else if (wir::strToLower(filter) == "bilinear")
  {
    filteri = odin::F_Bilinear;
  }
  else if (wir::strToLower(filter) == "nearest")
  {
    filteri = odin::F_Nearest;
  }
  else 
  {
    LogError("Invalid filter, possible options: anisotropic, trilinear, bilinear, nearest");
    return false;
  }


  std::string es = "clamp";
  uint32_t esi = odin::ES_Clamp;
  root->string("Filter", es);
  if (wir::strToLower(es) == "clamp")
  {
    esi = odin::ES_Clamp;
  }
  else if (wir::strToLower(es) == "clampmirrored")
  {
    esi = odin::ES_ClampMirrored;
  }
  else if (wir::strToLower(es) == "repeat")
  {
    esi = odin::ES_Repeat;
  }
  else if (wir::strToLower(es) == "repeatmirrored")
  {
    esi = odin::ES_RepeatMirrored;
  }
  else
  {
    LogError("Invalid edge sampling, possible options: clamp, clampmirrored, repeat, repeatmirrored");
    return false;
  }

  double maxAniso = 16.0f;
  root->decimal("MaxAnisotrophy", maxAniso);
  float maxAnisoF = glm::clamp((float)maxAniso, 1.0f, 16.0f);

  
  wir::Stream assetData;


  if (hdr)
  {
    int x = 0, y = 0, c = 0;
    float *data = stbi_loadf(sourceFilef.path().c_str(), &x, &y, &c, 4);
    if (!data)
    {
      LogError("stbi failed");
      return false;
    }

    uint64_t dataSize = x * y * 4 * sizeof(float);
    assetData << format << glm::uvec2(x, y) << uint32_t(levels);
    assetData << filteri << esi << maxAnisoF;
    assetData << dataSize;

    LogNotice("Writing %" PRIu64 " HDR bytes for base mip", dataSize);
    assetData.write(reinterpret_cast<uint8_t *>(data), dataSize);
    stbi_image_free(data);

    for (uint32_t i = 1; i < loadLevels; i++)
    {
      std::string level;
      if (!root->string(wir::formatString("Level%u", i), level))
      {
        LogError("Level %u not specified", i);
        return false;
      }

      auto levelf = wir::File(level);
      if (!levelf.exist())
      {
        LogError("Specified level file doesn't exist, %u", i);
        return false;
      }
      x = 0;
      y = 0;
      c = 0;
      data = stbi_loadf(levelf.path().c_str(), &x, &y, &c, 4);
      if (!data)
      {
        LogError("stbi failed");
        return false;
      }
      dataSize = x * y * 4 * sizeof(float);
      assetData << dataSize;

      LogNotice("Writing %" PRIu64 " HDR bytes for mip level %u", dataSize, i);
      assetData.write(reinterpret_cast<uint8_t *>(data), dataSize);
      stbi_image_free(data);
    }
  }
  else 
  {
    int x = 0, y = 0, c = 0;
    uint8_t *data = stbi_load(sourceFilef.path().c_str(), &x, &y, &c, 4);
    if (!data)
    {
      LogError("stbi failed");
      return false;
    }

    uint64_t dataSize = x * y * 4 * sizeof(uint8_t);
    assetData << format << glm::uvec2(x, y) << uint32_t(levels);
    assetData << filteri << esi << maxAnisoF;
    assetData << dataSize;

    LogNotice("Writing %" PRIu64 " LDR bytes for base mip", dataSize);
    assetData.write(data, dataSize);
    stbi_image_free(data);

    for (uint32_t i = 1; i < loadLevels; i++)
    {
      std::string level;
      if (root->string(wir::formatString("Level%u", i), level))
      {
        LogError("Level %u not specified", i);
        return false;
      }

      auto levelf = wir::File(level);
      if (!levelf.exist())
      {
        LogError("Specified level file doesn't exist, %u", i);
        return false;
      }

      data = stbi_load(levelf.path().c_str(), &x, &y, &c, 4);
      if (!data)
      {
        LogError("stbi failed");
        return false;
      }
      dataSize = x * y * 4 * sizeof(float);
      assetData << dataSize;

      LogNotice("Writing %" PRIu64 " LDR bytes for mip level %u", dataSize, i);
      assetData.write(data, dataSize);
      stbi_image_free(data);
    }
  }

  if (!utils::writeAsset(outputFilef.path(), "kit::Texture", assetData))
  {
    LogError("writeAsset failed");
    return false;
  }

  return true;
}

uint64_t Command_ImportTexture::requiredArguments() const
{
  return 3; // 2 + inputfile + outputfile
}
