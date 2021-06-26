
#pragma once

#include <KIT/Export.hpp>

#include <WIR/Math.hpp>

#include <string>
#include <vector>

struct aiScene;

namespace Assimp
{
  class Importer;
}

namespace KXF
{
  class Document;

  /** Importer from assimp to KXFDocument */
  class Importer_Assimp
  {
  public:
    Importer_Assimp();
    virtual ~Importer_Assimp();

    void execute(aiScene const *inputScene, KXF::Document *outputDocument);
    aiScene const *loadScene(std::string const &filePath);

  protected:
    Assimp::Importer *m_importer = nullptr;
  };
} // namespace KXF