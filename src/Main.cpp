
#include "Command.hpp"
#include "Command_CreateShaderModule.hpp"
#include "Command_TestCompression.hpp"
#include "Command_ImportMesh.hpp"
#include "Command_ImportPhysicsMesh.hpp"
#include "Command_CreateDefaultMaterial.hpp"
#include "Command_CreateEmptyMaterial.hpp"
#include "Command_ImportTexture.hpp"
#include "Command_ImportFont.hpp"


#include <KIT/Engine.hpp>

#include <WIR/Error.hpp>

#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLParser.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLAttribute.hpp>

#include <iostream>
#include <string>
#include <map>
#include <vector>


int main(int argc, char **argv)
{

  std::map<std::string, Command*> commands;
  std::map<std::string, Command *> importers;
  auto registerCommand = [&commands, &importers](Command* c)->void {
    commands[c->name()] = c;

    if (!c->imports().empty())
      importers[c->imports()] = c;
  };

  registerCommand(new Command_CreateShaderModule());
  registerCommand(new Command_TestCompression());
  registerCommand(new Command_ImportMesh());
  registerCommand(new Command_ImportPhysicsMesh());
  registerCommand(new Command_CreateDefaultMaterial());
  registerCommand(new Command_CreateEmptyMaterial());
  registerCommand(new Command_ImportTexture());
  registerCommand(new Command_ImportFont());

  std::vector<std::string> args;
  for (int32_t i = 0; i < argc; i++)
  {
    args.push_back(argv[i]);
  }

  if (args.size() == 1)
  {
    auto engine = new kit::Engine();
    engine->run();

    delete engine;

    //std::cin.get();

    return 0;
  }

  Command *command = nullptr;

  if (args[1] == "import" && args.size() > 2)
  {
    wir::XMLDocument document;
    wir::XMLParser parser;
    if (!parser.loadFromFile(args[2], document))
    {
      LogError("Failed to parse xml");
      return 5;
    }

    auto roots = document.rootElements();
    if (roots.size() != 1)
    {
      LogError("invalid import file");
      return 7;
    }

    auto root = roots[0];
    auto finder = importers.find(root->name());
    if (finder == importers.end())
    {
      LogError("no such import entity");
      return 8;
    }

    command = finder->second;

  }
  else 
  {
    auto finder = commands.find(args[1]);
    if (finder == commands.end())
    {
      LogError("No such command, %s.", args[1].c_str());
      return 2;
    }

    command = finder->second;
  }



  if (args.size() != command->requiredArguments())
  {
    LogError("Command %s requires %u arguments, but %u was given", args[1].c_str(), command->requiredArguments(), args.size());
    return 3;
  }

  if (!command->execute(args))
  {
    LogError("Command %s failed to execute.", args[1]);
    return 4;
  }

  LogNotice("Command executed successfully!");

  for (auto c : commands)
  {
    delete c.second;
  }

  #if defined(KIT_DEBUG)
  std::cin.get();
  #endif

  return 0;
}