
#include "Command_ImportFont.hpp"
#include "MSDF/msdfgen.h"
#include "Utils.hpp"

#include <KIT/Assets/Font.hpp>

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>

#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLParser.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLAttribute.hpp>

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

  struct FtContext
  {
    msdfgen::Point2 position;
    msdfgen::Shape *shape;
    msdfgen::Contour *contour;
  };

  msdfgen::Point2 ftPoint2(const FT_Vector &vector)
  {
    return msdfgen::Point2(F26DOT6_TO_DOUBLE(vector.x), F26DOT6_TO_DOUBLE(vector.y));
  }

  static FT_F26Dot6 floatToF26Dot6(float a)
  {
    return (FT_F26Dot6)(a * (1 << 6) + 0.5);
  }


  int ftMoveTo(const FT_Vector *to, void *user)
  {
    FtContext *context = reinterpret_cast<FtContext *>(user);
    if (!(context->contour && context->contour->edges.empty()))
      context->contour = &context->shape->addContour();
    context->position = ftPoint2(*to);
    return 0;
  }

  int ftLineTo(const FT_Vector *to, void *user)
  {
    FtContext *context = reinterpret_cast<FtContext *>(user);
    msdfgen::Point2 endpoint = ftPoint2(*to);
    if (endpoint != context->position)
    {
      context->contour->addEdge(new msdfgen::LinearSegment(context->position, endpoint));
      context->position = endpoint;
    }
    return 0;
  }

  int ftConicTo(const FT_Vector *control, const FT_Vector *to, void *user)
  {
    FtContext *context = reinterpret_cast<FtContext *>(user);
    context->contour->addEdge(new msdfgen::QuadraticSegment(context->position, ftPoint2(*control), ftPoint2(*to)));
    context->position = ftPoint2(*to);
    return 0;
  }

  int ftCubicTo(const FT_Vector *control1, const FT_Vector *control2, const FT_Vector *to, void *user)
  {
    FtContext *context = reinterpret_cast<FtContext *>(user);
    context->contour->addEdge(new msdfgen::CubicSegment(context->position, ftPoint2(*control1), ftPoint2(*control2), ftPoint2(*to)));
    context->position = ftPoint2(*to);
    return 0;
  }

  bool loadGlyph(msdfgen::Shape &output, FT_Face font, FT_GlyphSlot g)
  {
    output.contours.clear();
    output.inverseYAxis = false;

    ::FtContext context = {};
    context.shape = &output;
    FT_Outline_Funcs ftFunctions;
    ftFunctions.move_to = &ftMoveTo;
    ftFunctions.line_to = &ftLineTo;
    ftFunctions.conic_to = &ftConicTo;
    ftFunctions.cubic_to = &ftCubicTo;
    ftFunctions.shift = 0;
    ftFunctions.delta = 0;
    FT_Error error = FT_Outline_Decompose(&g->outline, &ftFunctions, &context);
    if (error)
      return false;
    if (!output.contours.empty() && output.contours.back().edges.empty())
      output.contours.pop_back();
    return true;
  }

  bool generateFontData(wir::Stream &toStream, float nativeSize, std::string const &filename)
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

    std::map<uint32_t, kit::Glyph> glyphIndex;

    // Determine cellsize
    uint32_t cellSize = (uint32_t)glm::ceil(nativeSize);
    
    // Get the square root of the number of glyphs, to calculate the size of a square grid needed to take all glyphs
    uint32_t gridSize = (uint32_t)glm::ceil(glm::sqrt(float(glyphData.size())));

    // Calculate the total grid size in pixels
    float gridSizePx = glm::ceil(float(gridSize) * float(cellSize));

    uint32_t gridSizePxi = (uint32_t)gridSizePx;
    glm::uvec2 gridSizePxv = glm::uvec2(gridSizePxi, gridSizePxi);
    std::vector<glm::vec4> data(gridSizePxi * gridSizePxi, glm::vec4{});

    glm::vec2 currTexPos(0.0f, 0.0f);
    currTexPos.y = gridSizePx - cellSize;
    for (char32_t const &currChar : glyphData)
    {
      if (FT_Load_Char(ftFace, currChar, FT_LOAD_NO_SCALE ) != 0)
      {
        LogError("Could not load character from font");
      }

      FT_GlyphSlot g = ftFace->glyph;

      kit::Glyph adder;
      adder.size.x = cellSize;
      adder.size.y = cellSize;
      adder.placement.x = 0.0f;
      adder.placement.y = 0.0f;
      adder.advance.x = F26DOT6_TO_DOUBLE(g->advance.x);
      adder.advance.y = F26DOT6_TO_DOUBLE(g->advance.y);

      if (!(adder.size.x == 0 && adder.size.y == 0))
      {
        adder.uv.x = ((currTexPos.x) / gridSizePx);
        adder.uv.y = ((currTexPos.y) / gridSizePx);
        adder.uv.z = ((currTexPos.x + float(adder.size.x)) / gridSizePx);
        adder.uv.w = ((currTexPos.y + float(adder.size.y)) / gridSizePx);

        msdfgen::Shape shape;
        if (!::loadGlyph(shape, ftFace, g))
        {
          continue;
        }

        if (!shape.contours.empty())
        {
          shape.normalize();
          msdfgen::edgeColoringSimple(shape, 3.0);
          msdfgen::Bitmap<float, 4> msdf(cellSize, cellSize);
          msdfgen::generateMTSDF(msdf, shape, 4.0, 1.0, msdfgen::Vector2(0.0, 0.0));

          //if (currChar == U'K')
          for (uint32_t x = 0; x < cellSize; x++)
          {
            for (uint32_t y = 0; y < cellSize; y++)
            {
              uint32_t offset = ((currTexPos.y + y) * gridSizePxv.x) + currTexPos.x + x;

              data[offset].x = msdf(x, cellSize - y - 1)[0];
              data[offset].y = msdf(x, cellSize - y - 1)[1];
              data[offset].z = msdf(x, cellSize - y - 1)[2];
              data[offset].w = msdf(x, cellSize - y - 1)[3];
            }
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

    toStream << nativeSize << gridSizePxv;
    toStream.write(reinterpret_cast<uint8_t*>(data.data()), data.size() * sizeof(glm::vec4));

    
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

      std::string importData = wir::format("<Font SourceFile=\"%s\" OutputFile=\"%s.asset\" NativeSize=\"32.0\" />", inputFile.name().c_str(), inputFile.basename().c_str());
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
  std::string outputFile;
  if (!root->string("OutputFile", outputFile))
  {
    LogError("No output file specified");
    return false;
  }
  auto outputFilef = wir::File(importBase + "/" + outputFile);

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

  double nativeSize = 32.0f;
  root->decimal("NativeSize", nativeSize);

  wir::Stream assetData;
  if(!generateFontData(assetData, (float)nativeSize, sourceFilef.path()))
  {
    LogError("Font data generation failed");
    return false;
  }

  if (!utils::writeAsset(outputFilef.path(), "kit::Font", assetData))
  {
    LogError("writeAsset failed");
    return false;
  }

  return true;
}

uint64_t Command_ImportFont::requiredArguments() const
{
  return 3; // 2 + inputfile + outputfile
}
