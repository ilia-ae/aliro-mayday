#pragma once
// Host substitutes for the Matter SDK boundary. Production delegate code is
// compiled unchanged; these types do not implement commissioning or Wallet.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
using CHIP_ERROR = int;
constexpr CHIP_ERROR CHIP_NO_ERROR=0, CHIP_ERROR_INTERNAL=100, CHIP_ERROR_INVALID_ARGUMENT=101,
    CHIP_ERROR_BUFFER_TOO_SMALL=102, CHIP_ERROR_PROVIDER_LIST_EXHAUSTED=103;
namespace chip {
using EndpointId=uint16_t; using FabricIndex=uint8_t;
constexpr FabricIndex kUndefinedFabricIndex=0;
template<class T> class Span {
 T* pointer_=nullptr; size_t count_=0;
public:
 Span()=default; Span(T* p,size_t n):pointer_(p),count_(n){}
 template<class U,size_t N,typename=std::enable_if_t<std::is_convertible_v<U*,T*>>> Span(U (&a)[N]):Span(a,N){}
 template<class U,size_t N,typename=std::enable_if_t<std::is_convertible_v<U*,T*>>> Span(std::array<U,N>& a):Span(a.data(),N){}
 T* data() const{return pointer_;} size_t size() const{return count_;} bool empty() const{return count_==0;}
 T* begin() const{return pointer_;} T* end() const{return pointer_?pointer_+count_:nullptr;}
 void reduce_size(size_t n){count_=n;} T& operator[](size_t i) const{return pointer_[i];}
 bool data_equal(const Span& b) const{return count_==b.count_ && (!count_ || std::memcmp(pointer_,b.pointer_,count_*sizeof(T))==0);}
};
using ByteSpan=Span<const uint8_t>; using MutableByteSpan=Span<uint8_t>; using CharSpan=Span<const char>;
template<class T> class Optional { bool set_=false; T value_{};
public: Optional()=default; Optional(T v):set_(true),value_(v){} bool HasValue() const{return set_;} const T& Value() const{return value_;}
};
namespace app::Clusters::DoorLock {
enum class CredentialTypeEnum:uint8_t {kProgrammingPIN=0,kPin=1,kRfid=2,kFingerprint=3,kFingerVein=4,kFace=5,kAliroCredentialIssuerKey=6,kAliroEvictableEndpointKey=7,kAliroNonEvictableEndpointKey=8};
enum class UserStatusEnum:uint8_t {kAvailable=0,kOccupiedEnabled=1,kOccupiedDisabled=3};
enum class UserTypeEnum:uint8_t {kUnrestrictedUser=0,kNonAccessUser=1,kForcedUser=2};
enum class CredentialRuleEnum:uint8_t {kSingle=0,kDual=1,kTri=2};
class Delegate {
public:
 virtual ~Delegate()=default;
 virtual CHIP_ERROR GetAliroReaderVerificationKey(MutableByteSpan&)=0;
 virtual CHIP_ERROR GetAliroReaderGroupIdentifier(MutableByteSpan&)=0;
 virtual CHIP_ERROR GetAliroReaderGroupSubIdentifier(MutableByteSpan&)=0;
 virtual CHIP_ERROR GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(size_t,MutableByteSpan&)=0;
 virtual CHIP_ERROR GetAliroGroupResolvingKey(MutableByteSpan&)=0;
 virtual CHIP_ERROR GetAliroSupportedBLEUWBProtocolVersionAtIndex(size_t,MutableByteSpan&)=0;
 virtual uint8_t GetAliroBLEAdvertisingVersion()=0;
 virtual uint16_t GetNumberOfAliroCredentialIssuerKeysSupported()=0;
 virtual uint16_t GetNumberOfAliroEndpointKeysSupported()=0;
 virtual CHIP_ERROR SetAliroReaderConfig(const ByteSpan&,const ByteSpan&,const ByteSpan&,const Optional<ByteSpan>&)=0;
 virtual CHIP_ERROR ClearAliroReaderConfig()=0;
};
}}
