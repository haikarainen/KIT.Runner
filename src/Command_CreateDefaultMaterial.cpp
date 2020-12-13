
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
  return "create_material";
}

bool Command_CreateDefaultMaterial::execute(std::vector<std::string> args) const
{
  auto inputFile = wir::File(args[2]);
  if (!inputFile.exist())
  {
    LogError("Input file does not exist!");
    return false;
  }

  if (inputFile.extension() != ".material")
  {
    LogError("Input file is not a material spec");
  }


  auto importBase = inputFile.directory().path();
  auto outputFile = importBase + "/" + inputFile.basename() + ".asset";

  wir::XMLDocument document;
  wir::XMLParser parser;
  if (!parser.loadFromFile(inputFile.path(), document))
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

  if (root->name() != "Material")
  {
    LogError("invalid material spec");
    return false;
  }

  std::string className = "kit::DefaultMaterial";
  root->string("Class", className);


  std::map<std::string, std::string> textures;
  std::map<std::string, glm::vec4> vectors;
  
  for(auto child : root->children())
  {
    if (child->name() == "Texture")
    {
      std::string name;
      std::string value;

      child->string("name", name);
      child->string("path", value);

      textures[name] = value;
    }
    else if (child->name() == "Vector")
    {
      std::string name;
      glm::dvec4 value;

      child->string("name", name);
      child->decimal("x", value.x);
      child->decimal("y", value.y);
      child->decimal("z", value.z);
      child->decimal("w", value.w);

      vectors[name] = value;
    }

  }

  wir::Stream assetData;
  assetData << std::string(className);
  
  assetData << uint64_t(textures.size());
  for (auto tex : textures)
    assetData << tex.first << tex.second;

  assetData << uint64_t(vectors.size());
  for (auto vec : vectors)
    assetData << vec.first << vec.second;

  if (!utils::writeAsset(outputFile, "kit::Material", assetData))
  {
    LogError("writeAsset failed");
    return false;
  }

  return true;
}

uint64_t Command_CreateDefaultMaterial::requiredArguments() const
{
  return 3; 
}

