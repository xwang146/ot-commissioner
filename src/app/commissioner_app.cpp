/*
 *    Copyright (c) 2019, The OpenThread Authors.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *    POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   The file implements commissioner application.
 */

#include "commissioner_app.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>

#include <address.hpp>
#include <error_macros.hpp>
#include <utils.hpp>

#include "json.hpp"

namespace ot {

namespace commissioner {

/**
 * The default commissioning handler that always accepts any joiner.
 *
 */
static bool DefaultCommissioningHandler(const JoinerInfo & aJoinerInfo,
                                        const std::string &aVendorName,
                                        const std::string &aVendorModel,
                                        const std::string &aVendorSwVersion,
                                        const ByteArray &  aVendorStackVersion,
                                        const std::string &aProvisioningUrl,
                                        const ByteArray &  aVendorData)
{
    (void)aJoinerInfo;
    (void)aVendorName;
    (void)aVendorModel;
    (void)aVendorSwVersion;
    (void)aVendorStackVersion;
    (void)aProvisioningUrl;
    (void)aVendorData;

    return true;
}

Error CommissionerApp::Create(std::shared_ptr<CommissionerApp> &aCommApp, const std::string &aConfigFile)
{
    Error     error;
    AppConfig appConfig;
    auto      app = std::shared_ptr<CommissionerApp>(new CommissionerApp());

    SuccessOrExit(error = ReadConfig(appConfig, aConfigFile));

    SuccessOrExit(error = app->Init(appConfig));

    aCommApp = app;

exit:
    return error;
}

Error CommissionerApp::Init(const AppConfig &aAppConfig)
{
    Error                         error;
    Config                        config;
    std::shared_ptr<Commissioner> commissioner = nullptr;

    SuccessOrExit(error = MakeConfig(config, aAppConfig));

    commissioner = Commissioner::Create(config, nullptr);
    VerifyOrExit(commissioner != nullptr, error = ERROR_INVALID_ARGS("bad commissioner configuration"));
    SuccessOrExit(error = commissioner->Start());

    mCommissioner = commissioner;
    mCommissioner->SetPanIdConflictHandler(
        [this](const std::string *aPeerAddr, const ChannelMask *aChannelMask, const uint16_t *aPanId, Error aError) {
            HandlePanIdConflict(aPeerAddr, aChannelMask, aPanId, aError);
        });
    mCommissioner->SetEnergyReportHandler(
        [this](const std::string *aPeerAddr, const ChannelMask *aChannelMask, const ByteArray *aEnergyList,
               Error aError) { HandleEnergyReport(aPeerAddr, aChannelMask, aEnergyList, aError); });
    mCommissioner->SetDatasetChangedHandler([this](Error aError) { HandleDatasetChanged(aError); });
    mCommissioner->SetJoinerInfoRequester(
        [this](JoinerType aType, const ByteArray &aJoinerId) { return GetJoinerInfo(aType, aJoinerId); });

    // This is the default behavior of OpenThread on-Mesh Commissioner.
    mCommissioner->SetCommissioningHandler(DefaultCommissioningHandler);

exit:
    return error;
}

Error CommissionerApp::Discover()
{
    return mCommissioner->Discover(mBorderAgents);
}

const std::list<BorderAgent> &CommissionerApp::GetBorderAgentList() const
{
    return mBorderAgents;
}

const BorderAgent *CommissionerApp::GetBorderAgent(const std::string &aNetworkName)
{
    for (auto &ba : mBorderAgents)
    {
        if (aNetworkName.empty() || aNetworkName == ba.mNetworkName)
        {
            return &ba;
        }
    }
    return nullptr;
}

Error CommissionerApp::Start(std::string &      aExistingCommissionerId,
                             const std::string &aBorderAgentAddr,
                             uint16_t           aBorderAgentPort)
{
    Error error;

    // We need to report the already active commissioner ID if one exists.
    SuccessOrExit(error = mCommissioner->Petition(aExistingCommissionerId, aBorderAgentAddr, aBorderAgentPort));
    SuccessOrExit(error = PullNetworkData());

exit:
    if (!error.NoError() && !IsActive())
    {
        Stop();
    }
    return error;
}

void CommissionerApp::Stop()
{
    mCommissioner->Resign();
}

void CommissionerApp::AbortRequests()
{
    mCommissioner->AbortRequests();
}

bool CommissionerApp::IsActive() const
{
    return mCommissioner->IsActive();
}

bool CommissionerApp::IsCcmMode() const
{
    return mCommissioner->IsCcmMode();
}

Error CommissionerApp::SaveNetworkData(const std::string &aFilename)
{
    Error       error;
    NetworkData networkData;

    networkData.mActiveDataset  = mActiveDataset;
    networkData.mPendingDataset = mPendingDataset;
    networkData.mCommDataset    = mCommDataset;
    networkData.mBbrDataset     = mBbrDataset;
    auto jsonString             = NetworkDataToJson(networkData);

    SuccessOrExit(error = WriteFile(jsonString, aFilename));

exit:
    return error;
}

Error CommissionerApp::PullNetworkData()
{
    Error                     error;
    CommissionerDataset       commDataset;
    ActiveOperationalDataset  activeDataset;
    PendingOperationalDataset pendingDataset;
    BbrDataset                bbrDataset;

    SuccessOrExit(error = mCommissioner->GetCommissionerDataset(commDataset, 0xFFFF));
    if (IsCcmMode())
    {
        SuccessOrExit(error = mCommissioner->GetBbrDataset(bbrDataset, 0xFFFF));
    }

    SuccessOrExit(error = mCommissioner->GetActiveDataset(activeDataset, 0xFFFF));
    SuccessOrExit(error = mCommissioner->GetPendingDataset(pendingDataset, 0xFFFF));

    MergeDataset(mCommDataset, commDataset);
    if (IsCcmMode())
    {
        mBbrDataset = bbrDataset;
    }
    mActiveDataset  = activeDataset;
    mPendingDataset = pendingDataset;

exit:
    return error;
}

Error CommissionerApp::GetSessionId(uint16_t &aSessionId) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    aSessionId = mCommissioner->GetSessionId();

exit:
    return error;
}

Error CommissionerApp::GetBorderAgentLocator(uint16_t &aLocator) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    // We must have Border Agent Locator in commissioner dataset.
    ASSERT(mCommDataset.mPresentFlags & CommissionerDataset::kBorderAgentLocatorBit);

    aLocator = mCommDataset.mBorderAgentLocator;

exit:
    return error;
}

Error CommissionerApp::GetSteeringData(ByteArray &aSteeringData, JoinerType aJoinerType) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    switch (aJoinerType)
    {
    case JoinerType::kMeshCoP:
        VerifyOrExit((mCommDataset.mPresentFlags & CommissionerDataset::kSteeringDataBit),
                     error = ERROR_NOT_FOUND("cannot find Thread 1.1 joiner Steering Data"));
        aSteeringData = mCommDataset.mSteeringData;
        break;

    case JoinerType::kAE:
        VerifyOrExit((mCommDataset.mPresentFlags & CommissionerDataset::kAeSteeringDataBit),
                     error = ERROR_NOT_FOUND("cannot find Thread CCM AE Steering Data"));
        aSteeringData = mCommDataset.mAeSteeringData;
        break;

    case JoinerType::kNMKP:
        VerifyOrExit((mCommDataset.mPresentFlags & CommissionerDataset::kNmkpSteeringDataBit),
                     error = ERROR_NOT_FOUND("cannot find CCM NMKP Steering Data"));
        aSteeringData = mCommDataset.mNmkpSteeringData;
        break;
    }

exit:
    return error;
}

Error CommissionerApp::EnableJoiner(JoinerType         aType,
                                    uint64_t           aEui64,
                                    const ByteArray &  aPSKd,
                                    const std::string &aProvisioningUrl)
{
    Error error;
    auto  joinerId    = Commissioner::ComputeJoinerId(aEui64);
    auto  commDataset = mCommDataset;
    commDataset.mPresentFlags &= ~CommissionerDataset::kSessionIdBit;
    commDataset.mPresentFlags &= ~CommissionerDataset::kBorderAgentLocatorBit;
    auto &steeringData = GetSteeringData(commDataset, aType);

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    VerifyOrExit(mJoiners.count({aType, joinerId}) == 0,
                 error = ERROR_ALREADY_EXISTS("joiner(type={}, eui64={:X}) has already been enabled",
                                              utils::to_underlying(aType), aEui64));

    Commissioner::AddJoiner(steeringData, joinerId);
    SuccessOrExit(error = mCommissioner->SetCommissionerDataset(commDataset));

    MergeDataset(mCommDataset, commDataset);
    mJoiners.emplace(JoinerKey{aType, joinerId}, JoinerInfo{aType, aEui64, aPSKd, aProvisioningUrl});

exit:
    return error;
}

Error CommissionerApp::DisableJoiner(JoinerType aType, uint64_t aEui64)
{
    Error     error;
    ByteArray joinerId;
    auto      commDataset = mCommDataset;
    commDataset.mPresentFlags &= ~CommissionerDataset::kSessionIdBit;
    commDataset.mPresentFlags &= ~CommissionerDataset::kBorderAgentLocatorBit;
    auto &steeringData = GetSteeringData(commDataset, aType);

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    steeringData = {0x00};
    for (const auto &kv : mJoiners)
    {
        auto &joiner = kv.second;
        if (joiner.mType == aType && joiner.mEui64 == aEui64)
        {
            continue;
        }
        joinerId = Commissioner::ComputeJoinerId(aEui64);
        Commissioner::AddJoiner(steeringData, joinerId);
    }

    SuccessOrExit(error = mCommissioner->SetCommissionerDataset(commDataset));

    MergeDataset(mCommDataset, commDataset);
    mJoiners.erase(JoinerKey{aType, joinerId});

exit:
    return error;
}

Error CommissionerApp::EnableAllJoiners(JoinerType aType, const ByteArray &aPSKd, const std::string &aProvisioningUrl)
{
    Error     error;
    ByteArray joinerId;
    auto      commDataset = mCommDataset;
    commDataset.mPresentFlags &= ~CommissionerDataset::kSessionIdBit;
    commDataset.mPresentFlags &= ~CommissionerDataset::kBorderAgentLocatorBit;
    auto &steeringData = GetSteeringData(commDataset, aType);

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    // Set steering data to all 1 to enable all joiners.
    steeringData = {0xFF};
    SuccessOrExit(error = mCommissioner->SetCommissionerDataset(commDataset));

    MergeDataset(mCommDataset, commDataset);

    EraseAllJoiners(aType);
    joinerId = Commissioner::ComputeJoinerId(0);
    mJoiners.emplace(JoinerKey{aType, joinerId}, JoinerInfo{aType, 0, aPSKd, aProvisioningUrl});

exit:
    return error;
}

Error CommissionerApp::DisableAllJoiners(JoinerType aType)
{
    Error error;
    auto  commDataset = mCommDataset;
    commDataset.mPresentFlags &= ~CommissionerDataset::kSessionIdBit;
    commDataset.mPresentFlags &= ~CommissionerDataset::kBorderAgentLocatorBit;
    auto &steeringData = GetSteeringData(commDataset, aType);

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    // Set steering data to all 0 to disable all joiners.
    steeringData = {0x00};
    SuccessOrExit(error = mCommissioner->SetCommissionerDataset(commDataset));

    MergeDataset(mCommDataset, commDataset);
    EraseAllJoiners(aType);

exit:
    return error;
}

bool CommissionerApp::IsJoinerCommissioned(JoinerType aType, uint64_t aEui64)
{
    // This doesn't work for CCM joiners, since CCM joiners are not
    // commissioned by the commissioner.
    auto joiner = mJoiners.find(JoinerKey{aType, Commissioner::ComputeJoinerId(aEui64)});
    if (joiner == mJoiners.end())
    {
        return false;
    }
    return joiner->second.mIsCommissioned;
}

Error CommissionerApp::GetJoinerUdpPort(uint16_t &aJoinerUdpPort, JoinerType aJoinerType) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    switch (aJoinerType)
    {
    case JoinerType::kMeshCoP:
        VerifyOrExit(mCommDataset.mPresentFlags & CommissionerDataset::kJoinerUdpPortBit,
                     error = ERROR_NOT_FOUND("cannot find Thread 1.1 Joiner UDP Port"));
        aJoinerUdpPort = mCommDataset.mJoinerUdpPort;
        break;

    case JoinerType::kAE:
        VerifyOrExit(mCommDataset.mPresentFlags & CommissionerDataset::kAeUdpPortBit,
                     error = ERROR_NOT_FOUND("cannot find Thread CCM AE UDP Port"));
        aJoinerUdpPort = mCommDataset.mAeUdpPort;
        break;

    case JoinerType::kNMKP:
        VerifyOrExit(mCommDataset.mPresentFlags & CommissionerDataset::kNmkpUdpPortBit,
                     error = ERROR_NOT_FOUND("cannot find Thread CCM NMKP Port"));
        aJoinerUdpPort = mCommDataset.mNmkpUdpPort;
        break;
    }

exit:
    return error;
}

Error CommissionerApp::SetJoinerUdpPort(JoinerType aType, uint16_t aUdpPort)
{
    Error error;
    auto  commDataset = mCommDataset;
    commDataset.mPresentFlags &= ~CommissionerDataset::kSessionIdBit;
    commDataset.mPresentFlags &= ~CommissionerDataset::kBorderAgentLocatorBit;
    auto &joinerUdpPort = GetJoinerUdpPort(commDataset, aType);

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    joinerUdpPort = aUdpPort;
    SuccessOrExit(error = mCommissioner->SetCommissionerDataset(commDataset));

    MergeDataset(mCommDataset, commDataset);

exit:
    return error;
}

Error CommissionerApp::GetCommissionerDataset(CommissionerDataset &aDataset, uint16_t aDatasetFlags)
{
    return mCommissioner->GetCommissionerDataset(aDataset, aDatasetFlags);

    // Don't merge requested commissioner dataset, because the commissioner is
    // the source of commissioner dataset.
}

Error CommissionerApp::SetCommissionerDataset(const CommissionerDataset &aDataset)
{
    Error error;

    SuccessOrExit(error = mCommissioner->SetCommissionerDataset(aDataset));
    MergeDataset(mCommDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::GetActiveTimestamp(Timestamp &aTimestamp) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    ASSERT(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kActiveTimestampBit);
    aTimestamp = mActiveDataset.mActiveTimestamp;

exit:
    return error;
}

Error CommissionerApp::GetChannel(Channel &aChannel)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    // Since channel will be updated by pending operational after a delay time,
    // we need to pull the active operational dataset.

    // TODO(wgtdkp): should we send MGMT_ACTIVE_GET.req for all GetXXX APIs ?
    SuccessOrExit(error = mCommissioner->GetActiveDataset(mActiveDataset, 0xFFFF));

    ASSERT(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kChannelBit);

    aChannel = mActiveDataset.mChannel;

exit:
    return error;
}

Error CommissionerApp::SetChannel(const Channel &aChannel, MilliSeconds aDelay)
{
    Error                     error;
    PendingOperationalDataset pendingDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    pendingDataset.mChannel = aChannel;
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kChannelBit;

    pendingDataset.mDelayTimer = aDelay.count();
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kDelayTimerBit;

    SuccessOrExit(error = mCommissioner->SetPendingDataset(pendingDataset));

    MergeDataset(mPendingDataset, pendingDataset);

exit:
    return error;
}

Error CommissionerApp::GetChannelMask(ChannelMask &aChannelMask) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kChannelMaskBit,
                 error = ERROR_NOT_FOUND("cannot find valid Channel Masks in Active Operational Dataset"));
    aChannelMask = mActiveDataset.mChannelMask;

exit:
    return error;
}

Error CommissionerApp::SetChannelMask(const ChannelMask &aChannelMask)
{
    Error                    error;
    ActiveOperationalDataset activeDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    activeDataset.mChannelMask = aChannelMask;
    activeDataset.mPresentFlags |= ActiveOperationalDataset::kChannelMaskBit;

    SuccessOrExit(error = mCommissioner->SetActiveDataset(activeDataset));

    MergeDataset(mActiveDataset, activeDataset);

exit:
    return error;
}

Error CommissionerApp::GetExtendedPanId(ByteArray &aExtendedPanId) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kExtendedPanIdBit,
                 error = ERROR_NOT_FOUND("cannot find valid Extended PAN ID in Active Operational Dataset"));
    aExtendedPanId = mActiveDataset.mExtendedPanId;

exit:
    return error;
}

Error CommissionerApp::SetExtendedPanId(const ByteArray &aExtendedPanId)
{
    Error                    error;
    ActiveOperationalDataset activeDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    activeDataset.mExtendedPanId = aExtendedPanId;
    activeDataset.mPresentFlags |= ActiveOperationalDataset::kExtendedPanIdBit;

    SuccessOrExit(error = mCommissioner->SetActiveDataset(activeDataset));

    MergeDataset(mActiveDataset, activeDataset);

exit:
    return error;
}

Error CommissionerApp::GetMeshLocalPrefix(std::string &aPrefix)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = mCommissioner->GetActiveDataset(mActiveDataset, 0xFFFF));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kMeshLocalPrefixBit,
                 error = ERROR_NOT_FOUND("cannot find valid Mesh-local Prefix in Active Operational Dataset"));
    aPrefix = Ipv6PrefixToString(mActiveDataset.mMeshLocalPrefix);

exit:
    return error;
}

Error CommissionerApp::SetMeshLocalPrefix(const std::string &aPrefix, MilliSeconds aDelay)
{
    Error                     error;
    PendingOperationalDataset pendingDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = Ipv6PrefixFromString(pendingDataset.mMeshLocalPrefix, aPrefix));
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kMeshLocalPrefixBit;

    pendingDataset.mDelayTimer = aDelay.count();
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kDelayTimerBit;

    SuccessOrExit(error = mCommissioner->SetPendingDataset(pendingDataset));

    MergeDataset(mPendingDataset, pendingDataset);

exit:
    return error;
}

Error CommissionerApp::GetNetworkMasterKey(ByteArray &aMasterKey)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    ;

    SuccessOrExit(error = mCommissioner->GetActiveDataset(mActiveDataset, 0xFFFF));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kNetworkMasterKeyBit,
                 error = ERROR_NOT_FOUND("cannot find valid Network Master Key in Active Operational Dataset"));
    aMasterKey = mActiveDataset.mNetworkMasterKey;

exit:
    return error;
}

Error CommissionerApp::SetNetworkMasterKey(const ByteArray &aMasterKey, MilliSeconds aDelay)
{
    Error                     error;
    PendingOperationalDataset pendingDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    pendingDataset.mNetworkMasterKey = aMasterKey;
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kNetworkMasterKeyBit;

    pendingDataset.mDelayTimer = aDelay.count();
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kDelayTimerBit;

    SuccessOrExit(error = mCommissioner->SetPendingDataset(pendingDataset));

    MergeDataset(mPendingDataset, pendingDataset);

exit:
    return error;
}

Error CommissionerApp::GetNetworkName(std::string &aNetworkName) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kNetworkNameBit,
                 error = ERROR_NOT_FOUND("cannot find valid Network Name in Active Operational Dataset"));
    aNetworkName = mActiveDataset.mNetworkName;

exit:
    return error;
}

Error CommissionerApp::SetNetworkName(const std::string &aNetworkName)
{
    Error                    error;
    ActiveOperationalDataset activeDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    activeDataset.mNetworkName = aNetworkName;
    activeDataset.mPresentFlags |= ActiveOperationalDataset::kNetworkNameBit;

    SuccessOrExit(error = mCommissioner->SetActiveDataset(activeDataset));

    MergeDataset(mActiveDataset, activeDataset);

exit:
    return error;
}

Error CommissionerApp::GetPanId(uint16_t &aPanId)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = mCommissioner->GetActiveDataset(mActiveDataset, 0xFFFF));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kPanIdBit,
                 error = ERROR_NOT_FOUND("cannot find valid PAN ID in Active Operational Dataset"));
    aPanId = mActiveDataset.mPanId;

exit:
    return error;
}

Error CommissionerApp::SetPanId(uint16_t aPanId, MilliSeconds aDelay)
{
    Error                     error;
    PendingOperationalDataset pendingDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    pendingDataset.mPanId = aPanId;
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kPanIdBit;

    pendingDataset.mDelayTimer = aDelay.count();
    pendingDataset.mPresentFlags |= PendingOperationalDataset::kDelayTimerBit;

    SuccessOrExit(error = mCommissioner->SetPendingDataset(pendingDataset));

    MergeDataset(mPendingDataset, pendingDataset);

exit:
    return error;
}

Error CommissionerApp::GetPSKc(ByteArray &aPSKc) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kPSKcBit,
                 error = ERROR_NOT_FOUND("cannot find valid PSKc in Active Operational Dataset"));
    aPSKc = mActiveDataset.mPSKc;

exit:
    return error;
}

Error CommissionerApp::SetPSKc(const ByteArray &aPSKc)
{
    Error                    error;
    ActiveOperationalDataset activeDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    activeDataset.mPSKc = aPSKc;
    activeDataset.mPresentFlags |= ActiveOperationalDataset::kPSKcBit;

    SuccessOrExit(error = mCommissioner->SetActiveDataset(activeDataset));

    MergeDataset(mActiveDataset, activeDataset);

exit:
    return error;
}

Error CommissionerApp::GetSecurityPolicy(SecurityPolicy &aSecurityPolicy) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    VerifyOrExit(mActiveDataset.mPresentFlags & ActiveOperationalDataset::kSecurityPolicyBit,
                 error = ERROR_NOT_FOUND("cannot find valid Security Policy in Active Operational Dataset"));
    aSecurityPolicy = mActiveDataset.mSecurityPolicy;

exit:
    return error;
}

Error CommissionerApp::SetSecurityPolicy(const SecurityPolicy &aSecurityPolicy)
{
    Error                    error;
    ActiveOperationalDataset activeDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    activeDataset.mSecurityPolicy = aSecurityPolicy;
    activeDataset.mPresentFlags |= ActiveOperationalDataset::kSecurityPolicyBit;

    SuccessOrExit(error = mCommissioner->SetActiveDataset(activeDataset));

    MergeDataset(mActiveDataset, activeDataset);

exit:
    return error;
}

Error CommissionerApp::GetActiveDataset(ActiveOperationalDataset &aDataset, uint16_t aDatasetFlags)
{
    Error error;

    SuccessOrExit(error = mCommissioner->GetActiveDataset(aDataset, aDatasetFlags));
    MergeDataset(mActiveDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::SetActiveDataset(const ActiveOperationalDataset &aDataset)
{
    Error error;

    SuccessOrExit(error = mCommissioner->SetActiveDataset(aDataset));
    MergeDataset(mActiveDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::GetPendingDataset(PendingOperationalDataset &aDataset, uint16_t aDatasetFlags)
{
    Error error;

    SuccessOrExit(error = mCommissioner->GetPendingDataset(aDataset, aDatasetFlags));
    MergeDataset(mPendingDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::SetPendingDataset(const PendingOperationalDataset &aDataset)
{
    Error error;

    SuccessOrExit(error = mCommissioner->SetPendingDataset(aDataset));
    MergeDataset(mPendingDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::GetTriHostname(std::string &aHostname) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    VerifyOrExit(mBbrDataset.mPresentFlags & BbrDataset::kTriHostnameBit,
                 error = ERROR_NOT_FOUND("cannot find valid TRI Hostname in BBR Dataset"));
    aHostname = mBbrDataset.mTriHostname;

exit:
    return error;
}

Error CommissionerApp::SetTriHostname(const std::string &aHostname)
{
    Error      error;
    BbrDataset bbrDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    bbrDataset.mTriHostname = aHostname;
    bbrDataset.mPresentFlags |= BbrDataset::kTriHostnameBit;

    SuccessOrExit(error = mCommissioner->SetBbrDataset(bbrDataset));

    MergeDataset(mBbrDataset, bbrDataset);

exit:
    return error;
}

Error CommissionerApp::GetRegistrarHostname(std::string &aHostname) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    VerifyOrExit(mBbrDataset.mPresentFlags & BbrDataset::kRegistrarHostnameBit,
                 error = ERROR_NOT_FOUND("cannot find valid Registrar Hostname in BBR Dataset"));
    aHostname = mBbrDataset.mRegistrarHostname;

exit:
    return error;
}

Error CommissionerApp::SetRegistrarHostname(const std::string &aHostname)
{
    Error      error;
    BbrDataset bbrDataset;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    bbrDataset.mRegistrarHostname = aHostname;
    bbrDataset.mPresentFlags |= BbrDataset::kRegistrarHostnameBit;

    SuccessOrExit(error = mCommissioner->SetBbrDataset(bbrDataset));

    MergeDataset(mBbrDataset, bbrDataset);

exit:
    return error;
}

Error CommissionerApp::GetRegistrarIpv6Addr(std::string &aIpv6Addr) const
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    VerifyOrExit(mBbrDataset.mPresentFlags & BbrDataset::kRegistrarIpv6AddrBit,
                 error = ERROR_NOT_FOUND("cannot find valid Registrar IPv6 Address in BBR Dataset"));
    aIpv6Addr = mBbrDataset.mRegistrarIpv6Addr;

exit:
    return error;
}

Error CommissionerApp::GetBbrDataset(BbrDataset &aDataset, uint16_t aDatasetFlags)
{
    Error error;

    SuccessOrExit(error = mCommissioner->GetBbrDataset(aDataset, aDatasetFlags));
    MergeDataset(mBbrDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::SetBbrDataset(const BbrDataset &aDataset)
{
    Error error;

    SuccessOrExit(error = mCommissioner->SetBbrDataset(aDataset));
    MergeDataset(mBbrDataset, aDataset);

exit:
    return error;
}

Error CommissionerApp::Reenroll(const std::string &aDstAddr)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    SuccessOrExit(error = mCommissioner->CommandReenroll(aDstAddr));

exit:
    return error;
}

Error CommissionerApp::DomainReset(const std::string &aDstAddr)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    SuccessOrExit(error = mCommissioner->CommandDomainReset(aDstAddr));

exit:
    return error;
}

Error CommissionerApp::Migrate(const std::string &aDstAddr, const std::string &aDesignatedNetwork)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));
    VerifyOrExit(IsCcmMode(), error = ERROR_INVALID_STATE("the commissioner is not in CCM Mode"));

    SuccessOrExit(error = mCommissioner->CommandMigrate(aDstAddr, aDesignatedNetwork));

exit:
    return error;
}

Error CommissionerApp::RegisterMulticastListener(const std::vector<std::string> &aMulticastAddrList, Seconds aTimeout)
{
    Error       error;
    std::string pbbrAddr;
    uint8_t     status;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = GetPrimaryBbrAddr(pbbrAddr));
    SuccessOrExit(error =
                      mCommissioner->RegisterMulticastListener(status, pbbrAddr, aMulticastAddrList, aTimeout.count()));
    VerifyOrExit(status == kMlrStatusSuccess,
                 error = ERROR_REJECTED("request was rejected with statusCode={}", status));

exit:
    return error;
}

Error CommissionerApp::AnnounceBegin(uint32_t           aChannelMask,
                                     uint8_t            aCount,
                                     MilliSeconds       aPeriod,
                                     const std::string &aDtsAddr)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = mCommissioner->AnnounceBegin(aChannelMask, aCount, aPeriod.count(), aDtsAddr));

exit:
    return error;
}

Error CommissionerApp::PanIdQuery(uint32_t aChannelMask, uint16_t aPanId, const std::string &aDstAddr)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = mCommissioner->PanIdQuery(aChannelMask, aPanId, aDstAddr));

exit:
    return error;
}

bool CommissionerApp::HasPanIdConflict(uint16_t aPanId) const
{
    return mPanIdConflicts.count(aPanId) != 0;
}

Error CommissionerApp::EnergyScan(uint32_t           aChannelMask,
                                  uint8_t            aCount,
                                  uint16_t           aPeriod,
                                  uint16_t           aScanDuration,
                                  const std::string &aDstAddr)
{
    Error error;

    VerifyOrExit(IsActive(), error = ERROR_INVALID_STATE("the commissioner is not active"));

    SuccessOrExit(error = mCommissioner->EnergyScan(aChannelMask, aCount, aPeriod, aScanDuration, aDstAddr));

exit:
    return error;
}

const EnergyReport *CommissionerApp::GetEnergyReport(const Address &aDstAddr) const
{
    auto report = mEnergyReports.find(aDstAddr);
    if (report == mEnergyReports.end())
    {
        return nullptr;
    }
    return &report->second;
}

const EnergyReportMap &CommissionerApp::GetAllEnergyReports() const
{
    return mEnergyReports;
}

const std::string &CommissionerApp::GetDomainName() const
{
    return mCommissioner->GetDomainName();
}

Error CommissionerApp::GetPrimaryBbrAddr(std::string &aAddr)
{
    Error       error;
    std::string meshLocalPrefix;

    SuccessOrExit(error = GetMeshLocalPrefix(meshLocalPrefix));
    SuccessOrExit(error = Commissioner::GetMeshLocalAddr(aAddr, meshLocalPrefix, kPrimaryBbrAloc16));

exit:
    return error;
}

const ByteArray &CommissionerApp::GetToken() const
{
    return mSignedToken;
}

Error CommissionerApp::RequestToken(const std::string &aAddr, uint16_t aPort)
{
    return mCommissioner->RequestToken(mSignedToken, aAddr, aPort);
}

Error CommissionerApp::SetToken(const ByteArray &aSignedToken, const ByteArray &aSignerCert)
{
    Error error;

    SuccessOrExit(error = mCommissioner->SetToken(aSignedToken, aSignerCert));

    mSignedToken = aSignedToken;

exit:
    return error;
}

bool CommissionerApp::JoinerKey::operator<(const JoinerKey &aOther) const
{
    return mType < aOther.mType || (mType == aOther.mType && mId < aOther.mId);
}

ByteArray &CommissionerApp::GetSteeringData(CommissionerDataset &aDataset, JoinerType aJoinerType)
{
    switch (aJoinerType)
    {
    case JoinerType::kMeshCoP:
        aDataset.mPresentFlags |= CommissionerDataset::kSteeringDataBit;
        return aDataset.mSteeringData;

    case JoinerType::kAE:
        aDataset.mPresentFlags |= CommissionerDataset::kAeSteeringDataBit;
        return aDataset.mAeSteeringData;

    case JoinerType::kNMKP:
        aDataset.mPresentFlags |= CommissionerDataset::kNmkpSteeringDataBit;
        return aDataset.mNmkpSteeringData;

    default:
        ASSERT(false);
        aDataset.mPresentFlags |= CommissionerDataset::kSteeringDataBit;
        return aDataset.mSteeringData;
    }
}

uint16_t &CommissionerApp::GetJoinerUdpPort(CommissionerDataset &aDataset, JoinerType aJoinerType)
{
    switch (aJoinerType)
    {
    case JoinerType::kMeshCoP:
        aDataset.mPresentFlags |= CommissionerDataset::kJoinerUdpPortBit;
        return aDataset.mJoinerUdpPort;

    case JoinerType::kAE:
        aDataset.mPresentFlags |= CommissionerDataset::kAeUdpPortBit;
        return aDataset.mAeUdpPort;

    case JoinerType::kNMKP:
        aDataset.mPresentFlags |= CommissionerDataset::kNmkpUdpPortBit;
        return aDataset.mNmkpUdpPort;

    default:
        ASSERT(false);
        aDataset.mPresentFlags |= CommissionerDataset::kJoinerUdpPortBit;
        return aDataset.mJoinerUdpPort;
    }
}

size_t CommissionerApp::EraseAllJoiners(JoinerType aJoinerType)
{
    size_t count  = 0;
    auto   joiner = mJoiners.begin();
    while (joiner != mJoiners.end())
    {
        if (joiner->first.mType == aJoinerType)
        {
            ++count;
            joiner = mJoiners.erase(joiner);
        }
        else
        {
            ++joiner;
        }
    }
    return count;
}

void CommissionerApp::MergeDataset(ActiveOperationalDataset &aDst, const ActiveOperationalDataset &aSrc)
{
#define TEST_AND_SET(name)                                            \
    if (aSrc.mPresentFlags & ActiveOperationalDataset::k##name##Bit)  \
    {                                                                 \
        aDst.m##name = aSrc.m##name;                                  \
        aDst.mPresentFlags |= ActiveOperationalDataset::k##name##Bit; \
    }

    TEST_AND_SET(ActiveTimestamp);
    TEST_AND_SET(Channel);
    TEST_AND_SET(ChannelMask);
    TEST_AND_SET(ExtendedPanId);
    TEST_AND_SET(MeshLocalPrefix);
    TEST_AND_SET(NetworkMasterKey);
    TEST_AND_SET(NetworkName);
    TEST_AND_SET(PanId);
    TEST_AND_SET(PSKc);
    TEST_AND_SET(SecurityPolicy);

#undef TEST_AND_SET
}

void CommissionerApp::MergeDataset(PendingOperationalDataset &aDst, const PendingOperationalDataset &aSrc)
{
    MergeDataset(static_cast<ActiveOperationalDataset &>(aDst), static_cast<const ActiveOperationalDataset &>(aSrc));

#define TEST_AND_SET(name)                                             \
    if (aSrc.mPresentFlags & PendingOperationalDataset::k##name##Bit)  \
    {                                                                  \
        aDst.m##name = aSrc.m##name;                                   \
        aDst.mPresentFlags |= PendingOperationalDataset::k##name##Bit; \
    }

    TEST_AND_SET(PendingTimestamp);
    TEST_AND_SET(DelayTimer);

#undef TEST_AND_SET
}

void CommissionerApp::MergeDataset(BbrDataset &aDst, const BbrDataset &aSrc)
{
#define TEST_AND_SET(name)                              \
    if (aSrc.mPresentFlags & BbrDataset::k##name##Bit)  \
    {                                                   \
        aDst.m##name = aSrc.m##name;                    \
        aDst.mPresentFlags |= BbrDataset::k##name##Bit; \
    }

    TEST_AND_SET(TriHostname);
    TEST_AND_SET(RegistrarHostname);
    TEST_AND_SET(RegistrarIpv6Addr);

#undef TEST_AND_SET
}

// Remove dst dataset's steering data and joiner UDP port
// if they are not presented in the src dataset.
void CommissionerApp::MergeDataset(CommissionerDataset &aDst, const CommissionerDataset &aSrc)
{
#define TEST_AND_SET(name)                                       \
    if (aSrc.mPresentFlags & CommissionerDataset::k##name##Bit)  \
    {                                                            \
        aDst.m##name = aSrc.m##name;                             \
        aDst.mPresentFlags |= CommissionerDataset::k##name##Bit; \
    }

    TEST_AND_SET(BorderAgentLocator);
    TEST_AND_SET(SessionId);

#undef TEST_AND_SET
#define TEST_AND_SET(name)                                        \
    if (aSrc.mPresentFlags & CommissionerDataset::k##name##Bit)   \
    {                                                             \
        aDst.m##name = aSrc.m##name;                              \
        aDst.mPresentFlags |= CommissionerDataset::k##name##Bit;  \
    }                                                             \
    else                                                          \
    {                                                             \
        aDst.mPresentFlags &= ~CommissionerDataset::k##name##Bit; \
    }

    TEST_AND_SET(SteeringData);
    TEST_AND_SET(AeSteeringData);
    TEST_AND_SET(NmkpSteeringData);
    TEST_AND_SET(JoinerUdpPort);
    TEST_AND_SET(AeUdpPort);
    TEST_AND_SET(NmkpUdpPort);

#undef TEST_AND_SET
}

Error CommissionerApp::ReadFile(std::string &aData, const std::string &aFilename)
{
    Error error;
    int   c;
    FILE *f = fopen(aFilename.c_str(), "r");

    VerifyOrExit(f != nullptr, error = ERROR_NOT_FOUND("cannot find file {}", aFilename));

    while ((c = fgetc(f)) != EOF)
    {
        aData.push_back(c);
    }

exit:
    return error;
}

Error CommissionerApp::ReadPemFile(ByteArray &aData, const std::string &aFilename)
{
    Error       error;
    std::string data;

    SuccessOrExit(error = ReadFile(data, aFilename));

    aData = {data.begin(), data.end()};
    aData.push_back(0);

exit:
    return error;
}

Error CommissionerApp::ReadHexStringFile(ByteArray &aData, const std::string &aFilename)
{
    Error       error;
    std::string hexString;
    ByteArray   data;

    SuccessOrExit(error = ReadFile(hexString, aFilename));

    hexString.erase(std::remove_if(hexString.begin(), hexString.end(), [](int c) { return isspace(c); }),
                    hexString.end());
    SuccessOrExit(error = utils::Hex(data, hexString));

    aData = data;

exit:
    return error;
}

Error CommissionerApp::WriteFile(const std::string &aData, const std::string &aFilename)
{
    Error error;

    std::ofstream file(aFilename);

    VerifyOrExit(file.is_open(), error = ERROR_NOT_FOUND("cannot find file {}", aFilename));

    for (auto c : aData)
    {
        file << c;
    }

exit:
    return error;
}

Error CommissionerApp::ReadConfig(AppConfig &aAppConfig, const std::string &aFilename)
{
    Error       error;
    std::string configData;

    SuccessOrExit(error = ReadFile(configData, aFilename));
    SuccessOrExit(error = AppConfigFromJson(aAppConfig, configData));

exit:
    return error;
}

void CommissionerApp::HandlePanIdConflict(const std::string *aPeerAddr,
                                          const ChannelMask *aChannelMask,
                                          const uint16_t *   aPanId,
                                          Error              aError)
{
    (void)aPeerAddr;

    SuccessOrExit(aError);

    // Main thread will wait for updates to mPanIdConflicts,
    // which guarantees no concurrent access to it.
    mPanIdConflicts[*aPanId] = *aChannelMask;

exit:
    // TODO(wgtdkp): logging
    return;
}

void CommissionerApp::HandleEnergyReport(const std::string *aPeerAddr,
                                         const ChannelMask *aChannelMask,
                                         const ByteArray *  aEnergyList,
                                         Error              aError)
{
    Address addr;

    SuccessOrExit(aError);

    addr.Set(*aPeerAddr);
    ASSERT(addr.IsValid());

    // Main thread will wait for updates to mPanIdConflicts,
    // which guarantees no concurrent access to it.
    mEnergyReports[addr] = {*aChannelMask, *aEnergyList};

exit:
    return;
}

void CommissionerApp::HandleDatasetChanged(Error error)
{
    // TODO(wgtdkp): logging
    (void)error;

    mCommissioner->GetActiveDataset(
        [this](const ActiveOperationalDataset *aDataset, Error aError) {
            if (aError.NoError())
            {
                // FIXME(wgtdkp): syncronization
                mActiveDataset = *aDataset;
            }
            else
            {
                // TODO(wgtdkp): logging
            }
        },
        0xFFFF);

    mCommissioner->GetPendingDataset(
        [this](const PendingOperationalDataset *aDataset, Error aError) {
            if (aError.NoError())
            {
                // FIXME(wgtdkp): syncronization
                mPendingDataset = *aDataset;
            }
            else
            {
                // TODO(wgtdkp): logging
            }
        },
        0xFFFF);
}

const JoinerInfo *CommissionerApp::GetJoinerInfo(JoinerType aType, const ByteArray &aJoinerId)
{
    auto joinerInfo = mJoiners.find({aType, aJoinerId});
    if (joinerInfo != mJoiners.end())
    {
        return &joinerInfo->second;
    }
    joinerInfo = mJoiners.find({aType, Commissioner::ComputeJoinerId(0)});
    if (joinerInfo != mJoiners.end())
    {
        return &joinerInfo->second;
    }
    return nullptr;
}

Error CommissionerApp::MakeConfig(Config &aConfig, const AppConfig &aAppConfig)
{
    Error error;

    aConfig = aAppConfig.mConfig;

    mCommLogStream.open(aAppConfig.mLogFile, std::ofstream::out | std::ofstream::app);
    VerifyOrExit(mCommLogStream.is_open(), error = ERROR_NOT_FOUND("cannot find file {}", aAppConfig.mLogFile));

    aConfig.mLogWriter = [this](LogLevel aLevel, const std::string &aMsg) { WriteCommLog(aLevel, aMsg); };

    if (!aAppConfig.mPSKc.empty())
    {
        SuccessOrExit(error = utils::Hex(aConfig.mPSKc, aAppConfig.mPSKc));
    }
    if (!aAppConfig.mPrivateKeyFile.empty())
    {
        SuccessOrExit(error = ReadPemFile(aConfig.mPrivateKey, aAppConfig.mPrivateKeyFile));
    }
    if (!aAppConfig.mCertificateFile.empty())
    {
        SuccessOrExit(error = ReadPemFile(aConfig.mCertificate, aAppConfig.mCertificateFile));
    }
    if (!aAppConfig.mTrustAnchorFile.empty())
    {
        SuccessOrExit(error = ReadPemFile(aConfig.mTrustAnchor, aAppConfig.mTrustAnchorFile));
    }

exit:
    return error;
}

static std::string ToString(LogLevel aLevel)
{
    switch (aLevel)
    {
    case LogLevel::kOff:
        return "off";
    case LogLevel::kCritical:
        return "critical";
    case LogLevel::kError:
        return "error";
    case LogLevel::kWarn:
        return "warn";
    case LogLevel::kInfo:
        return "info";
    case LogLevel::kDebug:
        return "debug";
    default:
        ASSERT(false);
        return "unknown";
    }
}

void CommissionerApp::WriteCommLog(LogLevel aLevel, const std::string &aMsg)
{
    ASSERT(mCommLogStream.is_open());

    char        dateBuf[64];
    std::time_t now = std::time(nullptr);
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    mCommLogStream << "[ " << dateBuf << " ] [ " << ToString(aLevel) << " ] " << aMsg << std::endl;
}

} // namespace commissioner

} // namespace ot
