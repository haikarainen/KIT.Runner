
#ifdef KIT_USE_FBXSDK
#include "KXFImporter_FBXSDK.hpp"
#include <KIT/KXF/KXFAnimation.hpp>
#include <KIT/KXF/KXFDocument.hpp>
#include <KIT/KXF/KXFMesh.hpp>
#include <KIT/KXF/KXFSkeleton.hpp>

#include <KIT/Common/Error.hpp>
#include <KIT/Common/Function.hpp>
#include <KIT/Common/String.hpp>
#include <KIT/Common/Utility.hpp>

#include <fbxsdk.h>
#include <queue>

KXFImporter_FBXSDK::KXFImporter_FBXSDK()
{
  m_fbxManager = FbxManager::Create();
  m_fbxIOSettings = FbxIOSettings::Create(m_fbxManager, IOSROOT);
  m_fbxManager->SetIOSettings(m_fbxIOSettings);

  m_fbxImporter = FbxImporter::Create(m_fbxManager, "");

  m_fbxScene = FbxScene::Create(m_fbxManager, "ImportScene");
}

KXFImporter_FBXSDK::~KXFImporter_FBXSDK()
{
  m_fbxScene->Destroy();
  m_fbxImporter->Destroy();
  m_fbxIOSettings->Destroy();
  m_fbxManager->Destroy();
}

void KXFImporter_FBXSDK::importFromFile(std::string const &fbxFilePath, class KXFDocument *outputDocument)
{
  m_output = outputDocument;
  if (!m_fbxImporter->Initialize(fbxFilePath.c_str(), -1, m_fbxIOSettings))
  {
    KLog() << "KXFImporter (FBXSDK): Error: " << m_fbxImporter->GetStatus().GetErrorString() << "\n";
    return;
  }

  m_fbxImporter->Import(m_fbxScene);

  KLog() << "KXFImporter (FBXSDK): Imported scene from file.\n";

  auto *converter = new FbxGeometryConverter(m_fbxManager);
  converter->RemoveBadPolygonsFromMeshes(m_fbxScene);

  // If skins are missing, replace this with the per-node variant:
  converter->Triangulate(m_fbxScene, true);
  KDELETE(converter);

  KLog() << "KXFImporter (FBXSDK): Cleaned up and triangulated all meshes.\n";

  importGeometry();
  importSkeletons();
  importAnimations();
}

void KXFImporter_FBXSDK::importGeometry()
{
  // @todo clean this shithole function up
  // split up into submethods, create methods from lambdas and move persistent variables to member variables
  int32_t numMesh = m_fbxScene->GetSrcObjectCount(FbxCriteria::ObjectType(FbxMesh::ClassId));

  for (int32_t iMesh = 0; iMesh < numMesh; iMesh++)
  {
    FbxMesh *currMesh = m_fbxScene->GetSrcObject<FbxMesh>(iMesh);
    uint32_t currMeshBoneIndex = 0;
    int32_t meshInvalidPolyCount = 0;
    int32_t meshSkippedWeights = 0;
    bool meshHasNormals = currMesh->GetElementNormalCount() > 0;
    bool meshHasUvs = currMesh->GetUVLayerCount() > 0;

    if (!meshHasUvs)
    {
      KLog() << "KXFImporter (FBXSDK): Mesh lacks UV's, refuses to import: \"" << currMesh->GetName() << "\"\n";
      continue;
    }

    KLog() << "KXFImporter (FBXSDK): Importing mesh \"" << currMesh->GetName() << "\"\n";

    KXFMesh *outMesh = new KXFMesh();
    outMesh->name = currMesh->GetName();
    m_output->addMesh(outMesh);

    // Find material index
    FbxLayerElementArrayTemplate<int> *materialIndex = nullptr;
    for (int32_t l = 0; l < currMesh->GetLayerCount(); l++)
    {
      FbxLayerElementMaterial *currMaterials = currMesh->GetLayer(l)->GetMaterials();
      if (currMaterials)
      {
        //currMaterials->SetReferenceMode(FbxLayerElement::eIndexToDirect);
        materialIndex = &currMaterials->GetIndexArray();
        break;
      }
    }

    // Maps from polygon id to material id
    auto getPolygonMaterial = [&](int32_t polyId) -> int32_t {
      if (materialIndex != nullptr)
      {
        return (*materialIndex)[polyId];
      }
      else
      {
        return -1;
      }
    };

    // Maps from material id to submesh
    std::map<int32_t, KXFSubmesh *> materialToSubmeshMap;
    auto getSubmesh = [&](int32_t materialIndex) -> KXFSubmesh * {
      auto finder = materialToSubmeshMap.find(materialIndex);
      if (finder == materialToSubmeshMap.end())
      {
        KXFSubmesh *newSubmesh = new KXFSubmesh();
        newSubmesh->materialPath = materialIndex >= 0 ? currMesh->GetNode()->GetMaterial(materialIndex)->GetName() : "DefaultMaterial";
        outMesh->submeshes.push_back(newSubmesh);
        newSubmesh->mesh = outMesh;
        materialToSubmeshMap[materialIndex] = newSubmesh;

        return newSubmesh;
      }

      return finder->second;
    };

    // Get the control points
    FbxVector4 *controlPoints = currMesh->GetControlPoints();
    int32_t numControlPoints = currMesh->GetControlPointsCount();

    // Get the UV sets, we need to use the first one
    FbxStringList uvSetNames;
    currMesh->GetUVSetNames(uvSetNames);
    if (uvSetNames.GetCount() > 1)
    {
      KLog() << "KXFImporter (FBXSDK): Notice: Multiple UV sets. Using UV set \"" << uvSetNames.GetStringAt(0) << "\"\n";
    }

    // Maps old control-point indices to new vertex-indices for a given submesh
    // Note that the new can have duplicates of the old (and most do), so we store a vector to the new
    std::map<int32_t, std::map<KXFSubmesh *, std::vector<uint32_t>>> oldToNewMap;

    // Iterate through all the FBX polygons and their vertices to generate the KXF vertices and indices
    // Dont worry about duplicates here, we will remove those in a later step.
    int32_t numPolygons = currMesh->GetPolygonCount();
    for (int32_t currPolygon = 0; currPolygon < numPolygons; currPolygon++)
    {
      // Only import polygons with exactly 3 vertices, since we triangulate and clean up the FBX meshes beforehand
      int32_t numVertices = currMesh->GetPolygonSize(currPolygon);
      if (numVertices != 3)
      {
        meshInvalidPolyCount++;
        continue;
      }

      // Find the submesh for this polygon (based on its material), and iterate through the polygons vertices
      KXFSubmesh *currSubmesh = getSubmesh(getPolygonMaterial(currPolygon));
      for (int32_t currVertex = 0; currVertex < numVertices; currVertex++)
      {
        // Create a new KXF vertex for EVERY FBX polygon-vertex
        KXFVertex newVertex;

        // Get the controlpoint index to this polygon-vertex
        int32_t vertexIndex = currMesh->GetPolygonVertex(currPolygon, currVertex);

        // Copy the vertex position data
        FbxVector4 vertexPosition = controlPoints[vertexIndex];
        newVertex.position.x = vertexPosition[0];
        newVertex.position.y = vertexPosition[1];
        newVertex.position.z = vertexPosition[2];

        // Copy the vertex normal data, if it has them
        if (meshHasNormals)
        {
          FbxVector4 vertexNormal;
          currMesh->GetPolygonVertexNormal(currPolygon, currVertex, vertexNormal);
          newVertex.normal.x = vertexNormal[0];
          newVertex.normal.y = vertexNormal[1];
          newVertex.normal.z = vertexNormal[2];
        }

        // Copy the vertex UV data, if it has them
        if (meshHasUvs && uvSetNames.GetCount() > 0)
        {
          FbxVector2 vertexUV;
          bool uvUnmapped = false;
          currMesh->GetPolygonVertexUV(currPolygon, currVertex, uvSetNames.GetStringAt(0), vertexUV, uvUnmapped);
          newVertex.uv.x = vertexUV[0];
          newVertex.uv.y = vertexUV[1];
        }

        // Add the newly created KXF vertex to the vertexbuffer
        currSubmesh->vertices.push_back(newVertex);

        // Add an index for the newly created vertex
        currSubmesh->indices.push_back(currSubmesh->vertices.size() - 1);

        // Add it in the oldtonew, for later resolvement of bones and indices
        oldToNewMap[vertexIndex][currSubmesh].push_back(currSubmesh->vertices.size() - 1);
      }
    }

    // Resolve the bones and indices
    int32_t numSkins = currMesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int32_t currSkin = 0; currSkin < numSkins; currSkin++)
    {
      // Reintepret cast here because fbx is sadistic
      FbxSkin *skin = reinterpret_cast<FbxSkin *>(currMesh->GetDeformer(currSkin, FbxDeformer::eSkin));

      int32_t numClusters = skin->GetClusterCount();
      for (int32_t currCluster = 0; currCluster < numClusters; currCluster++)
      {
        FbxCluster *cluster = skin->GetCluster(currCluster);

        int32_t numIndices = cluster->GetControlPointIndicesCount();
        int32_t *indices = cluster->GetControlPointIndices();
        double *weights = cluster->GetControlPointWeights();
        if (numIndices > 0 && indices != nullptr && weights != nullptr)
        {
          std::string clusterName = cluster->GetName();
          uint32_t clusterIndex = 0;
          auto boneIndexFinder = outMesh->boneIndex.find(clusterName);
          if (boneIndexFinder == outMesh->boneIndex.end())
          {
            clusterIndex = currMeshBoneIndex++;
            outMesh->boneIndex[clusterName] = clusterIndex;
          }
          else
          {
            clusterIndex = boneIndexFinder->second;
          }

          m_usedBones.insert(cluster);

          for (uint32_t currCpIndex = 0; currCpIndex < numIndices; currCpIndex++)
          {
            float currWeight = weights[currCpIndex];

            std::map<KXFSubmesh *, std::vector<uint32_t>> &oldToNew = oldToNewMap[indices[currCpIndex]];
            for (auto thisCpData : oldToNew)
            {
              for (auto newCpVertex : thisCpData.second)
              {
                KXFVertex *currVertex = &thisCpData.first->vertices[newCpVertex];
                if (currVertex->weights.x == 0.0f)
                {
                  currVertex->weights.x = currWeight;
                  currVertex->bones.x = clusterIndex;
                }
                else if (currVertex->weights.y == 0.0f)
                {
                  currVertex->weights.y = currWeight;
                  currVertex->bones.y = clusterIndex;
                }
                else if (currVertex->weights.z == 0.0f)
                {
                  currVertex->weights.z = currWeight;
                  currVertex->bones.z = clusterIndex;
                }
                else if (currVertex->weights.w == 0.0f)
                {
                  currVertex->weights.w = currWeight;
                  currVertex->bones.w = clusterIndex;
                }
                else
                {
                  // No empty weights, so skip any further ones.
                  // Keep track of them, because if the number gets really high,
                  // it might be an indicator that we're doing things the wrong way
                  meshSkippedWeights++;
                }
              }
            }
          }
        }
      }
    }

    // Spit out a warning message if we had any invalid polygons
    if (meshInvalidPolyCount > 0)
    {
      KLog() << "KXFImporter (FBXSDK): Warning: Mesh had non-triangulated polygons, despite triangulation and cleanup. Number of invalid polygons found (and skipped): " << meshInvalidPolyCount << "\n";
    }

    // Spit out a warning message if we had any skipped weights
    if (meshSkippedWeights > 0)
    {
      KLog() << "KXFImporter (FBXSDK): Warning: Mesh skipped weights as there were more than 4 weights assigned to some vertices. Number of weights skipped: " << meshSkippedWeights << "\n";
    }

    // Remove duplicate vertices in the newly generated vertex buffer and update the index buffer to match it
    outMesh->removeDuplicates();

    // If the mesh lacked normals, generate new ones
    if (!meshHasNormals)
    {
      outMesh->calculateNormals();
    }

    // Also generate tangents based on the UV's (meshes that lacks UV's are not imported for reasons of which this is one)
    outMesh->calculateTangents();
  }
}

void KXFImporter_FBXSDK::importSkeletons()
{

  auto isNodeSkeletal = [](FbxNode *inNode) {
    return inNode->GetNodeAttribute() &&
           inNode->GetNodeAttribute()->GetAttributeType() &&
           inNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton;
  };

  // Go through the used clusters in the queue, and find their roots
  std::map<std::string, std::set<FbxNode *>> skeletons;
  for (FbxCluster *c : m_usedBones)
  {
    FbxNode *cl = c->GetLink();
    while (cl->GetParent() && isNodeSkeletal(cl->GetParent()))
    {
      cl = cl->GetParent();
    }

    std::string skeletonName = "UndefinedSkeleton";
    if (cl->GetParent())
    {
      skeletonName = cl->GetParent()->GetName();
    }

    skeletons[skeletonName].insert(cl);
  }

  for (auto skeleton : skeletons)
  {
    std::string skeletonName = skeleton.first;
    std::set<FbxNode *> &rootNodes = skeleton.second;

    KXFSkeleton *newSkeleton = new KXFSkeleton();
    newSkeleton->name = skeletonName;
    m_output->addSkeleton(newSkeleton);

    KFunction<void(FbxNode *, KXFBone *)> addChildren = KFunction<void(FbxNode *, KXFBone *)>([&](FbxNode *node, KXFBone *parent) {
      for (int32_t i = 0; i < node->GetChildCount(); i++)
      {
        FbxNode *currChild = node->GetChild(i);
        if (!isNodeSkeletal(currChild))
        {
          continue;
        }

        KXFBone *newBone = new KXFBone();
        newBone->name = currChild->GetName();
        newBone->parent = parent;
        parent->children.push_back(newBone);
        newSkeleton->bones.push_back(newBone);

        //newBone->initialTransform = ..
        //newBone->inverseBindPose = ..

        addChildren(currChild, newBone);
      }
    });

    for (auto rootNode : rootNodes)
    {
      KXFBone *newRootBone = new KXFBone();
      newRootBone->name = rootNode->GetName();

      /*
      rootNode->GetGeometricTranslation();
      rootNode->GetGeometricRotation();
      rootNode->GetGeometricScaling();
      rootNode->GetTransform();
      rootNode->GetPivots();
      */
      //newRootBone->initialTransform =
      // newRootBone->initialTransform = ..
      // newRootBone->inverseBindPose = ..
      newSkeleton->bones.push_back(newRootBone);
      newSkeleton->rootBones.push_back(newRootBone);

      addChildren(rootNode, newRootBone);
    }
  }
}

void KXFImporter_FBXSDK::importAnimations()
{
  int32_t numAnimStacks = m_fbxScene->GetSrcObjectCount(FbxCriteria::ObjectType(FbxAnimStack::ClassId));
  for (int32_t animStack = 0; animStack < numAnimStacks; animStack++)
  {
    FbxAnimStack *currAnimStack = m_fbxScene->GetSrcObject<FbxAnimStack>(FbxCriteria::ObjectType(FbxAnimStack::ClassId), animStack);

    int32_t numLayers = currAnimStack->GetSrcObjectCount(FbxCriteria::ObjectType(FbxAnimLayer::ClassId));
    for (int32_t layer = 0; layer < numLayers; layer++)
    {
      FbxAnimLayer *currLayer = currAnimStack->GetSrcObject<FbxAnimLayer>(FbxCriteria::ObjectType(FbxAnimLayer::ClassId), layer);
      KLog() << "Found animation " << currLayer->GetName() << "\n";

      std::string animName = currLayer->GetName();
      std::vector<std::string> parts = splitString(animName, {'|'});
      if (parts.size() == 2)
      {
        animName = parts[1];
      }

      KXFAnimation *newAnimation = new KXFAnimation();
      newAnimation->name = animName;
      m_output->addAnimation(newAnimation);
      float maxDuration = 0.0f;

      for (auto bone : m_usedBones)
      {
        KXFAnimationChannel *newChannel = new KXFAnimationChannel();
        newChannel->name = bone->GetName();
        newAnimation->channels.push_back(newChannel);

        auto boneNode = bone->GetLink();

        auto addTrackKeysFromCurve = [&](FbxPropertyT<FbxDouble3> &property, char const *name, std::function<void(float time, glm::vec3 value)> func) {
          auto curveNode = property.GetCurveNode(currLayer);
          auto curve = property.GetCurve(currLayer, name);

          int32_t numKeys = curve->KeyGetCount();
          for (int32_t currKey = 0; currKey < numKeys; currKey++)
          {
            FbxAnimCurveKey key = curve->KeyGet(currKey);
            double newDoubles[4] = {0.0, 0.0, 0.0, 0.0};
            FbxAnimCurveNode::EvaluateChannels(curveNode, &newDoubles[0], 3, key.GetTime());
            float keyTime = float(key.GetTime().GetMilliSeconds()) / 1000.0f;
            glm::vec3 keyValue(newDoubles[0], newDoubles[1], newDoubles[2]);
            func(keyTime, keyValue);

            if (keyTime > maxDuration)
            {
              maxDuration = keyTime;
            }
          }
        };

        auto addTrackKeysVec3 = [&](FbxPropertyT<FbxDouble3> &property, std::function<void(float time, glm::vec3 value)> func) {
          addTrackKeysFromCurve(property, FBXSDK_CURVENODE_COMPONENT_X, func);
          addTrackKeysFromCurve(property, FBXSDK_CURVENODE_COMPONENT_Y, func);
          addTrackKeysFromCurve(property, FBXSDK_CURVENODE_COMPONENT_Z, func);
        };

        // Add translation track and keys
        KXFAnimationTrack<glm::vec3> *translationTrack = new KXFAnimationTrack<glm::vec3>();
        translationTrack->name = "Translation";
        newChannel->tracks.push_back(translationTrack);

        addTrackKeysVec3(boneNode->LclTranslation, [&](float time, glm::vec3 value) {
          translationTrack->getKeyAt(time).value = value;
          //KLog() << newChannel->name << ": Translation at " << time << ": " << value << "\n";
        });

        // Add rotation track and keys
        KXFAnimationTrack<glm::quat> *rotationTrack = new KXFAnimationTrack<glm::quat>();
        rotationTrack->name = "Rotation";
        newChannel->tracks.push_back(rotationTrack);

        addTrackKeysVec3(boneNode->LclRotation, [&](float time, glm::vec3 value) {
          glm::quat newQuat;

          FbxEuler::EOrder order;
          boneNode->GetRotationOrder(FbxNode::eSourcePivot, order);

          switch (order)
          {
          case FbxEuler::eOrderXYZ:
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            break;
          case FbxEuler::eOrderXZY:
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            break;
          case FbxEuler::eOrderYZX:
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            break;
          case FbxEuler::eOrderYXZ:
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            break;
          case FbxEuler::eOrderZXY:
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            break;
          case FbxEuler::eOrderZYX:
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            break;
          case FbxEuler::eOrderSphericXYZ:
            KLog() << "KXF Importer (FBXSDK): Unsupported node rotation eOrderSphericXYZ specified for animated node. Animations will be broken.\n";
            newQuat = glm::rotate(newQuat, glm::radians(value.x), glm::vec3(1.0f, 0.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.y), glm::vec3(0.0f, 1.0f, 0.0f));
            newQuat = glm::rotate(newQuat, glm::radians(value.z), glm::vec3(0.0f, 0.0f, 1.0f));
            break;
          }

          //KLog() << newChannel->name << ": Rotation at " << time << ": " << value << "\n";

          rotationTrack->getKeyAt(time).value = value;
        });

        // Add scale track and keys
        KXFAnimationTrack<glm::vec3> *scaleTrack = new KXFAnimationTrack<glm::vec3>();
        scaleTrack->name = "Scale";
        newChannel->tracks.push_back(scaleTrack);

        addTrackKeysVec3(boneNode->LclScaling, [&](float time, glm::vec3 value) {
          scaleTrack->getKeyAt(time).value = value;
          //KLog() << newChannel->name << ": Scale at " << time << ": " << value << "\n";
        });
      }

      newAnimation->duration = maxDuration;
    }
  }
}

#endif