#include "Command_ImportPhysicsMesh.hpp"

#include <KIT/FBX/FBXDocument.hpp>
#include <KIT/KXF/KXFDocument.hpp>

#include <KIT/KXF/KXFImporter_KFBX.hpp>
#include <KIT/KXF/KXFMesh.hpp>

#include <WIR/Error.hpp>
#include <WIR/Filesystem.hpp>

Command_ImportPhysicsMesh::~Command_ImportPhysicsMesh()
{
}

std::string const Command_ImportPhysicsMesh::name() const
{
  return "import_physics_mesh";
}

bool Command_ImportPhysicsMesh::execute(std::vector<std::string> args) const
{
  auto inputFile = wir::File(args[2]);
  if (!inputFile.exist())
  {
    LogError("Input file does not exist (%s)", inputFile.path().c_str());
    return false;
  }

  auto outputPath = args[3];
  auto outputDir = wir::Directory(outputPath);
  if (!outputDir.exist() && !outputDir.create())
  {
    LogError("Failed to create path for output path (%s)", outputPath.c_str());
    return false;
  }

  auto importer = new KXF::Importer_KFBX();
  auto fbxDoc = new KFBX::Document();
  auto kxfDoc = new KXF::Document();

  LogNotice("Loading FBX file...");

  if (!fbxDoc->loadFromFile(inputFile.path()))
  {
    LogError("FBX load failed (%s)", inputFile.path().c_str());
    return false;
  }

  LogNotice("Importing loaded FBX to KXF document in memory...");
  importer->execute(fbxDoc, kxfDoc);

  LogNotice("Imported!");

  for (auto mesh : kxfDoc->meshes())
  {
    uint32_t i = 0;
    if (mesh->submeshes.size() > 1)
    {
      for (auto submesh : mesh->submeshes)
      {
        LogNotice("Exporting submesh %s_%u", mesh->name.c_str(), i);
        submesh->bakeToPhysicsMesh(outputDir.path() + "/" + mesh->name + "PhysicsMesh_" + std::to_string(i++) + ".asset");
      }
    }
    else
    {
      LogNotice("Exporting submesh %s", mesh->name.c_str());
      mesh->submeshes[0]->bakeToPhysicsMesh(outputDir.path() + "/" + mesh->name + "PhysicsMesh.asset");
    }
  }

  return true;
}

uint64_t Command_ImportPhysicsMesh::requiredArguments() const
{
  return 4; // 2 + inputfile + outputfile
}
