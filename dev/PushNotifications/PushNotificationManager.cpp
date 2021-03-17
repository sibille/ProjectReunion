#include "pch.h"
#include "PushNotificationManager.h"
#include "PushNotificationManager.g.cpp"
#include "PushNotificationCreateChannelResult.h"
#include <winrt\windows.networking.pushnotifications.h>
#include <winerror.h>
#include <algorithm>
#include <vector>
#include <iostream>
#include "PushNotificationChannel.h"

using namespace winrt::Windows::Networking::PushNotifications;
using namespace winrt;

namespace winrt::Microsoft::ProjectReunion::implementation
{
    inline constexpr std::uint32_t c_maxBackoffSeconds{ 960 };

    const HRESULT WNP_E_NOT_CONNECTED = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x880403E8L);
    const HRESULT WNP_E_RECONNECTING = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x880403E9L);
    const HRESULT WNP_E_BIND_USER_BUSY = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x880403FEL);

    bool PushNotificationManager::isChannelRequestRetryable(const hresult& hr)
    {
        switch (hr)
        {
        case HRESULT_FROM_WIN32(ERROR_TIMEOUT):
        case WNP_E_NOT_CONNECTED:
        case WPN_E_OUTSTANDING_CHANNEL_REQUEST:
        case WNP_E_RECONNECTING:
        case WNP_E_BIND_USER_BUSY:
        case RPC_S_SERVER_UNAVAILABLE:
            return true;
        default:
            return false;
        }
    }

    inline bool IsPackagedProcess()
    {
        uint32_t packageFullNameLength = 0;
        const auto rc{ GetCurrentPackageFullName(&packageFullNameLength, nullptr) };

        if (rc != ERROR_INSUFFICIENT_BUFFER)
        {
            winrt::throw_hresult(rc);
        }
        return rc == ERROR_INSUFFICIENT_BUFFER;
    }

    Windows::Foundation::IAsyncOperationWithProgress<Microsoft::ProjectReunion::PushNotificationCreateChannelResult, Microsoft::ProjectReunion::PushNotificationCreateChannelStatus> PushNotificationManager::CreateChannelAsync(winrt::guid remoteId)
    {
        static std::vector<winrt::guid> s_remoteIdList;
        static std::mutex s_mutex;
        static std::unique_lock<std::mutex> s_lock(s_mutex, std::defer_lock); // Avoid locking during the constructor call

        // Add a comment to suggest we are going to support in later versions 
        IsPackagedProcess();

        winrt::check_pointer(&remoteId);

        winrt::Microsoft::ProjectReunion::PushNotificationCreateChannelResult channelResult{ nullptr };

        uint8_t retryCount = 0;
        Microsoft::ProjectReunion::PushNotificationCreateChannelStatus
            channelStatus = { E_PENDING /* ExtendedError */, PushNotificationChannelStatus::InProgress /* Status */, retryCount /* RetryCount */ };

        auto progress{ co_await winrt::get_progress_token() };

        /*
        3. scope the find function and also understand what happens if find throws
        */
        bool remoteIdPresent = false;
        {
            s_lock.lock();
            auto it = std::find(s_remoteIdList.begin(), s_remoteIdList.end(), remoteId);
            remoteIdPresent = (it != s_remoteIdList.end()) ? true : false;
            s_lock.unlock();
        }

        if (remoteIdPresent)
        {
            channelResult = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(
                nullptr, WPN_E_OUTSTANDING_CHANNEL_REQUEST, PushNotificationChannelStatus::CompletedFailure);
            co_return channelResult;
        }

        {
            s_lock.lock();
            s_remoteIdList.push_back(remoteId);
            s_lock.unlock();
        }
        // give back the flow control so progress can be shown
        progress(channelStatus);

        PushNotificationChannelManager channelManager{};
        winrt::Windows::Networking::PushNotifications::PushNotificationChannel pushChannelReceived{ nullptr };

        for (auto backOffTimeInSeconds = 30; backOffTimeInSeconds <= c_maxBackoffSeconds * 2; backOffTimeInSeconds *= 2)
        {
            try
            {
                pushChannelReceived = co_await channelManager.CreatePushNotificationChannelForApplicationAsync();

                auto pushChannel = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationChannel>(pushChannelReceived);
                // This has to go
                    winrt::Microsoft::ProjectReunion::implementation::PushNotificationChannel* pushChannelImplementation =
                        winrt::get_self< winrt::Microsoft::ProjectReunion::implementation::PushNotificationChannel>(pushChannel);

                    channelResult = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(
                                        *pushChannelImplementation,
                                        S_OK,
                                        PushNotificationChannelStatus::CompletedSuccess);
                 break;
            }
            catch (...)
            {
                auto ex = hresult_error(to_hresult(), take_ownership_from_abi);

                if ((backOffTimeInSeconds <= c_maxBackoffSeconds) && isChannelRequestRetryable(ex.code()))
                {
                    channelStatus.extendedError = ex.code();
                    channelStatus.status = PushNotificationChannelStatus::InProgressRetry;
                    channelStatus.retryCount = ++retryCount;

                    progress(channelStatus);
                }
                else
                {
                    channelResult = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(nullptr, ex.code(), PushNotificationChannelStatus::CompletedFailure);
                    break;
                }
            }

            co_await winrt::resume_after(std::chrono::seconds(backOffTimeInSeconds));
        }

        {
            s_lock.lock();
            std::remove(s_remoteIdList.begin(), s_remoteIdList.end(), remoteId);
            s_lock.unlock();
        }
        co_return channelResult;
    }

    /*
    Windows::Foundation::IAsyncOperationWithProgress<Microsoft::ProjectReunion::PushNotificationCreateChannelResult, Microsoft::ProjectReunion::PushNotificationCreateChannelResult> PushNotificationManager::CreateChannelAsync(winrt::guid remoteId)
    {
        winrt::Microsoft::ProjectReunion::PushNotificationCreateChannelResult channelResult{ nullptr };
        auto progress{ co_await winrt::get_progress_token() };

        winrt::check_pointer(&remoteId);

        s_lock.lock();
        if (std::find(s_remoteIdList.begin(), s_remoteIdList.end(), remoteId) != s_remoteIdList.end())
        {
            s_lock.unlock();
            channelResult = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(nullptr, WPN_E_OUTSTANDING_CHANNEL_REQUEST, PushNotificationChannelStatus::CompletedFailure);
            co_return channelResult;
        }
        else
        {
            s_remoteIdList.push_back(remoteId);
            s_lock.unlock();
            progress(winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(nullptr, E_PENDING, PushNotificationChannelStatus::InProgress));
        }

        PushNotificationChannelManager channelManager{};
        winrt::Windows::Networking::PushNotifications::PushNotificationChannel pushChannel{ nullptr };

        for (auto backOffTimeInSeconds = 30; backOffTimeInSeconds <= c_maxBackoffSeconds * 2; backOffTimeInSeconds *= 2)
        {
            try
            {

                if (IsPackagedProcess())
                {
                    pushChannel = co_await channelManager.CreatePushNotificationChannelForApplicationAsync();
                }

                if (pushChannel != nullptr)
                {
                    channelResult = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(nullptr, S_OK, PushNotificationChannelStatus::CompletedSuccess);
                    break;
                }
            }
            catch (...)
            {
                auto ex = hresult_error(to_hresult(), take_ownership_from_abi);

                if ((backOffTimeInSeconds <= c_maxBackoffSeconds) && isChannelRequestRetryable(ex.code()))
                {
                    progress(winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(nullptr, ex.code(), PushNotificationChannelStatus::InProgressRetry));
                }
                else
                {
                    channelResult = winrt::make<winrt::Microsoft::ProjectReunion::implementation::PushNotificationCreateChannelResult>(nullptr, ex.code(), PushNotificationChannelStatus::CompletedFailure);
                    break;
                }
            }

            co_await winrt::resume_after(std::chrono::seconds(backOffTimeInSeconds));
        }

        s_lock.lock();
        std::remove(s_remoteIdList.begin(), s_remoteIdList.end(), remoteId);
        s_lock.unlock();

        co_return channelResult;
    } */
}
