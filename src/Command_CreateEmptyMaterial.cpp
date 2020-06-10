
#include "Command_CreateEmptyMaterial.hpp"
#include "Utils.hpp"

#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLParser.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLAttribute.hpp>

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>


Command_CreateEmptyMaterial::~Command_CreateEmptyMaterial()
{

}

std::string const Command_CreateEmptyMaterial::name() const
{
  return "create_empty_material";
}

bool Command_CreateEmptyMaterial::execute(std::vector<std::string> args) const
{
  auto importFile = args[2];

  wir::XMLDocument document;
  wir::XMLParser parser;
  if(!parser.loadFromFile(importFile, document))
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

  if (root->name() != "EmptyMaterial")
  {
    LogError("invalid material spec");
    return false;
  }
  std::string outputFile;
  if(!root->string("OutputFile", outputFile))
  {
    LogError("No output file specified");
    return false;
  }

  std::string materialClass;
  if (!root->string("MaterialClass", materialClass))
  {
    LogError("No material class specified");
    return false;
  }


  auto outputFilef = wir::File(outputFile);
  if (!outputFilef.createPath())
  {
    LogError("Failed to create path for output file (%s)", outputFilef.path().c_str());
    return false;
  }

  wir::Stream assetData;
  assetData << materialClass;
  assetData << uint64_t(0);

  if (!utils::writeAsset(outputFilef.path(), "kit::Material", assetData))
  {
    LogError("writeAsset failed");
    return false;
  }

  return true;
}

uint64_t Command_CreateEmptyMaterial::requiredArguments() const
{
  return 3; // 2 + importfile
}
