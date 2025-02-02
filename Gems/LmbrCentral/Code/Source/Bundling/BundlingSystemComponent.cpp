/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BundlingSystemComponent.h"

#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/std/string/string_view.h>

#include <AzFramework/Asset/AssetBundleManifest.h>
#include <AzFramework/StringFunc/StringFunc.h>

#include <ISystem.h>
#include <IConsole.h>

#include <AzFramework/Archive/IArchive.h>

namespace LmbrCentral
{
    const char bundleRoot[] = "@products@";

    void BundlingSystemComponent::Activate()
    {
        BundlingSystemRequestBus::Handler::BusConnect();
        CrySystemEventBus::Handler::BusConnect();
        AZ::IO::ArchiveNotificationBus::Handler::BusConnect();
    }

    void BundlingSystemComponent::Deactivate()
    {
        AZ::IO::ArchiveNotificationBus::Handler::BusDisconnect();
        CrySystemEventBus::Handler::BusDisconnect();
        BundlingSystemRequestBus::Handler::BusDisconnect();
    }

    void BundlingSystemComponent::OnCrySystemInitialized(ISystem& system, const SSystemInitParams& systemInitParams)
    {
        AZ_UNUSED(systemInitParams);

        system.GetIConsole()->AddCommand("loadbundles", ConsoleCommandLoadBundles);
        system.GetIConsole()->AddCommand("unloadbundles", ConsoleCommandUnloadBundles);
    }

    void BundlingSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<BundlingSystemComponent, AZ::Component>()
                ->Version(1)
                ;
        }
    }

    AZStd::vector<AZStd::string> BundlingSystemComponent::GetBundleList(const char* bundlePath, const char* bundleExtension) const
    {
        AZStd::string fileFilter{ AZStd::string::format("*%s",bundleExtension) };
        AZStd::vector<AZStd::string> bundleList;

        AZ::IO::FileIOBase::GetInstance()->FindFiles(bundlePath, fileFilter.c_str(), [&bundleList](const char* foundPath) -> bool
        {
            if (!AZ::IO::FileIOBase::GetInstance()->IsDirectory(foundPath))
            {
                bundleList.push_back(foundPath);
            }
            return true;
        });

        return bundleList;
    }

    void BundlingSystemComponent::ConsoleCommandLoadBundles(IConsoleCmdArgs* pCmdArgs)
    {
        const char defaultBundleFolder[] = "bundles";
        const char defaultBundleExtension[] = ".pak";

        const char* bundleFolder = pCmdArgs->GetArgCount() > 1 ? pCmdArgs->GetArg(1) : defaultBundleFolder;
        const char* bundleExtension = pCmdArgs->GetArgCount() > 2 ? pCmdArgs->GetArg(2) : defaultBundleExtension;

        BundlingSystemRequestBus::Broadcast(&BundlingSystemRequestBus::Events::LoadBundles, bundleFolder, bundleExtension);
    }

    void BundlingSystemComponent::ConsoleCommandUnloadBundles(IConsoleCmdArgs* pCmdArgs)
    {
        AZ_UNUSED(pCmdArgs);
        BundlingSystemRequestBus::Broadcast(&BundlingSystemRequestBus::Events::UnloadBundles);
    }

    void BundlingSystemComponent::UnloadBundles()
    {
        ISystem* crySystem{ GetISystem() };
        if (!crySystem)
        {
            AZ_Error("BundlingSystem", false, "Couldn't Get ISystem to unload bundles!");
            return;
        }
        if (!m_bundleModeBundles.size())
        {
            AZ_TracePrintf("BundlingSystem", "No bundles currently loaded\n");
            return;
        }
        AZStd::lock_guard<AZStd::mutex> openBundleLock(m_bundleModeMutex);
        for (const auto& thisBundle : m_bundleModeBundles)
        {
            if (crySystem->GetIPak()->ClosePack(thisBundle.c_str()))
            {
                AZ_TracePrintf("BundlingSystem", "Unloaded %s\n",thisBundle.c_str());
            }
            else
            {
                AZ_TracePrintf("BundlingSystem", "Failed to unload %s\n",thisBundle.c_str());
            }
        }
        m_bundleModeBundles.clear();
    }

    void BundlingSystemComponent::LoadBundles(const char* bundleFolder, const char* bundleExtension)
    {
        AZStd::vector<AZStd::string> bundleList = GetBundleList(bundleFolder, bundleExtension);
        AZ_TracePrintf("BundlingSystem", "Loading bundles from %s of type %s\n",bundleFolder, bundleExtension);
        if (!bundleList.size())
        {
            AZ_Warning("BundlingSystem", false, "Failed to locate bundles of type %s in folder %s", bundleExtension, bundleFolder);
            return;
        }

        ISystem* crySystem{ GetISystem() };
        if (!crySystem)
        {
            AZ_Error("BundlingSystem", false, "Couldn't Get ISystem to load bundles!");
            return;
        }

        auto cryPak = crySystem->GetIPak();
        if (!cryPak)
        {
            AZ_Error("BundlingSystem", false, "Couldn't Get IArchive to load bundles!");
            return;
        }
        AZStd::lock_guard<AZStd::mutex> openBundleLock(m_bundleModeMutex);
        for (const auto& thisBundle : bundleList)
        {
            for (const auto& openedBundle : m_bundleModeBundles)
            {
                if (openedBundle == thisBundle)
                {
                    continue;
                }
            }
            AZStd::string bundlePath;
            AzFramework::StringFunc::Path::Join(bundleRoot, thisBundle.c_str(), bundlePath);
            if (cryPak->OpenPack(bundleRoot, thisBundle.c_str()))
            {
                AZ_TracePrintf("BundlingSystem", "Loaded bundle %s\n",bundlePath.c_str());
                m_bundleModeBundles.emplace_back(AZStd::move(bundlePath));
            }
            else
            {
                AZ_TracePrintf("BundlingSystem", "Failed to load %s\n",bundlePath.c_str());
            }
        }
    }

    void BundlingSystemComponent::BundleOpened(const char* bundleName, AZStd::shared_ptr<AzFramework::AssetBundleManifest> bundleManifest, const char* nextBundle, AZStd::shared_ptr<AzFramework::AssetRegistry> bundleCatalog)
    {
        AZ_TracePrintf("BundlingSystem", "Opening bundle %s\n",bundleName);
        AZStd::lock_guard<AZStd::mutex> openBundleLock(m_openedBundleMutex);
        auto bundleIter = m_openedBundles.find(bundleName);
        if (bundleIter != m_openedBundles.end())
        {
            AZ_Warning("BundlingSystem", false, "Received BundleOpened message for bundle in records - %s", bundleName);
            return;
        }

        // Not already opened, new entry
        m_openedBundles[bundleName] = AZStd::make_unique<OpenBundleInfo>();
        AZStd::shared_ptr<AzFramework::AssetRegistry> nextCatalog; // Not all bundles will have catalogs - some are legacy.
        if (nextBundle == nullptr)
        {
            // Added to the end
            m_openedBundleList.push_back(bundleName);
        }
        else
        {
            for (auto openedListIter = m_openedBundleList.rbegin(); openedListIter != m_openedBundleList.rend(); ++openedListIter)
            {
                if (*openedListIter == nextBundle)
                {
                    m_openedBundleList.insert(openedListIter.base(), bundleName);
                    break;
                }
                auto openedBundleIter = m_openedBundles.find(*openedListIter);
                if (openedBundleIter == m_openedBundles.end())
                {
                    AZ_Error("BundlingSystem", false, "Invalid bundle %s in openedList is not found in bundle map", openedListIter->c_str());
                }
                else if(openedBundleIter->second->m_catalog)
                {
                    nextCatalog = openedBundleIter->second->m_catalog;
                }
            }
        }

        if (bundleManifest)
        {
            m_openedBundles[bundleName]->m_manifest = bundleManifest;
            m_openedBundles[bundleName]->m_catalog = AZStd::move(bundleCatalog);
            if (!m_openedBundles[bundleName]->m_catalog)
            {
                AZ_Error("BundlingSystem", false, "Failed to load catalog %s from bundle %s", bundleManifest->GetCatalogName().c_str(), bundleName);
            }

            bool catalogAdded{ false };
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(catalogAdded, &AZ::Data::AssetCatalogRequestBus::Events::InsertDeltaCatalogBefore, m_openedBundles[bundleName]->m_catalog, nextCatalog);

            if (bundleManifest->GetDependentBundleNames().size())
            {
                OpenDependentBundles(bundleName, bundleManifest);
            }
        }
        else
        {
            AZ_TracePrintf("BundlingSystem", "No Manifest found - %s is a legacy Pak\n",bundleName);
        }
    }

    void BundlingSystemComponent::OpenDependentBundles(const char* bundleName, AZStd::shared_ptr<AzFramework::AssetBundleManifest> bundleManifest)
    {
        ISystem* crySystem{ GetISystem() };
        if (!crySystem)
        {
            AZ_Error("BundlingSystem", false, "Couldn't Get ISystem to load dependent bundles for %s", bundleName);
            return;
        }

        auto cryPak{ crySystem->GetIPak() };
        if (!cryPak)
        {
            AZ_Error("BundlingSystem", false, "Couldn't Get IArchive to load dependent bundles for %s", bundleName);
            return;
        }

        AZStd::string folderPath;
        AzFramework::StringFunc::Path::GetFolderPath(bundleName, folderPath);
        for (const auto& thisBundle : bundleManifest->GetDependentBundleNames())
        {
            AZStd::string bundlePath;
            AzFramework::StringFunc::Path::Join(folderPath.c_str(), thisBundle.c_str(), bundlePath);

            if (!cryPak->OpenPack(bundleRoot, bundlePath.c_str()))
            {
                // We're not bailing here intentionally - try to open the remaining bundles
                AZ_Warning("BundlingSystem", false, "Failed to open dependent bundle %s of bundle %s", bundlePath.c_str(), bundleName);
            }
        }
    }

    void BundlingSystemComponent::BundleClosed(const char* bundleName)
    {
        AZ_TracePrintf("BundlingSystem", "Closing bundle %s\n",bundleName);
        AZStd::unique_ptr<OpenBundleInfo> bundleRecord;
        {
            AZStd::lock_guard<AZStd::mutex> openBundleLock(m_openedBundleMutex);
            auto bundleIter = m_openedBundles.find(bundleName);
            if (bundleIter == m_openedBundles.end())
            {

                AZ_Warning("BundlingSystem", false, "Failed to locate record for bundle %s", bundleName);

                return;
            }

            bundleRecord = AZStd::move(bundleIter->second);
            m_openedBundles.erase(bundleIter);

            for (auto openedListIter = m_openedBundleList.begin(); openedListIter != m_openedBundleList.end(); ++openedListIter)
            {
                if (*openedListIter == bundleName)
                {
                    m_openedBundleList.erase(openedListIter);
                    break;
                }
            }
        }
        bool catalogRemoved{ false };
        if (bundleRecord && bundleRecord->m_catalog)
        {
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(catalogRemoved, &AZ::Data::AssetCatalogRequestBus::Events::RemoveDeltaCatalog, bundleRecord->m_catalog);

            if (bundleRecord->m_manifest->GetDependentBundleNames().size())
            {
                CloseDependentBundles(bundleName, bundleRecord->m_manifest);
            }
        }
    }

    void BundlingSystemComponent::CloseDependentBundles(const char* bundleName, AZStd::shared_ptr<AzFramework::AssetBundleManifest> bundleManifest)
    {
        ISystem* crySystem{ GetISystem() };
        if (!crySystem)
        {
            AZ_Error("BundlingSystem", false, "Couldn't get ISystem to close dependent bundles for %s", bundleName);
            return;
        }

        auto cryPak{ crySystem->GetIPak() };
        if (!cryPak)
        {
            AZ_Error("BundlingSystem", false, "Couldn't get IArchive to close dependent bundles for %s", bundleName);
            return;
        }

        AZStd::string folderPath;
        AzFramework::StringFunc::Path::GetFolderPath(bundleName, folderPath);
        for (const auto& thisBundle : bundleManifest->GetDependentBundleNames())
        {
            AZStd::string bundlePath;
            AzFramework::StringFunc::Path::Join(folderPath.c_str(), thisBundle.c_str(), bundlePath);

            if (!cryPak->ClosePack(bundlePath.c_str()))
            {
                // We're not bailing here intentionally - try to close the remaining bundles
                AZ_Warning("BundlingSystem", false, "Failed to close dependent bundle %s of bundle %s", bundlePath.c_str(), bundleName);
            }
        }
    }

    size_t BundlingSystemComponent::GetOpenedBundleCount() const
    {
        AZStd::lock_guard<AZStd::mutex> openBundleLock(m_openedBundleMutex);
        size_t bundleCount = m_openedBundleList.size();

        AZ_Assert(bundleCount == m_openedBundles.size(), "Bundle count does not match - %d vs %d", bundleCount, m_openedBundles.size());
        return bundleCount;
    }
}
