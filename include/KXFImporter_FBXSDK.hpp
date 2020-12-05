
#ifdef KIT_USE_FBXSDK
#pragma once 

#include <KIT/Export.hpp>

#include <WIR/Math.hpp>

#include <vector>
#include <set>

namespace fbxsdk {
  class FbxManager;
  class FbxIOSettings;
  class FbxImporter;
  class FbxScene;
  class FbxCluster;
};


namespace KXF
{
  class Document;

  /** Importer from FBXSDK to KXFDocument */
  class Importer_FBXSDK
  {
  public:
    Importer_FBXSDK();
    ~Importer_FBXSDK();

    void importFromFile(std::string const & fbxFilePath, KXF::Document * outputDocument);

  protected:

    void importGeometry();
    void importSkeletons();
    void importAnimations();

    KXFDocument *m_output = nullptr;

    fbxsdk::FbxManager *m_fbxManager = nullptr;
    fbxsdk::FbxIOSettings *m_fbxIOSettings = nullptr;
    fbxsdk::FbxImporter *m_fbxImporter = nullptr;
    fbxsdk::FbxScene *m_fbxScene = nullptr;

    std::set<fbxsdk::FbxCluster*> m_usedBones;
  };
}
#endif