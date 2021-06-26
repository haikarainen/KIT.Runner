
#include "Command_ImportFont.hpp"
#include "Utils.hpp"

#include <KIT/Assets/Font.hpp>

#include <WIR/Error.hpp>
#include <WIR/Filesystem.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>

#include <WIR/XML/XMLAttribute.hpp>
#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLParser.hpp>

#include <cinttypes>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SIZES_H

#define F26DOT6_TO_DOUBLE(x) (1 / 64.0 * double(x))

namespace
{

  // What characters to cache glyphs for. Any character not in this string will not be renderable using Kit
  // Currently contains every visible character on a standard swedish qwerty-keyboard, as well as a caret: ‸
  // TODO: Should be removed and have glyph generation be completely dynamic, with better packing algorithm!
  const std::u32string glyphData = U" –ABCDEFGHIJKLMNOPQRSTUVWXYZÅÄÖabcdefghijklmnopqrstuvwxyzåäö0123456789§½¶!¡\"@#£¤$%€&¥/{([)]=}?\\+`´±¨~^'´*-_.:·,;¸µ€<>|‸�";

  bool generateFontData(wir::Stream &toStream, float inSize, std::string const &filename)
  {
    FT_Library ftLibrary = FT_Library();
    if (FT_Init_FreeType(&ftLibrary))
    {
      LogError("Could not initialize Freetype");
      return false;
    }

    FT_Face ftFace = nullptr;
    if (FT_New_Face(ftLibrary, filename.c_str(), 0, &ftFace) != 0)
    {
      LogError("Failed to load font from file");
    }
    FT_Select_Charmap(ftFace, ft_encoding_unicode);
    FT_Set_Pixel_Sizes(ftFace, 0, (FT_UInt)inSize);

    std::map<uint32_t, kit::Glyph> glyphIndex;

    // First iterate through all the characters to get the max possible size
    glm::uvec2 maxSize(0, 0);
    for (char32_t const &currChar : glyphData)
    {
      if (FT_Load_Char(ftFace, currChar, FT_LOAD_RENDER) != 0)
      {
        LogWarning("Could not load character from font");
        continue;
      }

      FT_GlyphSlot g = ftFace->glyph;

      if (maxSize.x < g->bitmap.width)
      {
        maxSize.x = g->bitmap.width;
      }

      if (maxSize.y < g->bitmap.rows)
      {
        maxSize.y = g->bitmap.rows;
      }
    }
    uint32_t cellSize = (glm::max)(maxSize.x, maxSize.y);
    uint32_t gridSize = (uint32_t)glm::ceil(glm::sqrt(float(glyphData.size())));

    // Calculate the total grid size in pixels
    float gridSizePx = glm::ceil(float(gridSize) * float(cellSize));
    uint32_t gridSizePxi = (uint32_t)gridSizePx;
    glm::uvec2 gridSizePxv = glm::uvec2(gridSizePxi, gridSizePxi);

    std::vector<glm::u8vec4> data(gridSizePxi * gridSizePxi, glm::u8vec4(255, 255, 255, 0));
    glm::vec2 currTexPos(0.0f, 0.0f);
    currTexPos.y = gridSizePx - cellSize;
    for (char32_t const &currChar : glyphData)
    {
      if (FT_Load_Char(ftFace, currChar, FT_LOAD_RENDER) != 0)
      {
        LogError("Could not load character from font");
      }

      FT_GlyphSlot g = ftFace->glyph;

      kit::Glyph adder;
      adder.size.x = g->bitmap.width;
      adder.size.y = g->bitmap.rows;
      adder.placement.x = g->bitmap_left;
      adder.placement.y = g->bitmap_top;
      adder.advance.x = float(g->advance.x >> 6);
      adder.advance.y = float(g->advance.y >> 6);

      if (!(g->bitmap.width == 0 && g->bitmap.rows == 0))
      {
        adder.uv.x = (currTexPos.x / gridSizePx);
        adder.uv.y = (currTexPos.y / gridSizePx);
        adder.uv.z = ((currTexPos.x + float(adder.size.x)) / gridSizePx);
        adder.uv.w = ((currTexPos.y + float(adder.size.y)) / gridSizePx);

        for (uint32_t ay = 0, ty = currTexPos.y; ay < adder.size.y; ay++, ty++)
        {
          for (uint32_t ax = 0, tx = currTexPos.x; ax < adder.size.x; ax++, tx++)
          {
            data[ty * gridSizePxi + tx].a = g->bitmap.buffer[ay * uint32_t(adder.size.x) + ax];
          }
        }

        currTexPos.x += cellSize;
        if (currTexPos.x >= gridSizePx)
        {
          currTexPos.x = 0;
          currTexPos.y -= cellSize;
        }
      }
      else
      {
        adder.uv.x = 0;
        adder.uv.y = 0;
        adder.uv.z = 0;
        adder.uv.w = 0;
      }

      glyphIndex[currChar] = adder;
    }

    float lineHeight = float(ftFace->height) / 64.0f;
    float height = (float)glyphIndex[0x00000058].size.y;

    toStream << uint16_t(glyphIndex.size());

    for (auto g : glyphIndex)
    {
      toStream << g.first << g.second.advance << g.second.placement << g.second.size << g.second.uv;
    }

    toStream << gridSizePxv << lineHeight << height;
    toStream.write(reinterpret_cast<uint8_t const *>(data.data()), data.size() * sizeof(glm::u8vec4));

    if (FT_Done_Face(ftFace))
    {
      LogError("Failed to release font");
    }

    if (FT_Done_FreeType(ftLibrary))
    {
      LogError("Could not destroy Freetype");
    }

    return true;
  }

} // namespace

Command_ImportFont::~Command_ImportFont()
{
}

std::string const Command_ImportFont::name() const
{
  return "import_font";
}

bool Command_ImportFont::execute(std::vector<std::string> args) const
{
  auto inputFile = wir::File(args[2]);
  if (!inputFile.exist())
  {
    LogError("Input file does not exist!");
    return false;
  }

  auto importFile = inputFile.path();

  if (inputFile.extension() != ".import")
  {
    importFile += ".import";

    if (!wir::File(importFile).exist())
    {
      auto inputBase = inputFile.directory().path() + "/" + inputFile.basename();

      std::string importData = wir::format("<Font SourceFile=\"%s\" OutputName=\"%s\" />", inputFile.name().c_str(), inputFile.basename().c_str());
      if (!wir::File(importFile).writeString(importData))
      {
        LogError("Failed to create import file");
        return false;
      }
    }
  }

  auto importBase = wir::File(importFile).directory().path();

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
    LogError("invalid font spec");
    return false;
  }

  auto root = roots[0];

  if (root->name() != "Font")
  {
    LogError("invalid font spec");
    return false;
  }
  std::string outputName;
  if (!root->string("OutputName", outputName))
  {
    LogError("No output file specified");
    return false;
  }

  std::string sourceFile;
  if (!root->string("SourceFile", sourceFile))
  {
    LogError("No source file specified");
    return false;
  }

  auto sourceFilef = wir::File(importBase + "/" + sourceFile);
  if (!sourceFilef.exist())
  {
    LogError("Source file doesn't exist");
    return false;
  }

  std::vector<int64_t> FontSizes = {8, 10, 12, 14, 16, 18, 24};
  root->integerArray("FontSizes", FontSizes);

  for (auto fS : FontSizes)
  {
    wir::Stream assetData;
    if (!generateFontData(assetData, fS, sourceFilef.path()))
    {
      LogError("Font data generation failed");
      return false;
    }

    std::string outputFilename = wir::format("%s/%s_%u.asset", importBase.c_str(), outputName.c_str(), fS);

    if (!utils::writeAsset(wir::File(outputFilename).path(), "kit::Font", assetData))
    {
      LogError("writeAsset failed");
      return false;
    }
  }

  return true;
}

uint64_t Command_ImportFont::requiredArguments() const
{
  return 3; // 2 + inputfile + outputfile
}
