#pragma once
#include "door-lock-delegate.h"
using chip::app::Clusters::DoorLock::CredentialTypeEnum;
using chip::app::Clusters::DoorLock::UserStatusEnum;
using chip::app::Clusters::DoorLock::UserTypeEnum;
using chip::app::Clusters::DoorLock::CredentialRuleEnum;
struct CredentialStruct { CredentialTypeEnum credentialType; uint16_t credentialIndex; };
enum class DlCredentialStatus:uint8_t {kAvailable=0,kOccupied=1};
enum class DlAssetSource:uint8_t {kUnspecified=0,kMatterIM=1};
struct EmberAfPluginDoorLockCredentialInfo {
 DlCredentialStatus status=DlCredentialStatus::kAvailable; CredentialTypeEnum credentialType{};
 chip::ByteSpan credentialData; DlAssetSource creationSource{},modificationSource{};
 chip::FabricIndex createdBy{},lastModifiedBy{};
};
struct EmberAfPluginDoorLockUserInfo {
 chip::CharSpan userName; chip::Span<const CredentialStruct> credentials;
 uint32_t userUniqueId{}; UserStatusEnum userStatus=UserStatusEnum::kAvailable;
 UserTypeEnum userType{}; CredentialRuleEnum credentialRule{};
 DlAssetSource creationSource{},modificationSource{}; chip::FabricIndex createdBy{},lastModifiedBy{};
};
bool emberAfPluginDoorLockGetUser(chip::EndpointId,uint16_t,EmberAfPluginDoorLockUserInfo&);
bool emberAfPluginDoorLockSetUser(chip::EndpointId,uint16_t,chip::FabricIndex,chip::FabricIndex,const chip::CharSpan&,uint32_t,UserStatusEnum,UserTypeEnum,CredentialRuleEnum,const CredentialStruct*,size_t);
bool emberAfPluginDoorLockGetCredential(chip::EndpointId,uint16_t,CredentialTypeEnum,EmberAfPluginDoorLockCredentialInfo&);
bool emberAfPluginDoorLockSetCredential(chip::EndpointId,uint16_t,chip::FabricIndex,chip::FabricIndex,DlCredentialStatus,CredentialTypeEnum,const chip::ByteSpan&);
