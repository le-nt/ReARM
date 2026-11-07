// SPDX-License-Identifier: Apache-2.0
#include "jni32.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace rearm::jni32 {
namespace {
void checkedRange(Address address, std::size_t size) {
  if ((size && !address) || std::uint64_t(address) + size > (std::uint64_t{1} << 32))
    throw MemoryError("JNI guest memory range is null or wraps the 32-bit address space");
}
Address offset(Address address, std::uint64_t delta) {
  if (std::uint64_t(address) + delta > std::numeric_limits<Address>::max())
    throw MemoryError("JNI guest address offset overflows");
  return address + static_cast<Address>(delta);
}
std::uint32_t status(std::int32_t value) { return static_cast<std::uint32_t>(value); }
bool supportedVersion(std::uint32_t version) {
  return version == 0x00010001 || version == 0x00010002 || version == 0x00010004 || version == JNI_VERSION_1_6;
}
const char* functionName(Function function) {
  switch (function) {
#define JNI_NAME(name) case Function::name: return #name
    JNI_NAME(GetVersion); JNI_NAME(FindClass); JNI_NAME(Throw); JNI_NAME(ThrowNew);
    JNI_NAME(ExceptionOccurred); JNI_NAME(ExceptionClear); JNI_NAME(PushLocalFrame);
    JNI_NAME(PopLocalFrame); JNI_NAME(NewGlobalRef); JNI_NAME(DeleteGlobalRef);
    JNI_NAME(DeleteLocalRef); JNI_NAME(IsSameObject); JNI_NAME(NewLocalRef);
    JNI_NAME(EnsureLocalCapacity); JNI_NAME(GetObjectClass); JNI_NAME(GetMethodID);
    JNI_NAME(GetStaticMethodID); JNI_NAME(RegisterNatives); JNI_NAME(UnregisterNatives);
    JNI_NAME(GetJavaVM); JNI_NAME(ExceptionCheck); JNI_NAME(GetObjectRefType);
    JNI_NAME(AttachCurrentThread); JNI_NAME(DetachCurrentThread); JNI_NAME(GetEnv);
    JNI_NAME(AttachCurrentThreadAsDaemon);
#undef JNI_NAME
    default: return nullptr;
  }
}
bool allowedPending(Function function) {
  return function == Function::ExceptionOccurred || function == Function::ExceptionClear ||
         function == Function::ExceptionCheck || function == Function::DeleteGlobalRef ||
         function == Function::DeleteLocalRef || function == Function::PopLocalFrame;
}
bool descriptorType(std::string_view text, std::size_t& at, bool allow_void) {
  if (at >= text.size()) return false;
  const char type = text[at++];
  if (type == 'V') return allow_void;
  if (std::string_view("BCDFIJSZ").find(type) != std::string_view::npos) return true;
  if (type == 'L') {
    const auto end = text.find(';', at);
    if (end == std::string_view::npos || end == at || text.substr(at, end - at).find_first_of(".[]()") != std::string_view::npos)
      return false;
    at = end + 1;
    return true;
  }
  if (type == '[') {
    unsigned dimensions = 1;
    while (at < text.size() && text[at] == '[') { ++at; ++dimensions; }
    return dimensions <= 255 && descriptorType(text, at, false);
  }
  return false;
}
bool methodDescriptor(std::string_view text) {
  if (text.empty() || text[0] != '(') return false;
  std::size_t at = 1;
  while (at < text.size() && text[at] != ')') if (!descriptorType(text, at, false)) return false;
  if (at == text.size()) return false;
  ++at;
  return descriptorType(text, at, true) && at == text.size();
}
}  // namespace

std::uint32_t GuestMemory::readU32(Address address) const {
  checkedRange(address, 4);
  std::array<std::uint8_t, 4> bytes{};
  read(address, bytes);
  return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
         (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
void GuestMemory::writeU32(Address address, std::uint32_t value) {
  checkedRange(address, 4);
  const std::array<std::uint8_t, 4> bytes{std::uint8_t(value), std::uint8_t(value >> 8),
                                       std::uint8_t(value >> 16), std::uint8_t(value >> 24)};
  write(address, bytes);
}
std::string GuestMemory::readCString(Address address, std::uint32_t limit) const {
  if (!address || !limit) throw MemoryError("JNI C string is null or has no bounded capacity");
  std::string result;
  for (std::uint32_t i = 0; i < limit; ++i) {
    const auto at = offset(address, i);
    std::array<std::uint8_t, 1> byte{};
    read(at, byte);
    if (!byte[0]) return result;
    result.push_back(static_cast<char>(byte[0]));
  }
  throw MemoryError("JNI guest C string exceeds configured byte limit");
}

struct Host::Impl {
  enum class Kind { Class, Instance, Throwable };
  struct Object { Kind kind; std::string class_name; std::string message; };
  struct Method { std::uint32_t id{}; bool is_static{}; bool is_native{}; std::optional<Address> entry; };
  using MethodKey = std::pair<std::string, std::string>;
  struct Class { std::shared_ptr<Object> object; bool throwable{}; std::map<MethodKey, Method> methods; };
  struct Reference { std::shared_ptr<Object> object; bool global{}; ThreadId owner{}; std::size_t frame{}; };
  struct Thread {
    Address env{};
    bool daemon{};
    std::string name;
    std::shared_ptr<Object> pending;
    std::vector<std::vector<Handle>> frames{1};
    std::uint32_t local_count{};
  };

  GuestMemory& memory;
  Address trampoline_base;
  Limits limits;
  Address vm{}, vm_table{}, env_table{};
  std::vector<Binding> bindings;
  std::map<std::string, Class, std::less<>> classes;
  std::map<ThreadId, Thread> threads;
  std::map<Handle, Reference> references;
  Handle next_handle = 0x10000000;
  std::uint32_t next_method = 0x20000000;
  std::uint32_t global_count{};
  mutable std::mutex mutex;

  Impl(GuestMemory& memory_, Address base, Limits limits_) : memory(memory_), trampoline_base(base), limits(limits_) {
    if ((base & 3) || std::uint64_t(base) + 0x2000 > (std::uint64_t{1} << 32))
      throw Error("JNI trampoline arena must be 4-byte aligned and fit in guest address space");
    if (!limits.max_frames || !limits.max_local_refs || !limits.max_global_refs || !limits.max_native_methods || !limits.max_string_bytes)
      throw Error("JNI limits must be positive");
    env_table = allocate(kEnvTableWords * 4);
    vm_table = allocate(kVmTableWords * 4);
    vm = allocate(4);
    memory.writeU32(vm, vm_table);
    for (std::uint32_t index = 4; index < kEnvTableWords; ++index) bind(Table::Environment, index, env_table);
    for (std::uint32_t index = 3; index < kVmTableWords; ++index) bind(Table::VirtualMachine, index, vm_table);
  }
  Address allocate(std::uint32_t size) {
    const Address result = memory.allocate(size, 4);
    checkedRange(result, size);
    if (result & 3) throw MemoryError("JNI guest allocator returned an unaligned allocation");
    if (std::uint64_t(result) < std::uint64_t(trampoline_base) + 0x2000 &&
        std::uint64_t(result) + size > trampoline_base)
      throw MemoryError("JNI guest allocation overlaps reserved callback arena");
    memory.write(result, std::vector<std::uint8_t>(size));
    return result;
  }
  void bind(Table table, std::uint32_t index, Address table_address) {
    const auto id = index + (table == Table::VirtualMachine ? 0x10000 : 0);
    const auto function = static_cast<Function>(id);
    const auto trampoline = trampoline_base + (table == Table::VirtualMachine ? 0x1000 : 0) + index * 4;
    const char* name = functionName(function);
    const auto fallback = std::string(table == Table::Environment ? "JNIEnv" : "JavaVM") + "[" + std::to_string(index) + "]";
    bindings.push_back({trampoline, function, table, index, name ? name : fallback, name != nullptr});
    memory.writeU32(table_address + index * 4, trampoline);
  }
  Thread& thread(ThreadId id) {
    const auto found = threads.find(id);
    if (found == threads.end()) throw Error("JNI call from detached guest thread " + std::to_string(id));
    return found->second;
  }
  Address attach(ThreadId id, bool daemon, std::string name = {}) {
    if (const auto it = threads.find(id); it != threads.end()) return it->second.env;
    Thread value;
    value.env = allocate(4);
    value.daemon = daemon;
    value.name = std::move(name);
    memory.writeU32(value.env, env_table);
    const Address env = value.env;
    threads.emplace(id, std::move(value));
    return env;
  }
  void detach(ThreadId id) {
    const auto found = threads.find(id);
    if (found == threads.end()) return;  // Invocation API specifies a no-op.
    for (const auto& frame : found->second.frames) for (const Handle handle : frame) references.erase(handle);
    threads.erase(found);
  }
  const Reference& reference(ThreadId id, Handle handle) const {
    const auto found = references.find(handle);
    if (!handle || found == references.end() || (!found->second.global && found->second.owner != id))
      throw Error("Invalid or foreign-thread JNI reference " + std::to_string(handle));
    return found->second;
  }
  std::shared_ptr<Object> object(ThreadId id, Handle handle) const {
    return handle ? reference(id, handle).object : nullptr;
  }
  Class& classReference(ThreadId id, Handle handle) {
    const auto value = object(id, handle);
    if (!value || value->kind != Kind::Class) throw Error("JNI class argument is not a class reference");
    return classes.at(value->class_name);
  }
  void exception(ThreadId id, std::string name, std::string message) {
    thread(id).pending = std::make_shared<Object>(Object{Kind::Throwable, std::move(name), std::move(message)});
  }
  Handle newReference(ThreadId id, std::shared_ptr<Object> value, bool global = false) {
    if (!value) return 0;
    auto& state = thread(id);
    if ((!global && state.local_count >= limits.max_local_refs) || (global && global_count >= limits.max_global_refs)) {
      exception(id, "java/lang/OutOfMemoryError", "JNI reference limit exceeded");
      return 0;
    }
    if (next_handle >= 0x1fffffff) throw Error("JNI reference handle space exhausted");
    const Handle result = ++next_handle;
    references.emplace(result, Reference{std::move(value), global, id, state.frames.size() - 1});
    if (!global) { state.frames.back().push_back(result); ++state.local_count; }
    else ++global_count;
    return result;
  }
  void deleteReference(ThreadId id, Handle handle, bool global) {
    if (!handle) return;
    const auto value = reference(id, handle);
    if (value.global != global) throw Error("JNI reference deletion kind mismatch");
    if (!global) {
      auto& state = thread(id);
      auto& frame = state.frames.at(value.frame);
      frame.erase(std::find(frame.begin(), frame.end(), handle));
      --state.local_count;
    } else --global_count;
    references.erase(handle);
  }
  std::uint32_t ensureCapacity(ThreadId id, std::uint32_t raw) {
    const auto capacity = std::bit_cast<std::int32_t>(raw);
    if (capacity < 0) throw Error("JNI local capacity must not be negative");
    if (raw > limits.max_local_refs - thread(id).local_count) {
      exception(id, "java/lang/OutOfMemoryError", "JNI local capacity exceeds configured limit");
      return status(JNI_ENOMEM);
    }
    return status(JNI_OK);
  }
  void arity(std::span<const std::uint32_t> args, std::size_t expected) {
    if (args.size() != expected) throw Error("JNI callback argument count mismatch");
  }
  std::uint32_t vmCall(Function function, ThreadId id, std::span<const std::uint32_t> args) {
    arity(args, function == Function::DetachCurrentThread ? 1 : 3);
    if (args[0] != vm) throw Error("JNI invocation uses an invalid JavaVM pointer");
    if (function == Function::DetachCurrentThread) { detach(id); return status(JNI_OK); }
    memory.writeU32(args[1], 0);  // Validate writable out-pointer before changing thread state.
    if (function == Function::GetEnv) {
      if (!threads.contains(id)) return status(JNI_EDETACHED);
      if (!supportedVersion(args[2])) return status(JNI_EVERSION);
      memory.writeU32(args[1], thread(id).env);
      return status(JNI_OK);
    }
    if (const auto found = threads.find(id); found != threads.end()) {
      memory.writeU32(args[1], found->second.env);
      return status(JNI_OK);
    }
    std::string name;
    if (args[2]) {
      const Address attach_args = args[2];
      checkedRange(attach_args, 12);
      if (!supportedVersion(memory.readU32(attach_args))) return status(JNI_EVERSION);
      const Address name_pointer = memory.readU32(offset(attach_args, 4));
      const Handle group = memory.readU32(offset(attach_args, 8));
      if (name_pointer) name = memory.readCString(name_pointer, limits.max_string_bytes);
      if (group) {
        const auto& ref = reference(id, group);
        if (!ref.global || ref.object->kind != Kind::Instance || ref.object->class_name != "java/lang/ThreadGroup")
          throw Error("JNI attach group must be a global ThreadGroup reference");
      }
    }
    const Address env = attach(id, function == Function::AttachCurrentThreadAsDaemon, std::move(name));
    memory.writeU32(args[1], env);
    return status(JNI_OK);
  }
  std::uint32_t call(Function function, ThreadId id, std::span<const std::uint32_t> args) {
    const char* name = functionName(function);
    if (!name) throw UnsupportedFunction("Unsupported JNI callback ID " + std::to_string(static_cast<std::uint32_t>(function)));
    if (static_cast<std::uint32_t>(function) >= 0x10000) return vmCall(function, id, args);
    auto& state = thread(id);
    if (args.empty() || args[0] != state.env) throw Error("JNI callback uses invalid or foreign-thread JNIEnv pointer");
    if (state.pending && !allowedPending(function)) throw Error(std::string("JNI call with pending exception: ") + name);
    switch (function) {
      case Function::GetVersion: arity(args, 1); return JNI_VERSION_1_6;
      case Function::FindClass: {
        arity(args, 2);
        const auto class_name = memory.readCString(args[1], limits.max_string_bytes);
        const auto found = classes.find(class_name);
        if (found == classes.end()) { exception(id, "java/lang/NoClassDefFoundError", class_name); return 0; }
        return newReference(id, found->second.object);
      }
      case Function::GetObjectClass: {
        arity(args, 2);
        const auto value = object(id, args[1]);
        if (!value) throw Error("GetObjectClass requires non-null object");
        const auto class_name = value->kind == Kind::Class ? "java/lang/Class" : value->class_name;
        const auto found = classes.find(class_name);
        if (found == classes.end()) { exception(id, "java/lang/NoClassDefFoundError", class_name); return 0; }
        return newReference(id, found->second.object);
      }
      case Function::GetMethodID:
      case Function::GetStaticMethodID: {
        arity(args, 4);
        auto& klass = classReference(id, args[1]);
        const auto method = memory.readCString(args[2], limits.max_string_bytes);
        const auto signature = memory.readCString(args[3], limits.max_string_bytes);
        const auto found = klass.methods.find({method, signature});
        if (found == klass.methods.end() || found->second.is_static != (function == Function::GetStaticMethodID)) {
          exception(id, "java/lang/NoSuchMethodError", method + signature); return 0;
        }
        return found->second.id;
      }
      case Function::ExceptionCheck: arity(args, 1); return state.pending ? 1 : 0;
      case Function::ExceptionOccurred: arity(args, 1); return newReference(id, state.pending);
      case Function::ExceptionClear: arity(args, 1); state.pending.reset(); return 0;
      case Function::Throw: {
        arity(args, 2);
        auto value = object(id, args[1]);
        if (!value || value->kind != Kind::Throwable) throw Error("Throw requires a throwable reference");
        state.pending = std::move(value); return status(JNI_OK);
      }
      case Function::ThrowNew: {
        arity(args, 3);
        auto& klass = classReference(id, args[1]);
        if (!klass.throwable) throw Error("ThrowNew requires an explicitly registered throwable class");
        const auto message = args[2] ? memory.readCString(args[2], limits.max_string_bytes) : std::string{};
        exception(id, klass.object->class_name, message); return status(JNI_OK);
      }
      case Function::NewGlobalRef:
      case Function::NewLocalRef: arity(args, 2); return newReference(id, object(id, args[1]), function == Function::NewGlobalRef);
      case Function::DeleteGlobalRef:
      case Function::DeleteLocalRef: arity(args, 2); deleteReference(id, args[1], function == Function::DeleteGlobalRef); return 0;
      case Function::IsSameObject: arity(args, 3); return object(id, args[1]) == object(id, args[2]) ? 1 : 0;
      case Function::GetObjectRefType: {
        arity(args, 2);
        const auto found = references.find(args[1]);
        if (found == references.end() || (!found->second.global && found->second.owner != id)) return 0;
        return found->second.global ? 2 : 1;
      }
      case Function::EnsureLocalCapacity: arity(args, 2); return ensureCapacity(id, args[1]);
      case Function::PushLocalFrame: {
        arity(args, 2);
        if (const auto result = ensureCapacity(id, args[1]); result != status(JNI_OK)) return result;
        if (state.frames.size() >= limits.max_frames) {
          exception(id, "java/lang/OutOfMemoryError", "JNI frame limit exceeded"); return status(JNI_ENOMEM);
        }
        state.frames.emplace_back();
        state.frames.back().reserve(args[1]);
        return status(JNI_OK);
      }
      case Function::PopLocalFrame: {
        arity(args, 2);
        if (state.frames.size() == 1) throw Error("Cannot pop the JNI base local frame");
        auto result = object(id, args[1]);
        for (const auto handle : state.frames.back()) { references.erase(handle); --state.local_count; }
        state.frames.pop_back();
        return newReference(id, std::move(result));
      }
      case Function::RegisterNatives: {
        arity(args, 4);
        auto& klass = classReference(id, args[1]);
        const auto count = std::bit_cast<std::int32_t>(args[3]);
        if (count <= 0 || args[3] > limits.max_native_methods) {
          exception(id, "java/lang/IllegalArgumentException", "Invalid JNI native method count"); return status(JNI_ERR);
        }
        checkedRange(args[2], std::size_t(args[3]) * 12);
        std::vector<std::pair<Method*, Address>> updates;
        for (std::uint32_t index = 0; index < args[3]; ++index) {
          const auto at = offset(args[2], std::uint64_t(index) * 12);
          const auto method = memory.readCString(memory.readU32(at), limits.max_string_bytes);
          const auto signature = memory.readCString(memory.readU32(offset(at, 4)), limits.max_string_bytes);
          const Address entry = memory.readU32(offset(at, 8));
          const auto found = klass.methods.find({method, signature});
          if (found == klass.methods.end() || !found->second.is_native) {
            exception(id, "java/lang/NoSuchMethodError", method + signature); return status(JNI_ERR);
          }
          if (!(entry & ~Address{1}) || (!(entry & 1) && (entry & 3))) {
            exception(id, "java/lang/IllegalArgumentException", "Invalid ARM/Thumb native entry"); return status(JNI_ERR);
          }
          updates.emplace_back(&found->second, entry);
        }
        for (const auto& [method, entry] : updates) method->entry = entry;
        return status(JNI_OK);
      }
      case Function::UnregisterNatives: {
        arity(args, 2);
        for (auto& [key, method] : classReference(id, args[1]).methods) method.entry.reset();
        return status(JNI_OK);
      }
      case Function::GetJavaVM: arity(args, 2); memory.writeU32(args[1], vm); return status(JNI_OK);
      default: throw UnsupportedFunction(std::string("Unsupported JNI function ") + name);
    }
  }
};

Host::Host(GuestMemory& memory, Address trampoline_base, Limits limits)
    : impl_(std::make_unique<Impl>(memory, trampoline_base, limits)) {}
Host::~Host() = default;
Address Host::vmPointer() const { return impl_->vm; }
Address Host::attachThread(ThreadId id, bool daemon) {
  const std::lock_guard lock(impl_->mutex); return impl_->attach(id, daemon);
}
void Host::detachThread(ThreadId id) { const std::lock_guard lock(impl_->mutex); impl_->detach(id); }
bool Host::isAttached(ThreadId id) const { const std::lock_guard lock(impl_->mutex); return impl_->threads.contains(id); }
Address Host::envPointer(ThreadId id) const { const std::lock_guard lock(impl_->mutex); return impl_->thread(id).env; }
const std::vector<Binding>& Host::bindings() const { return impl_->bindings; }
std::optional<Binding> Host::bindingForAddress(Address address) const {
  const auto found = std::find_if(impl_->bindings.begin(), impl_->bindings.end(), [address](const Binding& value) { return value.trampoline == address; });
  return found == impl_->bindings.end() ? std::nullopt : std::optional<Binding>(*found);
}
std::uint32_t Host::invoke(Function function, ThreadId id, std::span<const std::uint32_t> args) {
  const std::lock_guard lock(impl_->mutex); return impl_->call(function, id, args);
}
void Host::registerClass(std::string name, bool throwable) {
  const std::lock_guard lock(impl_->mutex);
  if (name.empty() || name.find_first_of(".;[\0", 0, 4) != std::string::npos || name.front() == '/' || name.back() == '/')
    throw Error("Invalid explicit JNI class name");
  if (impl_->classes.contains(name)) throw Error("JNI class already registered: " + name);
  auto object = std::make_shared<Impl::Object>(Impl::Object{Impl::Kind::Class, name, {}});
  impl_->classes.emplace(std::move(name), Impl::Class{std::move(object), throwable, {}});
}
std::uint32_t Host::registerMethod(std::string_view class_name, std::string name, std::string signature, bool is_static, bool is_native) {
  const std::lock_guard lock(impl_->mutex);
  const auto found = impl_->classes.find(class_name);
  if (found == impl_->classes.end()) throw Error("Method class is not registered");
  if (name.empty() || name.find('\0') != std::string::npos || signature.find('\0') != std::string::npos || !methodDescriptor(signature)) throw Error("Invalid JNI method declaration");
  if (impl_->next_method >= 0x2fffffff) throw Error("JNI method ID space exhausted");
  const auto id = ++impl_->next_method;
  if (!found->second.methods.emplace(Impl::MethodKey{std::move(name), std::move(signature)}, Impl::Method{id, is_static, is_native, {}}).second)
    throw Error("JNI method already registered");
  return id;
}
Handle Host::makeObject(ThreadId id, std::string_view class_name) {
  const std::lock_guard lock(impl_->mutex);
  const auto found = impl_->classes.find(class_name);
  if (found == impl_->classes.end()) throw Error("Object class is not registered");
  auto value = std::make_shared<Impl::Object>(Impl::Object{found->second.throwable ? Impl::Kind::Throwable : Impl::Kind::Instance, std::string(class_name), {}});
  return impl_->newReference(id, std::move(value));
}
std::optional<NativeBinding> Host::lookupNative(std::string_view class_name, std::string_view name, std::string_view signature) const {
  const std::lock_guard lock(impl_->mutex);
  const auto klass = impl_->classes.find(class_name);
  if (klass == impl_->classes.end()) return std::nullopt;
  const auto method = klass->second.methods.find({std::string(name), std::string(signature)});
  if (method == klass->second.methods.end() || !method->second.entry) return std::nullopt;
  return NativeBinding{method->second.id, *method->second.entry, method->second.is_static};
}
std::optional<ExceptionInfo> Host::pendingException(ThreadId id) const {
  const std::lock_guard lock(impl_->mutex);
  const auto& state = impl_->thread(id);
  if (!state.pending) return std::nullopt;
  return ExceptionInfo{state.pending->class_name, state.pending->message};
}
}  // namespace rearm::jni32
