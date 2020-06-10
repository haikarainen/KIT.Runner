#include "Command_ImportFont.hpp"
#include "Utils.hpp"

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>


#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLParser.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLAttribute.hpp>

#include <cinttypes>

Command_ImportFont::~Command_ImportFont()
{

}

std::string const Command_ImportFont::name() const
{
  return "import_font";
}

bool Command_ImportFont::execute(std::vector<std::string> args) const
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

  wir::Stream assetData;

  if (!assetData.readFile(sourceFilef.path()))
  {
    LogError("Failed to read source file");
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
