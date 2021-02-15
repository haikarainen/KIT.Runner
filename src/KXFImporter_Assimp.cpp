
#include "KXFImporter_Assimp.hpp"
#include <KIT/KXF/KXFSkeleton.hpp>
#include <KIT/KXF/KXFMesh.hpp>
#include <KIT/KXF/KXFAnimation.hpp>
#include <KIT/KXF/KXFDocument.hpp>

#include <WIR/Math.hpp>

#include <WIR/glm/gtx/matrix_decompose.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <map>
#include <cstdint>
#include <vector>



glm::mat4 glmMat4(const aiMatrix4x4 & from)
{
  glm::mat4 to;
  to[0][0] = from.a1;
  to[1][0] = from.a2;
  to[2][0] = from.a3;
  to[3][0] = from.a4;

  to[0][1] = from.b1;
  to[1][1] = from.b2;
  to[2][1] = from.b3;
  to[3][1] = from.b4;

  to[0][2] = from.c1;
  to[1][2] = from.c2;
  to[2][2] = from.c3;
  to[3][2] = from.c4;

  to[0][3] = from.d1;
  to[1][3] = from.d2;
  to[2][3] = from.d3;
  to[3][3] = from.d4;

  return to;
}

KXF::Importer_Assimp::Importer_Assimp()
{
  m_importer = new Assimp::Importer();
  m_importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
}

KXF::Importer_Assimp::~Importer_Assimp()
{
  delete m_importer;
}

void flip(glm::vec4& inVec)
{
  inVec = -inVec;
}

void blenderToKxf(glm::vec3 &inVec)
{

  glm::vec3 newVec = inVec;
  inVec.x = newVec.x;
  inVec.y = -newVec.z;
  inVec.z = newVec.y;

}

void blenderToKxfScale(glm::vec3 &inVec)
{

  glm::vec3 newVec = inVec;
  inVec.x = newVec.x;
  inVec.y = newVec.z;
  inVec.z = newVec.y;
}


void blenderToKxfUv(glm::vec4& inVec)
{

  glm::vec4 newVec = inVec;
  inVec.x = newVec.x;
  inVec.y = 1.0f-newVec.y;

}

void blenderToKxf(glm::vec4 &inVec)
{

  glm::vec4 newVec = inVec;
  inVec.x = newVec.x;
  inVec.y = -newVec.z;
  inVec.z = newVec.y;
}

void blenderToKxf(KXF::Vertex &inVertex)
{

  blenderToKxf(inVertex.normal);
  blenderToKxf(inVertex.tangent);
  blenderToKxf(inVertex.position);
  blenderToKxfUv(inVertex.texCoords1);
  blenderToKxfUv(inVertex.texCoords2);
  blenderToKxfUv(inVertex.texCoords3);
  blenderToKxfUv(inVertex.texCoords4);

  flip(inVertex.normal);
}


void blenderToKxf(glm::quat &inQuat)
{

  glm::quat newQuat = inQuat;
  inQuat.x = newQuat.x;
  inQuat.y = -newQuat.z;
  inQuat.z = newQuat.y;
  inQuat.w = newQuat.w;

  // @todo possibly need to negate xyz as a final step here
  //inQuat.x = -inQuat.x;
  //inQuat.y = -inQuat.y;
  //inQuat.z = -inQuat.z;
}

void blenderToKxf(glm::mat4 &mat)
{
  glm::mat4 matrix = mat;
  glm::vec3 scale;
  glm::quat rotation;
  glm::vec3 translation;
  glm::vec3 skew;
  glm::vec4 perspective;
  glm::decompose(matrix, scale, rotation, translation, skew, perspective);

  blenderToKxf(translation);
  blenderToKxf(rotation);
  blenderToKxfScale(scale);

  auto r = glm::toMat4(rotation);
  auto t = glm::translate(glm::mat4(1.0f), translation);
  auto s = glm::scale(glm::mat4(1.f), scale);

  mat = t * r * s;
}




void KXF::Importer_Assimp::execute(aiScene const *inputScene, KXF::Document *outputDocument)
{
  /*
  Get a scene by something like this:
  Assimp::Importer importer;
  importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
  importer.ReadFile(inputFilePath, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_ValidateDataStructure | aiProcess_ImproveCacheLocality);
  */
  
  double factor(0.0);
  inputScene->mMetaData->Get("UnitScaleFactor", factor);

  // Get the global inverse transform for the entire scene 
  auto git = glmMat4(inputScene->mRootNode->mTransformation);
  blenderToKxf(git);
  glm::mat4 globalInverseTransform = glm::inverse(git);

  // Temporary state holders for meshes
  std::map<std::string, KXF::Mesh*> meshes;

  // Potentially a new skeleton
  KXF::Skeleton *newSkeleton = new KXF::Skeleton();
  std::map<KXF::Bone*, std::string> parentNames;

  // Iterate through all the meshes to add them
  for (unsigned int currMesh = 0; currMesh < inputScene->mNumMeshes; currMesh++)
  {

    std::map<KXF::Bone *, aiBone *> assimpBoneIndex;
    KXF::Submesh *newSubmesh = new KXF::Submesh();

    aiMesh* currMeshPtr = inputScene->mMeshes[currMesh];
    uint32_t vertexCount = currMeshPtr->mNumVertices;
    uint32_t triangleCount = currMeshPtr->mNumFaces;

    // Get the submesh material
    aiString tempMaterial;
    inputScene->mMaterials[currMeshPtr->mMaterialIndex]->Get(AI_MATKEY_NAME, tempMaterial);
    newSubmesh->materialPath = tempMaterial.C_Str();
    if (newSubmesh->materialPath.size() == 0)
    {
      newSubmesh->materialPath = "Core/Default/Material";
    }

    std::string currMeshName = currMeshPtr->mName.C_Str();

    newSubmesh->vertexFlags = KXF::VF_None;
    if (currMeshPtr->mTextureCoords[0] != nullptr)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_TexCoords1);
    if (currMeshPtr->mTextureCoords[1] != nullptr)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_TexCoords2);
    if (currMeshPtr->mTextureCoords[2] != nullptr)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_TexCoords3);
    if (currMeshPtr->mTextureCoords[3] != nullptr)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_TexCoords4);
    if (currMeshPtr->mNormals != nullptr)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_Normal);
    if (currMeshPtr->mTangents != nullptr)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_Tangent);

    // Fill vertices
    for (unsigned int currVertex = 0; currVertex < vertexCount; currVertex++)
    {
      KXF::Vertex currVertexData;

      currVertexData.position.x = currMeshPtr->mVertices[currVertex].x;
      currVertexData.position.y = currMeshPtr->mVertices[currVertex].y;
      currVertexData.position.z = currMeshPtr->mVertices[currVertex].z;
      currVertexData.position.w = 1.0f;

      if (currMeshPtr->mTextureCoords[0] != nullptr)
      {
        currVertexData.texCoords1.x = currMeshPtr->mTextureCoords[0][currVertex].x;
        currVertexData.texCoords1.y = currMeshPtr->mTextureCoords[0][currVertex].y;
      }

      if (currMeshPtr->mTextureCoords[1] != nullptr)
      {
        currVertexData.texCoords2.x = currMeshPtr->mTextureCoords[1][currVertex].x;
        currVertexData.texCoords2.y = currMeshPtr->mTextureCoords[1][currVertex].y;
      }

      if (currMeshPtr->mTextureCoords[2] != nullptr)
      {
        currVertexData.texCoords3.x = currMeshPtr->mTextureCoords[2][currVertex].x;
        currVertexData.texCoords3.y = currMeshPtr->mTextureCoords[2][currVertex].y;
      }

      if (currMeshPtr->mTextureCoords[3] != nullptr)
      {
        currVertexData.texCoords4.x = currMeshPtr->mTextureCoords[3][currVertex].x;
        currVertexData.texCoords4.y = currMeshPtr->mTextureCoords[3][currVertex].y;
      }

      if (currMeshPtr->mNormals != nullptr)
      {
        currVertexData.normal.x = currMeshPtr->mNormals[currVertex].x;
        currVertexData.normal.y = currMeshPtr->mNormals[currVertex].y;
        currVertexData.normal.z = currMeshPtr->mNormals[currVertex].z;
      }

      if (currMeshPtr->mTangents != nullptr)
      {
        currVertexData.tangent.x = currMeshPtr->mTangents[currVertex].x;
        currVertexData.tangent.y = currMeshPtr->mTangents[currVertex].y;
        currVertexData.tangent.z = currMeshPtr->mTangents[currVertex].z;
      }

      // @todo: make this optional
      blenderToKxf(currVertexData);

      newSubmesh->vertices.push_back(currVertexData);
    }

    // Fill faces
    for (unsigned int currTriangle = 0; currTriangle < triangleCount; currTriangle++)
    {
      aiFace *currFacePtr = &currMeshPtr->mFaces[currTriangle];
      if (currFacePtr->mNumIndices == 3)
      {
        newSubmesh->indices.push_back(currFacePtr->mIndices[0]);
        newSubmesh->indices.push_back(currFacePtr->mIndices[1]);
        newSubmesh->indices.push_back(currFacePtr->mIndices[2]);
      }
    }

    if (newSubmesh->indices.size() < 65536)
      newSubmesh->indexFlags = KXF::IndexFlags(newSubmesh->indexFlags | IF_Use16bit);


    // --- SKELETON STUFF BEGINS HERE
    // Initialize a cache that helps us keep track of how many weights each vertex has
    std::map<uint32_t, uint32_t> vertexWeightCount;
    for (uint32_t i = 0; i < newSubmesh->vertices.size(); i++)
    {
      vertexWeightCount[i] = 0;
    }

    std::map<std::string, uint32_t> meshBoneIndex;

    // Add bones from this mesh
    for (unsigned int currBone = 0; currBone < currMeshPtr->mNumBones; currBone++)
    {
      if (newSkeleton->name == "")
        newSkeleton->name = currMeshName;

      aiBone* currBonePtr = currMeshPtr->mBones[currBone];
      aiNode* currBoneNode = inputScene->mRootNode->FindNode(currBonePtr->mName.C_Str());
      std::string boneName = currBonePtr->mName.C_Str();

      if (!currBoneNode)
      {
        continue;
      }

      aiNode* parentBoneNode = currBoneNode->mParent;
      std::string parentBoneName = "";
      if (parentBoneNode)
      {
        parentBoneName = parentBoneNode->mName.C_Str();
      }

      // If we dont have this bone already, add it
      if (!newSkeleton->findBoneByName(boneName))
      {
        KXF::Bone *newBone = new KXF::Bone();
        newBone->inverseBindPose = glmMat4(currBonePtr->mOffsetMatrix);
        newBone->initialTransform = glmMat4(currBoneNode->mTransformation);

        blenderToKxf(newBone->inverseBindPose);
        blenderToKxf(newBone->initialTransform);

        newBone->name = boneName;
        parentNames[newBone] = parentBoneName;

        newSkeleton->bones.push_back(newBone);

        if (!parentBoneNode)
        {
          newSkeleton->rootBones.push_back(newBone);
        }

        newSkeleton->boneIndex[newBone->name] = newBone;

        assimpBoneIndex[newBone] = currBonePtr;
      }
    }

    // Add weights for the bones
    uint64_t currId = 0;
    for (auto bone : newSkeleton->bones)
    {
      // Bone needs to be matched to an assimp bone
      auto assimpFinder = assimpBoneIndex.find(bone);
      if (assimpFinder == assimpBoneIndex.end())
      {
        continue;
      }

      newSubmesh->boneIndex[bone->name] = currId;

      // Add weights from this bone to the submesh
      auto currBonePtr = assimpFinder->second;
      for (unsigned int currWeight = 0; currWeight < currBonePtr->mNumWeights; currWeight++)
      {
        aiVertexWeight* currWeightPtr = &currBonePtr->mWeights[currWeight];
        KXF::Vertex *currVertex = &newSubmesh->vertices[currWeightPtr->mVertexId];
        switch (vertexWeightCount.at(currWeightPtr->mVertexId))
        {
        case 0:
          currVertex->bones.x = currId;
          currVertex->weights.x = currWeightPtr->mWeight;
          vertexWeightCount.at(currWeightPtr->mVertexId)++;
          break;

        case 1:
          currVertex->bones.y = currId;
          currVertex->weights.y = currWeightPtr->mWeight;
          vertexWeightCount.at(currWeightPtr->mVertexId)++;
          break;

        case 2:
          currVertex->bones.z = currId;
          currVertex->weights.z = currWeightPtr->mWeight;
          vertexWeightCount.at(currWeightPtr->mVertexId)++;
          break;

        case 3:
          currVertex->bones.w = currId;
          currVertex->weights.w = currWeightPtr->mWeight;
          vertexWeightCount.at(currWeightPtr->mVertexId)++;
          break;

        default:
          break;
        }
      }
      
      currId++;
    }

    if (currId > 0)
      newSubmesh->vertexFlags = KXF::VertexFlags(newSubmesh->vertexFlags | KXF::VF_Bones);

    // Add the new mesh
    LogNotice("Imported mesh %s", currMeshName.c_str());
    auto meshFinder = meshes.find(currMeshName);
    if (meshFinder == meshes.end())
    {
      auto newMesh = new KXF::Mesh();
      newMesh->name = currMeshName;
      newMesh->submeshes.push_back(newSubmesh);
      newSubmesh->mesh = newMesh;
      meshes[currMeshName] = newMesh;
      outputDocument->mesh(newMesh);
    }
    else
    {
      meshes.at(currMeshName)->submeshes.push_back(newSubmesh);
      newSubmesh->mesh = meshes.at(currMeshName);
    }

  }

  // Resolve the skeletons children and parent bones
  for (auto bone : newSkeleton->bones)
  {
    auto finder = parentNames.find(bone);
    if (finder != parentNames.end())
    {
      bone->parent = newSkeleton->findBoneByName(finder->second);
      if (bone->parent)
      {
        bone->parent->children.push_back(bone);
      }
      else
      {
        newSkeleton->rootBones.push_back(bone);
      }
    }
  }

  // Add the skeleton 
  if (newSkeleton->bones.size() > 0)
  {
    LogNotice("Imported skeleton");
    outputDocument->skeleton(newSkeleton);
  }
  else
  {
    delete newSkeleton;
  }

  // Add any animations
  for (uint32_t currAnimation = 0; currAnimation < inputScene->mNumAnimations; currAnimation++)
  {
    aiAnimation* currAnimPtr = inputScene->mAnimations[currAnimation];
    std::string currAnimName = currAnimPtr->mName.C_Str();
    if (currAnimName.substr(0, 11) == std::string("AnimStack::"))
    {
      currAnimName = currAnimName.substr(11);
    }

    auto splitter = wir::split(currAnimName, {'|'});
    if (splitter.size() > 1)
    {
      currAnimName = splitter.back();
    }

    currAnimName = meshes.begin()->first + "_" + currAnimName;

    double tps = currAnimPtr->mTicksPerSecond;
    if (tps == 0.0)
    {
      LogWarning("Ticks per second not set (or 0), default to 30.");
      tps = 30.0;
    }

    KXF::Animation *newAnimation = new KXF::Animation();
    newAnimation->name = currAnimName;
    newAnimation->duration = currAnimPtr->mDuration / tps;

    outputDocument->animation(newAnimation);

    LogNotice("Importing animation %s, %f seconds", newAnimation->name.c_str(), newAnimation->duration);

    for (uint32_t currChannel = 0; currChannel < currAnimPtr->mNumChannels; currChannel++)
    {
      aiNodeAnim* currChannelPtr = currAnimPtr->mChannels[currChannel];
      std::string currChannelName = currChannelPtr->mNodeName.C_Str();
      
      KXF::AnimationChannel *newAnimationChannel = new KXF::AnimationChannel();
      newAnimationChannel->name = currChannelName;
      newAnimation->channels.push_back(newAnimationChannel);

      // Position keys
      KXF::AnimationTrackVec3 *positionTrack = new KXF::AnimationTrackVec3();
      positionTrack->name = "Translation";
      newAnimationChannel->tracks.push_back(positionTrack);

      for (uint32_t currTKey = 0; currTKey < currChannelPtr->mNumPositionKeys; currTKey++)
      {
        aiVectorKey currKey = currChannelPtr->mPositionKeys[currTKey];
        KXF::AnimationKey<glm::vec3> newKey;
        newKey.time = currKey.mTime / tps;
        newKey.value = glm::vec3(currKey.mValue.x, currKey.mValue.y, currKey.mValue.z);

        // @todo make optional
        blenderToKxf(newKey.value);

        positionTrack->keys.push_back(newKey); 
      }

      // Rotation keys
      KXF::AnimationTrackQuat *rotationTrack = new KXF::AnimationTrackQuat();
      rotationTrack->name = "Rotation";
      newAnimationChannel->tracks.push_back(rotationTrack);

      for (uint32_t currRKey = 0; currRKey < currChannelPtr->mNumRotationKeys; currRKey++)
      {
        aiQuatKey currKey = currChannelPtr->mRotationKeys[currRKey];
        KXF::AnimationKey<glm::quat> newKey;
        newKey.time = currKey.mTime / tps;
        newKey.value = glm::quat(currKey.mValue.w, currKey.mValue.x, currKey.mValue.y, currKey.mValue.z);

        // @todo make optional
        blenderToKxf(newKey.value);
 
        rotationTrack->keys.push_back(newKey);

      }

      // Scale keys
      KXF::AnimationTrackVec3 *scaleTrack = new KXF::AnimationTrackVec3();
      scaleTrack->name = "Scale";
      newAnimationChannel->tracks.push_back(scaleTrack);

      for (uint32_t currSKey = 0; currSKey < currChannelPtr->mNumScalingKeys; currSKey++)
      {
        aiVectorKey currKey = currChannelPtr->mScalingKeys[currSKey];
        KXF::AnimationKey<glm::vec3> newKey;
        newKey.time = currKey.mTime / tps;
        newKey.value = glm::vec3(currKey.mValue.x, currKey.mValue.y, currKey.mValue.z);

        // @todo make optional
        blenderToKxfScale(newKey.value);

        scaleTrack->keys.push_back(newKey);
      }
    }

  }

  LogNotice("Import done!");

}

aiScene const *KXF::Importer_Assimp::loadScene(std::string const &filePath)
{
  auto assScene = m_importer->ReadFile(filePath, aiProcess_FlipWindingOrder |  aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_ValidateDataStructure | aiProcess_ImproveCacheLocality);
  if (!assScene)
  {
    LogError("Assimp failed: %s", m_importer->GetErrorString());
    return nullptr;
  }

  return assScene;
}
