﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.
#include <pch.h>
#include <ActivationRegistrationManager.h>
#include <Microsoft.ApplicationModel.Activation.ActivationRegistrationManager.g.cpp>

#include "LaunchActivatedEventArgs.h"
#include "ProtocolActivatedEventArgs.h"
#include "FileActivatedEventArgs.h"
#include "Association.h"
#include "ExtensionContract.h"
#include "EncodedLaunchExecuteCommand.h"

namespace winrt::Microsoft::ApplicationModel::Activation::implementation
{
    std::wstring GenerateEncodedLaunchUri(std::wstring const& appUserModelId, std::wstring const& contractId)
    {
        Windows::Foundation::Uri uri{ wil::str_printf<std::wstring>(L"%s://%s?%s=%s",
            c_encodedLaunchSchemeName, appUserModelId.c_str(), c_contractIdKeyName, contractId.c_str()).c_str() };
        return uri.AbsoluteUri().c_str();
    }

    std::wstring GenerateCommandLine(std::wstring const& modulePath)
    {
        std::wstring exePath{ modulePath.empty() ? GetModulePath() : modulePath };

        return wil::str_printf<std::wstring>(L"%s %s%s%s", exePath.c_str(), c_argumentPrefix,
            c_protocolArgumentString, c_argumentSuffix);
    }

    void ActivationRegistrationManager::RegisterForFileTypeActivation(
        array_view<hstring const> supportedFileTypes, hstring const& logo, hstring const& displayName,
        array_view<hstring const> supportedVerbs, hstring const& exePath)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, !supportedFileTypes.size());

        auto appId = ComputeAppId(exePath.c_str());
        auto type = AssociationType::File;
        auto progId = ComputeProgId(appId, type);

        RegisterProgId(progId.c_str(), L"", L"", displayName.c_str(), logo.c_str());
        RegisterApplication(appId.c_str());

        for (auto fileType : supportedFileTypes)
        {
            RegisterFileExtension(fileType.c_str());
            
            for (auto verb : supportedVerbs)
            {
                auto command = GenerateCommandLine(exePath.c_str()) + GenerateEncodedLaunchUri(L"App",
                    c_fileContractId) + L"&Verb=" + verb.c_str() + L"&File=%1";

                RegisterVerb(progId, verb.c_str(), command);
            }

            RegisterAssociationHandler(appId, fileType.c_str(), type);
        }
    }

    void ActivationRegistrationManager::RegisterForProtocolActivation(hstring const& schemeName,
        hstring const& logo, hstring const& displayName, hstring const& exePath)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, schemeName.empty());

        RegisterForProtocolActivationInternal(schemeName.c_str(), L"", logo.c_str(),
            displayName.c_str(), exePath.c_str());
    }

    void ActivationRegistrationManager::RegisterForStartupActivation(hstring const& taskId,
        hstring const& exePath)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, taskId.empty());

        // Example: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
        wil::unique_hkey key;
        THROW_IF_WIN32_ERROR(::RegOpenKeyEx(HKEY_CURRENT_USER, c_runKeyPath, 0, KEY_WRITE,
            key.put()));

        // Pass a command line that will make sense while constructing the args object.
        auto command = GenerateCommandLine(exePath.c_str()) + GenerateEncodedLaunchUri(L"App",
            c_startupTaskContractId) + L"&TaskId=" + taskId;

        // Name: taskId
        // Value: commandLine
        THROW_IF_WIN32_ERROR(::RegSetValueEx(key.get(), taskId.c_str(), 0, REG_SZ,
            reinterpret_cast<BYTE const*>(command.c_str()),
            static_cast<uint32_t>((command.size() + 1) * sizeof(wchar_t))));
    }

    void ActivationRegistrationManager::UnregisterForFileTypeActivation(array_view<hstring const> fileTypes,
        hstring const& exePath)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, !fileTypes.size());

        auto appId = ComputeAppId(exePath.c_str());
        auto type = AssociationType::File;
        auto progId = ComputeProgId(appId, type);

        for (auto fileType : fileTypes)
        {
            UnregisterAssociationHandler(appId, fileType.c_str(), type);
            UnregisterProgId(progId);
        }
    }

    void ActivationRegistrationManager::UnregisterForProtocolActivation(hstring const& schemeName,
        hstring const& exePath)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, schemeName.empty());

        auto appId = ComputeAppId(exePath.c_str());
        auto type = AssociationType::Protocol;
        auto progId = ComputeProgId(appId, type);
        UnregisterAssociationHandler(appId, schemeName.c_str(), type);
        UnregisterProgId(progId);
    }

    void ActivationRegistrationManager::UnregisterForStartupActivation(hstring const& taskId)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, taskId.empty());

        // Example: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
        wil::unique_hkey key;
        if (::RegOpenKeyEx(HKEY_CURRENT_USER, c_runKeyPath, 0, KEY_WRITE, key.put())
            == ERROR_SUCCESS)
        {
            ::RegDeleteValue(key.get(), taskId.c_str());
        }
    }

    void ActivationRegistrationManager::RegisterForProtocolActivationInternal(std::wstring const& schemeName,
        std::wstring const& appUserModelId, std::wstring const& logo, std::wstring const& displayName,
        std::wstring const& exePath)
    {
        THROW_HR_IF(E_ILLEGAL_METHOD_CALL, HasIdentity());
        THROW_HR_IF(E_INVALIDARG, schemeName.empty());

        RegisterProtocol(schemeName.c_str());

        auto appId = ComputeAppId(exePath.c_str());
        auto type = AssociationType::Protocol;
        auto progId = ComputeProgId(appId, type);

        RegisterProgId(progId.c_str(), L"", appUserModelId.c_str(), displayName.c_str(),
            logo.c_str());

        auto command = GenerateCommandLine(exePath.c_str()) + L"%1";
        RegisterVerb(progId.c_str(), c_openVerbName, command);

        RegisterApplication(appId.c_str());
        RegisterAssociationHandler(appId, schemeName.c_str(), type);
    }

    void ActivationRegistrationManager::RegisterEncodedLaunchCommand()
    {
        RegisterProtocol(c_launchSchemeName);

        auto delegateExecute = __uuidof(EncodedLaunchExecuteCommandFactory);
        RegisterVerb(c_launchSchemeName, c_openVerbName, L"", &delegateExecute);
    }

    void ActivationRegistrationManager::RegisterEncodedLaunchSupport(std::wstring const& appUserModelId,
        std::wstring const& exePath)
    {
        // Make sure the encoded launch delegate execute command is registered on the system.
        RegisterEncodedLaunchCommand();

        // Register the current app to receive launch requests.
        RegisterForProtocolActivationInternal(c_encodedLaunchSchemeName, appUserModelId,
            L"", L"Encoded Launch Target", exePath);
    }
}
