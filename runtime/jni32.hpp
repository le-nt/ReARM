// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rearm::jni32 {
using Address = std::uint32_t;
using ThreadId = std::uint64_t;
using Handle = std::uint32_t;

inline constexpr std::int32_t JNI_OK = 0;
inline constexpr std::int32_t JNI_ERR = -1;
inline constexpr std::int32_t JNI_EDETACHED = -2;
inline constexpr std::int32_t JNI_EVERSION = -3;
inline constexpr std::int32_t JNI_ENOMEM = -4;
inline constexpr std::uint32_t JNI_VERSION_1_6 = 0x00010006;
inline constexpr std::uint32_t kEnvTableWords = 233;  // Through GetObjectRefType, JNI 1.6.
inline constexpr std::uint32_t kVmTableWords = 8;

class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class MemoryError : public Error { public: using Error::Error; };
class UnsupportedFunction : public Error { public: using Error::Error; };

// Implementations must bounds-check every read/write and throw MemoryError on
// unmapped/protected ranges. allocate supplies nonzero aligned guest storage;
// it must not overlap earlier allocations. No guest address is a host pointer.
// write must validate the entire destination before modifying it, so a rejected
// multi-byte write cannot leave a partially modified guest object.
class GuestMemory {
 public:
  virtual ~GuestMemory() = default;
  virtual Address allocate(std::uint32_t size, std::uint32_t alignment) = 0;
  virtual void read(Address address, std::span<std::uint8_t> destination) const = 0;
  virtual void write(Address address, std::span<const std::uint8_t> source) = 0;
  std::uint32_t readU32(Address address) const;
  void writeU32(Address address, std::uint32_t value);
  std::string readCString(Address address, std::uint32_t limit = 4096) const;
};

// Env IDs are the official JNI table indices. Invocation-table IDs are
// qualified with 0x10000 so their slot numbers cannot collide with env calls.
enum class Function : std::uint32_t {
  GetVersion = 4, FindClass = 6, Throw = 13, ThrowNew = 14,
  ExceptionOccurred = 15, ExceptionClear = 17,
  PushLocalFrame = 19, PopLocalFrame = 20, NewGlobalRef = 21,
  DeleteGlobalRef = 22, DeleteLocalRef = 23, IsSameObject = 24,
  NewLocalRef = 25, EnsureLocalCapacity = 26, GetObjectClass = 31,
  GetMethodID = 33, GetStaticMethodID = 113, RegisterNatives = 215,
  NewString = 163, GetStringLength = 164, NewStringUTF = 167,
  GetStringUTFLength = 168, GetStringRegion = 220, GetStringUTFRegion = 221,
  UnregisterNatives = 216, GetJavaVM = 219, ExceptionCheck = 228,
  GetObjectRefType = 232,
  DestroyJavaVM = 0x10003, AttachCurrentThread = 0x10004,
  DetachCurrentThread = 0x10005, GetEnv = 0x10006,
  AttachCurrentThreadAsDaemon = 0x10007,
};
enum class Table { Environment, VirtualMachine };
struct Binding {
  Address trampoline{};
  Function function{};
  Table table{};
  std::uint32_t index{};
  std::string name;
  bool supported{};
};
struct ExceptionInfo { std::string class_name; std::string message; };
struct NativeBinding {
  std::uint32_t method_id{};
  Address guest_entry{};  // ARM/Thumb tag preserved; caller validates executable mapping.
  bool is_static{};
};
struct Limits {
  std::uint32_t max_local_refs = 4096;
  std::uint32_t max_global_refs = 65536;
  std::uint32_t max_frames = 256;
  std::uint32_t max_string_bytes = 4096;
  std::uint32_t max_string_units = 4096;
  std::uint32_t max_string_storage_bytes = 16*1024*1024;
  std::uint32_t max_native_methods = 4096;
};

// An explicit JNI 1.6 host subset, not a Java bytecode VM or complete Android
// runtime. Calls are serialized internally; the external guest-memory adapter
// must synchronize unrelated guest memory accesses itself.
class Host {
 public:
  explicit Host(GuestMemory& memory, Address trampoline_base = 0xffe00000,
                Limits limits = {});
  ~Host();
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  Address vmPointer() const;  // JavaVM*: guest word containing VM table address.
  Address attachThread(ThreadId thread, bool daemon = false);
  void detachThread(ThreadId thread);
  bool isAttached(ThreadId thread) const;
  Address envPointer(ThreadId thread) const;  // JNIEnv*: guest word containing env table address.
  const std::vector<Binding>& bindings() const;
  std::optional<Binding> bindingForAddress(Address address) const;

  // args includes the leading JavaVM*/JNIEnv* argument. It contains already
  // marshalled 32-bit ABI words; ARM registers/stack marshalling is separate.
  std::uint32_t invoke(Function function, ThreadId thread,
                       std::span<const std::uint32_t> args);
  // java/lang/String is intrinsic. Registration declares another already
  // initialized host-provided class. There is
  // no Java class loading/bytecode initialization, inheritance or method execution.
  void registerClass(std::string name, bool throwable = false);
  std::uint32_t registerMethod(std::string_view class_name, std::string name,
                               std::string signature, bool is_static = false,
                               bool is_native = false);
  Handle makeObject(ThreadId thread, std::string_view class_name);
  // Host entry into an explicitly registered, already initialized class.
  Handle classHandle(ThreadId thread, std::string_view class_name);
  // Paired host-to-native transitions. Return releases all locals in that call,
  // including unmatched guest PushLocalFrames, while preserving globals/outer calls.
  void beginNativeCall(ThreadId thread);
  void endNativeCall(ThreadId thread);
  std::optional<NativeBinding> lookupNative(std::string_view class_name,
                                           std::string_view name,
                                           std::string_view signature) const;
  std::optional<ExceptionInfo> pendingException(ThreadId thread) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace rearm::jni32
