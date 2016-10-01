#include <ToolsFoundation/PCH.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Serialization/DocumentObjectConverter.h>
#include <Foundation/Serialization/JsonSerializer.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <ToolsFoundation/Document/DocumentManager.h>
#include <ToolsFoundation/Document/PrefabUtils.h>

#include <Foundation/IO/MemoryStream.h>
#include <Foundation/IO/FileSystem/FileReader.h>

void ezDocument::UpdatePrefabs()
{
  // make sure the prefabs are updated
  m_CachedPrefabDocuments.Clear();
  m_CachedPrefabGraphs.Clear();

  GetCommandHistory()->StartTransaction("Update Prefabs");

  UpdatePrefabsRecursive(GetObjectManager()->GetRootObject());

  GetCommandHistory()->FinishTransaction();

  ShowDocumentStatus("Prefabs have been updated");
}

void ezDocument::RevertPrefabs(const ezDeque<const ezDocumentObject*>& Selection)
{
  if (Selection.IsEmpty())
    return;

  auto pHistory = GetCommandHistory();

  m_CachedPrefabDocuments.Clear();
  m_CachedPrefabGraphs.Clear();

  pHistory->StartTransaction("Revert Prefab");

  for (auto pItem : Selection)
  {
    RevertPrefab(pItem);
  }

  pHistory->FinishTransaction();
}

void ezDocument::UnlinkPrefabs(const ezDeque<const ezDocumentObject*>& Selection)
{
  if (Selection.IsEmpty())
    return;

  /// \todo this operation is (currently) not undo-able, since it only operates on meta data

  for (auto pObject : Selection)
  {
    auto pMetaDoc = m_DocumentObjectMetaData.BeginModifyMetaData(pObject->GetGuid());
    pMetaDoc->m_CreateFromPrefab = ezUuid();
    pMetaDoc->m_PrefabSeedGuid = ezUuid();
    pMetaDoc->m_sBasePrefab.Clear();
    m_DocumentObjectMetaData.EndModifyMetaData(ezDocumentObjectMetaData::PrefabFlag);
  }
}

const ezString& ezDocument::GetCachedPrefabDocument(const ezUuid& documentGuid) const
{
  if (!m_CachedPrefabDocuments.Contains(documentGuid))
  {
    ezString sPrefabFile = GetDocumentPathFromGuid(documentGuid);

    m_CachedPrefabDocuments[documentGuid] = ReadDocumentAsString(sPrefabFile);
  }

  return m_CachedPrefabDocuments[documentGuid];
}

const ezAbstractObjectGraph* ezDocument::GetCachedPrefabGraph(const ezUuid& documentGuid) const
{
  if (m_CachedPrefabGraphs.Contains(documentGuid))
  {
    return m_CachedPrefabGraphs[documentGuid].Borrow();
  }
  else
  {
    const ezString& sPrefabFile = GetCachedPrefabDocument(documentGuid);
    if (sPrefabFile.IsEmpty())
    {
      return nullptr;
    }
    else
    {
      auto it = m_CachedPrefabGraphs.Insert(documentGuid, ezUniquePtr<ezAbstractObjectGraph>(EZ_DEFAULT_NEW(ezAbstractObjectGraph)));
      ezPrefabUtils::LoadGraph(*it.Value().Borrow(), sPrefabFile);
      return it.Value().Borrow();
    }
  }
}

ezStatus ezDocument::CreatePrefabDocumentFromSelection(const char* szFile, const ezRTTI* pRootType)
{
  auto Selection = GetSelectionManager()->GetTopLevelSelection(pRootType);

  if (Selection.GetCount() != 1)
    return ezStatus("To create a prefab, the selection must contain exactly one game object");

  const ezDocumentObject* pNode = Selection[0];

  ezUuid PrefabGuid, SeedGuid;
  SeedGuid.CreateNewUuid();
  ezStatus res = CreatePrefabDocument(szFile, pNode, SeedGuid, PrefabGuid);

  if (res.m_Result.Succeeded())
  {
    ReplaceByPrefab(pNode, szFile, PrefabGuid, SeedGuid);
  }

  return res;
}

ezStatus ezDocument::CreatePrefabDocument(const char* szFile, const ezDocumentObject* pSaveAsPrefab, const ezUuid& invPrefabSeed, ezUuid& out_NewDocumentGuid)
{
  EZ_ASSERT_DEV(pSaveAsPrefab != nullptr, "CreatePrefabDocument: pSaveAsPrefab must be a valod object!");
  const ezRTTI* pRootType = pSaveAsPrefab->GetTypeAccessor().GetType();

  const ezDocumentTypeDescriptor* pTypeDesc = nullptr;
  if (ezDocumentManager::FindDocumentTypeFromPath(szFile, true, pTypeDesc).Failed())
    return ezStatus("Document type is unknown: '%s'", szFile);

  if (pTypeDesc->m_pManager->EnsureDocumentIsClosed(szFile).Failed())
  {
    return ezStatus("Could not close the existing prefab document");
  }

  // prepare the current state as a graph
  ezAbstractObjectGraph PrefabGraph;

  ezDocumentObjectConverterWriter writer(&PrefabGraph, GetObjectManager(), true, true);
  auto pPrefabGraphMainNode = writer.AddObjectToGraph(pSaveAsPrefab);

  PrefabGraph.ReMapNodeGuids(invPrefabSeed, true);

  ezDocument* pSceneDocument = nullptr;

  {
    auto res = pTypeDesc->m_pManager->CreateDocument("ezPrefab", szFile, pSceneDocument, false);

    if (res.m_Result.Failed())
      return res;
  }

  out_NewDocumentGuid = pSceneDocument->GetGuid();

  ezUuid rootGuid = pSaveAsPrefab->GetGuid();
  rootGuid.RevertCombinationWithSeed(invPrefabSeed);

  auto pPrefabSceneRoot = pSceneDocument->GetObjectManager()->GetRootObject();
  ezDocumentObject* pPrefabSceneMainObject = pSceneDocument->GetObjectManager()->CreateObject(pRootType, rootGuid);
  pSceneDocument->GetObjectManager()->AddObject(pPrefabSceneMainObject, pPrefabSceneRoot, "Children", 0);

  ezDocumentObjectConverterReader reader(&PrefabGraph, pSceneDocument->GetObjectManager(), ezDocumentObjectConverterReader::Mode::CreateAndAddToDocument);
  reader.ApplyPropertiesToObject(pPrefabGraphMainNode, pPrefabSceneMainObject);

  auto res = pSceneDocument->SaveDocument();
  pTypeDesc->m_pManager->CloseDocument(pSceneDocument);
  return res;
}


ezUuid ezDocument::ReplaceByPrefab(const ezDocumentObject* pRootObject, const char* szPrefabFile, const ezUuid& PrefabAsset, const ezUuid& PrefabSeed)
{
  GetCommandHistory()->StartTransaction("Replace by Prefab");

  ezRemoveObjectCommand remCmd;
  remCmd.m_Object = pRootObject->GetGuid();

  ezInstantiatePrefabCommand instCmd;
  instCmd.m_bAllowPickedPosition = false;
  instCmd.m_CreateFromPrefab = PrefabAsset;
  instCmd.m_Parent = pRootObject->GetParent() == GetObjectManager()->GetRootObject() ? ezUuid() : pRootObject->GetParent()->GetGuid();
  instCmd.m_sJsonGraph = ReadDocumentAsString(szPrefabFile); // since the prefab might have been created just now, going through the cache (via GUID) will most likely fail
  instCmd.m_RemapGuid = PrefabSeed;

  GetCommandHistory()->AddCommand(remCmd);
  GetCommandHistory()->AddCommand(instCmd);
  GetCommandHistory()->FinishTransaction();

  return instCmd.m_CreatedRootObject;
}

ezUuid ezDocument::RevertPrefab(const ezDocumentObject* pObject)
{
  auto pHistory = GetCommandHistory();
  auto pMeta = m_DocumentObjectMetaData.BeginReadMetaData(pObject->GetGuid());

  const ezUuid PrefabAsset = pMeta->m_CreateFromPrefab;

  if (!PrefabAsset.IsValid())
  {
    m_DocumentObjectMetaData.EndReadMetaData();
    return ezUuid();
  }

  ezRemoveObjectCommand remCmd;
  remCmd.m_Object = pObject->GetGuid();

  ezInstantiatePrefabCommand instCmd;
  instCmd.m_bAllowPickedPosition = false;
  instCmd.m_CreateFromPrefab = PrefabAsset;
  instCmd.m_Parent = pObject->GetParent() == GetObjectManager()->GetRootObject() ? ezUuid() : pObject->GetParent()->GetGuid();
  instCmd.m_RemapGuid = pMeta->m_PrefabSeedGuid;
  instCmd.m_sJsonGraph = GetCachedPrefabDocument(pMeta->m_CreateFromPrefab);

  m_DocumentObjectMetaData.EndReadMetaData();

  pHistory->AddCommand(remCmd);
  pHistory->AddCommand(instCmd);

  return instCmd.m_CreatedRootObject;
}


void ezDocument::UpdatePrefabsRecursive(ezDocumentObject* pObject)
{
  // Deliberately copy the array as the UpdatePrefabObject function will add / remove elements from the array.
  auto ChildArray = pObject->GetChildren();

  ezStringBuilder sPrefabBase;

  for (auto pChild : ChildArray)
  {
    auto pMeta = m_DocumentObjectMetaData.BeginReadMetaData(pChild->GetGuid());
    const ezUuid PrefabAsset = pMeta->m_CreateFromPrefab;
    const ezUuid PrefabSeed = pMeta->m_PrefabSeedGuid;
    sPrefabBase = pMeta->m_sBasePrefab;

    m_DocumentObjectMetaData.EndReadMetaData();

    // if this is a prefab instance, update it
    if (PrefabAsset.IsValid())
    {
      UpdatePrefabObject(pChild, PrefabAsset, PrefabSeed, sPrefabBase);
    }
    else
    {
      // only recurse (currently), if no prefab was found
      // that means nested prefabs are currently not possible, that needs to be handled a bit differently later
      UpdatePrefabsRecursive(pChild);
    }
  }
}

void ezDocument::UpdatePrefabObject(ezDocumentObject* pObject, const ezUuid& PrefabAsset, const ezUuid& PrefabSeed, const char* szBasePrefab)
{
  const ezString& sNewPrefab = GetCachedPrefabDocument(PrefabAsset);

  ezStringBuilder sNewGraph;
  ezPrefabUtils::Merge(szBasePrefab, sNewPrefab, pObject, PrefabSeed, sNewGraph);

  // remove current object
  ezRemoveObjectCommand rm;
  rm.m_Object = pObject->GetGuid();
  
  // instantiate prefab again
  ezInstantiatePrefabCommand inst;
  inst.m_bAllowPickedPosition = false;
  inst.m_CreateFromPrefab = PrefabAsset;
  inst.m_Parent = pObject->GetParent() == GetObjectManager()->GetRootObject() ? ezUuid() : pObject->GetParent()->GetGuid();
  inst.m_RemapGuid = PrefabSeed;
  inst.m_sJsonGraph = sNewGraph;

  GetCommandHistory()->AddCommand(rm);
  GetCommandHistory()->AddCommand(inst);
}

ezString ezDocument::ReadDocumentAsString(const char* szFile) const
{
  ezFileReader file;
  if (file.Open(szFile) == EZ_FAILURE)
  {
    ezLog::Error("Failed to open document file '%s'", szFile);
    return ezString();
  }

  ezStringBuilder sGraph;
  sGraph.ReadAll(file);

  return sGraph;
}

ezString ezDocument::GetDocumentPathFromGuid(const ezUuid& documentGuid) const
{
  EZ_ASSERT_NOT_IMPLEMENTED;
  return ezString();
}
