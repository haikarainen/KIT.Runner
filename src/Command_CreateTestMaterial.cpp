
#include "Command_CreateTestMaterial.hpp"
#include "Utils.hpp"

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>
#include <WIR/Math.hpp>
#include <WIR/Stream.hpp>


Command_CreateTestMaterial::~Command_CreateTestMaterial()
{

}

std::string const Command_CreateTestMaterial::name() const
{
  return "create_test_material";
}

bool Command_CreateTestMaterial::execute(std::vector<std::string> args) const
{
  auto texturePath = args[2];

  auto outputFile = wir::File(args[3]);
  if (!outputFile.createPath())
  {
    LogError("Failed to create path for output file (%s)", outputFile.path().c_str());
    return false;
  }

  wir::Stream instanceData;
  instanceData << texturePath;

  wir::Stream assetData;
  assetData << std::string("kit::TestForwardClass");
  assetData << uint64_t(instanceData.size());
  assetData.write(instanceData.begin(), instanceData.size());

  if (!utils::writeAsset(outputFile.path(), "kit::MaterialDefinition", assetData))
  {
    LogError("writeAsset failed");
    return false;
  }

  return true;
}

uint64_t Command_CreateTestMaterial::requiredArguments() const
{
  return 4; // 2 + texturepath  + outputfile
}
