
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


#include <iostream>
#include <string>
#include <map>
#include <vector>


int main(int argc, char **argv)
{

  std::map<std::string, Command*> commands;
  auto registerCommand = [&commands](Command* c)->void {
    commands[c->name()] = c;
  };

  registerCommand(new Command_CreateShaderModule());
  registerCommand(new Command_TestCompression());
  registerCommand(new Command_ImportMesh());
  registerCommand(new Command_ImportPhysicsMesh());
  registerCommand(new Command_ImportTexture());
  registerCommand(new Command_CreateDefaultMaterial());
  registerCommand(new Command_CreateEmptyMaterial());
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

  auto finder = commands.find(args[1]);
  if (finder == commands.end())
  {
    LogError("No such command, %s.", args[1].c_str());
    return 2;
  }

  if (args.size() != finder->second->requiredArguments())
  {
    LogError("Command %s requires %u arguments, but %u was given", args[1].c_str(), finder->second->requiredArguments(), args.size());
    return 3;
  }

  if (!finder->second->execute(args))
  {
    LogError("Command %s failed to execute.", args[1]);
    return 4;
  }

  LogNotice("Command executed successfully!");

  for (auto c : commands)
  {
    delete c.second;
  }

  return 0;
}