
#include "Command_CreateDefaultMaterial.hpp"
#include "Utils.hpp"

#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLParser.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLAttribute.hpp>

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>


Command_CreateDefaultMaterial::~Command_CreateDefaultMaterial()
{

}

std::string const Command_CreateDefaultMaterial::name() const
{
  return "create_default_material";
}

bool Command_CreateDefaultMaterial::execute(std::vector<std::string> args) const
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

  if (root->name() != "DeferredMaterial")
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

  auto outputFilef = wir::File(outputFile);
  if (!outputFilef.createPath())
  {
    LogError("Failed to create path for output file (%s)", outputFilef.path().c_str());
    return false;
  }

  std::string albedo, normal, metalness, occlusion, roughness;
  root->string("Albedo", albedo);
  root->string("Normal", normal);
  root->string("Metalness", metalness);
  root->string("Occlusion", occlusion);
  root->string("Roughness", roughness);

  wir::Stream instanceData;
  instanceData << albedo << normal << metalness << occlusion << roughness;

  wir::Stream assetData;
  assetData << std::string("kit::DefaultMaterial");
  assetData << uint64_t(instanceData.size());
  assetData.write(instanceData.begin(), instanceData.size());

  if (!utils::writeAsset(outputFilef.path(), "kit::Material", assetData))
  {
    LogError("writeAsset failed");
    return false;
  }

  return true;
}

uint64_t Command_CreateDefaultMaterial::requiredArguments() const
{
  return 3; // 2 + importfile
}
