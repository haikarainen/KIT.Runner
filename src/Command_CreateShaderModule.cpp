#include "Command_CreateShaderModule.hpp"

#include "Utils.hpp"

#include <WIR/Async.hpp>
#include <WIR/Error.hpp>
#include <WIR/Filesystem.hpp>
#include <WIR/Stream.hpp>
#include <WIR/String.hpp>

std::string const Command_CreateShaderModule::name() const
{
  return "create_shader_module";
}

uint64_t Command_CreateShaderModule::requiredArguments() const
{
  return 5;
}

bool Command_CreateShaderModule::execute(std::vector<std::string> args) const
{
  auto inputFile = wir::File(args[3]);
  if (!inputFile.exist())
  {
    LogError("Input file does not exist (%s)", inputFile.path().c_str());
    return false;
  }

  std::string outputSpv = inputFile.path() + ".spv";
  std::string type = wir::strToLower(args[2]);

  std::string vulkanExe = utils::getVulkanSDKPath() + "\\Bin\\glslangValidator.exe";

  wir::AsyncProcess compiler(vulkanExe, {vulkanExe, "-V", "-S", type, "-o", outputSpv, inputFile.path()});
  compiler.runSynchronously();
  if (compiler.processStatus() == wir::AsyncProcessStatus::PS_Success && compiler.returnCode() != 0)
  {
    LogError("Compiler failed (returncode %i)", compiler.returnCode());
    return false;
  }

  LogNotice("Compiled SPIR-V...");

  wir::Stream dataStream;

  /*
  VK_SHADER_STAGE_VERTEX_BIT = 0x00000001,
  VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT = 0x00000002,
  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT = 0x00000004,
  VK_SHADER_STAGE_GEOMETRY_BIT = 0x00000008,
  VK_SHADER_STAGE_FRAGMENT_BIT = 0x00000010,
  VK_SHADER_STAGE_COMPUTE_BIT = 0x00000020,
  */

  if (type == "frag")
  {
    dataStream << int64_t(0x00000010);
  }
  else if (type == "vert")
  {
    dataStream << int64_t(0x00000001);
  }
  else if (type == "tesc")
  {
    dataStream << int64_t(0x00000002);
  }
  else if (type == "tese")
  {
    dataStream << int64_t(0x00000004);
  }
  else if (type == "geom")
  {
    dataStream << int64_t(0x00000008);
  }
  else if (type == "comp")
  {
    dataStream << int64_t(0x00000020);
  }
  else
  {
    LogError("Invalid shader stage, valid stages are; frag, vert, tesc, tese, geom, comp");
    return false;
  }

  wir::Stream fileStream;
  if (!fileStream.readFile(outputSpv))
  {
    LogError("Failed to load newly compiled SPIR-V file");
    return false;
  }

  dataStream << uint64_t(fileStream.size());
  dataStream << fileStream;

  if (!utils::writeAsset(args[4], "kit::ShaderModule", dataStream))
  {
    LogError("utils::writeAsset failed to write asset (%s)", args[3].c_str());
    return false;
  }

  return true;
}
