#include "Command_ImportMesh.hpp"

#include "KXFImporter_Assimp.hpp"

#include <KIT/FBX/FBXDocument.hpp>
#include <KIT/KXF/KXFDocument.hpp>

//#include <KIT/KXF/KXFImporter_KFBX.hpp>

#include <KIT/KXF/KXFAnimation.hpp>
#include <KIT/KXF/KXFMesh.hpp>
#include <KIT/KXF/KXFSkeleton.hpp>

#include <WIR/XML/XMLAttribute.hpp>
#include <WIR/XML/XMLDocument.hpp>
#include <WIR/XML/XMLElement.hpp>
#include <WIR/XML/XMLParser.hpp>

#include <WIR/Error.hpp>
#include <WIR/Filesystem.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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
    LogError("Input file does not exist!");
    return false;
  }

  auto importFile = inputFile.path();
  auto outputDir = inputFile.directory().path();

  if (inputFile.extension() != ".import")
  {
    importFile += ".import";

    if (!wir::File(importFile).exist())
    {
      auto inputBase = inputFile.directory().path() + "/" + inputFile.basename();

      std::string importData = wir::format("<Mesh SourceFile=\"%s\" Physics=\"1\" Skeleton=\"1\" Animations=\"1\" Index16=\"1\" Normal=\"1\" Tangent=\"1\" Bones=\"1\" TexCoord1=\"1\" TexCoord2=\"1\" TexCoord3=\"1\" TexCoord4=\"1\" />", inputFile.name().c_str());
      if (!wir::File(importFile).writeString(importData))
      {
        LogError("Failed to create import file");
        return false;
      }
    }
  }

  auto importBase = wir::File(importFile).directory().path();

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
    LogError("invalid texture spec");
    return false;
  }

  auto root = roots[0];

  if (root->name() != "Mesh")
  {
    LogError("invalid mesh spec");
    return false;
  }

  std::string sourceFile;
  if (!root->string("SourceFile", sourceFile))
  {
    LogError("No source file specified");
    return false;
  }

  auto sourceFilef = wir::File(importBase + "/" + sourceFile);
  if (!sourceFilef.exist())
  {
    LogError("Source file doesn't exist");
    return false;
  }

  KXF::VertexFlags globalVertexFlags = KXF::VF_None;
  KXF::IndexFlags globalIndexFlags = KXF::IF_None;

  bool normal = true;
  root->boolean("Normal", normal);
  if (normal)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_Normal);

  bool tangent = true;
  root->boolean("Tangent", tangent);
  if (tangent)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_Tangent);

  bool bones = true;
  root->boolean("Bones", bones);
  if (bones)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_Bones);

  bool tex1 = true;
  root->boolean("TexCoord1", tex1);
  if (tex1)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_TexCoords1);

  bool tex2 = true;
  root->boolean("TexCoord2", tex2);
  if (tex2)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_TexCoords2);

  bool tex3 = true;
  root->boolean("TexCoord3", tex3);
  if (tex3)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_TexCoords3);

  bool tex4 = true;
  root->boolean("TexCoord4", tex4);
  if (tex4)
    globalVertexFlags = KXF::VertexFlags(globalVertexFlags | KXF::VF_TexCoords4);

  bool index16 = true;
  root->boolean("Index16", index16);
  if (index16)
    globalIndexFlags = KXF::IndexFlags(globalIndexFlags | KXF::IF_Use16bit);

  bool physics = true;
  root->boolean("Physics", physics);

  bool skeleton = true;
  root->boolean("Skeleton", skeleton);

  bool animations = true;
  root->boolean("Animations", animations);

  std::map<std::string, std::string> mappings;

  for (auto child : root->children())
  {
    if (child->name() == "MaterialMapping")
    {
      std::string id = "";
      child->string("id", id);
      std::string path = "";
      child->string("path", path);

      mappings[id] = path;
    }
  }

  auto importer = new KXF::Importer_Assimp();
  auto kxfDoc = new KXF::Document();

  LogNotice("Importing loaded FBX to KXF document in memory...");
  auto scene = importer->loadScene(sourceFilef.path());
  importer->execute(scene, kxfDoc);

  delete importer;

  uint32_t i = 0;
  for (auto mesh : kxfDoc->meshes())
  {
    if (physics)
      mesh->generateTriangles();

    if (mesh->submeshes.size() > 1)
    {
      for (auto submesh : mesh->submeshes)
      {
        // global is what we request, submesh flags is what is supported
        /// Use the flags where both exist
        KXF::VertexFlags vflags = KXF::VertexFlags(submesh->vertexFlags & globalVertexFlags);
        KXF::IndexFlags iflags = KXF::IndexFlags(submesh->indexFlags & globalIndexFlags);

        std::string mat = submesh->materialPath;
        auto f = mappings.find(mat);
        if (f != mappings.end())
          mat = f->second;

        submesh->materialPath = mat;

        LogNotice("Exporting submesh %s_%u", mesh->name.c_str(), i);
        submesh->bakeToMesh(wir::format("%s/Mesh_%s_%u.asset", outputDir.c_str(), mesh->name.c_str(), i), vflags, iflags);
        if (physics)
          submesh->bakeToPhysicsMesh(wir::format("%s/PhysicsMesh_%s_%u.asset", outputDir.c_str(), mesh->name.c_str(), i));
        i++;
      }
    }
    else if (mesh->submeshes.size() > 0)
    {
      KXF::VertexFlags vflags = KXF::VertexFlags(mesh->submeshes[0]->vertexFlags & globalVertexFlags);
      KXF::IndexFlags iflags = KXF::IndexFlags(mesh->submeshes[0]->indexFlags & globalIndexFlags);

      std::string mat = mesh->submeshes[0]->materialPath;
      auto f = mappings.find(mat);
      if (f != mappings.end())
        mat = f->second;

      mesh->submeshes[0]->materialPath = mat;

      LogNotice("Exporting submesh %s", mesh->name.c_str());
      mesh->submeshes[0]->bakeToMesh(wir::format("%s/Mesh_%s.asset", outputDir.c_str(), mesh->name.c_str()), vflags, iflags);
      if (physics)
        mesh->submeshes[0]->bakeToPhysicsMesh(wir::format("%s/PhysicsMesh_%s.asset", outputDir.c_str(), mesh->name.c_str()));
      i++;
    }
  }

  if (skeleton)
    for (auto s : kxfDoc->skeletons())
    {
      LogNotice("Exporting skeleton %s", s->name.c_str());
      s->bakeToAsset(wir::format("%s/Skeleton_%s.asset", outputDir.c_str(), s->name.c_str()));
    }

  if (animations)
    for (auto animation : kxfDoc->animations())
    {
      LogNotice("Exporting animation %s", animation->name.c_str());
      animation->bakeToAsset(wir::format("%s/Animation_%s.asset", outputDir.c_str(), animation->name.c_str()));
    }

  delete kxfDoc;

  return true;
}

uint64_t Command_ImportMesh::requiredArguments() const
{
  return 3;
}
