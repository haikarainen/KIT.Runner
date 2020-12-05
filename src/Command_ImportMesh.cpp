#include "Command_ImportMesh.hpp"

#include "KXFImporter_Assimp.hpp"

#include <KIT/FBX/FBXDocument.hpp>
#include <KIT/KXF/KXFDocument.hpp>

//#include <KIT/KXF/KXFImporter_KFBX.hpp>

#include <KIT/KXF/KXFMesh.hpp>

#include <WIR/Filesystem.hpp>
#include <WIR/Error.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

Command_ImportMesh::~Command_ImportMesh()
{

}

std::string const Command_ImportMesh::name() const
{
  return "import_mesh";
}

bool Command_ImportMesh::execute(std::vector<std::string> args) const
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

  auto importer = new KXF::Importer_Assimp();
  auto kxfDoc = new KXF::Document();

  LogNotice("Importing loaded FBX to KXF document in memory...");
  auto scene = importer->loadScene(inputFile.path());
  importer->execute(scene, kxfDoc);

  delete importer;

  LogNotice("Imported!");

  for (auto mesh : kxfDoc->meshes())
  {
    uint32_t i = 0;
    if (mesh->submeshes.size() > 1)
    {
      for (auto submesh : mesh->submeshes)
      {
        LogNotice("Exporting submesh %s_%u", mesh->name.c_str(), i);
        submesh->bakeToMesh(outputDir.path() + "/" + mesh->name + "Mesh_" + std::to_string(i++) + ".asset");
      }
    }
    else
    {
      LogNotice("Exporting submesh %s", mesh->name.c_str());
      mesh->submeshes[0]->bakeToMesh(outputDir.path() + "/" + mesh->name + ".asset");
    }
  }

  delete kxfDoc;

  return true;
}

uint64_t Command_ImportMesh::requiredArguments() const
{
  return 4; // 2 + inputfile + outputfile
}
