// SPDX-License-Identifier: Apache-2.0
#include "runtime/jni32.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

using namespace rearm::jni32;
namespace {
int passed = 0, failed = 0;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template <typename Exception = Error, typename F> void throws(F&& action) {
  try { action(); } catch (const Exception&) { return; }
  throw std::runtime_error("Expected diagnostic exception was not thrown");
}
void test(const char* name, const std::function<void()>& body) {
  try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
}
class Memory final : public GuestMemory {
 public:
  std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(1024 * 1024);
  Address next = 0x1000;
  Address allocate(std::uint32_t size, std::uint32_t alignment) override {
    const std::uint64_t result = (std::uint64_t(next) + alignment - 1) & ~(std::uint64_t(alignment) - 1);
    if (!size || result + size > bytes.size()) throw MemoryError("Test guest allocator exhausted");
    next = static_cast<Address>(result + size);
    return static_cast<Address>(result);
  }
  void read(Address address, std::span<std::uint8_t> output) const override {
    if (!address || std::uint64_t(address) + output.size() > bytes.size()) throw MemoryError("Test guest read outside mapping");
    std::copy_n(bytes.begin() + address, output.size(), output.begin());
  }
  void write(Address address, std::span<const std::uint8_t> input) override {
    if (!address || std::uint64_t(address) + input.size() > bytes.size()) throw MemoryError("Test guest write outside mapping");
    std::copy(input.begin(), input.end(), bytes.begin() + address);
  }
  Address string(std::string_view value) {
    const auto result = allocate(static_cast<std::uint32_t>(value.size() + 1), 4);
    std::vector<std::uint8_t> encoded(value.begin(), value.end());
    encoded.push_back(0);
    write(result, encoded);
    return result;
  }
};
std::uint32_t call(Host& host, Function function, ThreadId thread, std::initializer_list<std::uint32_t> args) {
  return host.invoke(function, thread, {args.begin(), args.size()});
}
std::uint32_t result(std::int32_t value) { return static_cast<std::uint32_t>(value); }
struct Fixture {
  Memory memory;
  Host host;
  Address env;
  Fixture(Limits limits = {}) : host(memory, 0xffe00000, limits), env(host.attachThread(1)) { host.registerClass("test/Engine"); }
  Handle klass() { return call(host, Function::FindClass, 1, {env, memory.string("test/Engine")}); }
  void clear() { call(host, Function::ExceptionClear, 1, {env}); }
  Address method(std::string_view name, std::string_view signature, Address entry) {
    const auto array = memory.allocate(12, 4);
    memory.writeU32(array, memory.string(name));
    memory.writeU32(array + 4, memory.string(signature));
    memory.writeU32(array + 8, entry);
    return array;
  }
};
}  // namespace

int main() {
  // Indices are independently documented by Oracle JNI 17 and OpenJDK jdk-17-ga
  // jni.h. JNI 1.6 ends at slot 232; newer Java-only extensions are excluded.
  static_assert(static_cast<unsigned>(Function::GetVersion) == 4);
  static_assert(static_cast<unsigned>(Function::FindClass) == 6);
  static_assert(static_cast<unsigned>(Function::GetMethodID) == 33);
  static_assert(static_cast<unsigned>(Function::GetStaticMethodID) == 113);
  static_assert(static_cast<unsigned>(Function::RegisterNatives) == 215);
  static_assert(static_cast<unsigned>(Function::GetJavaVM) == 219);
  static_assert(static_cast<unsigned>(Function::ExceptionCheck) == 228);
  static_assert(static_cast<unsigned>(Function::GetObjectRefType) == 232);
  test("actual 32-bit JNIEnv and JavaVM pointer indirection", [] {
    Fixture f;
    const auto env_table = f.memory.readU32(f.env);
    const auto vm_table = f.memory.readU32(f.host.vmPointer());
    check(env_table != f.env && vm_table != f.host.vmPointer(), "table pointer indirection missing");
    check(env_table != vm_table, "env and VM tables alias");
    for (unsigned i = 0; i < 4; ++i) check(f.memory.readU32(env_table + i * 4) == 0, "env reserved slot not zero");
    for (unsigned i = 0; i < 3; ++i) check(f.memory.readU32(vm_table + i * 4) == 0, "VM reserved slot not zero");
    const auto get_version = f.host.bindingForAddress(f.memory.readU32(env_table + 4 * 4));
    check(get_version && get_version->function == Function::GetVersion && get_version->supported, "slot4 does not bind GetVersion");
    const auto get_env = f.host.bindingForAddress(f.memory.readU32(vm_table + 6 * 4));
    check(get_env && get_env->function == Function::GetEnv && get_env->table == Table::VirtualMachine, "VM slot6 does not bind GetEnv");
    check(f.host.bindings().size() == (233 - 4) + (8 - 3), "unexpected table descriptor count");
  });
  test("all nonreserved slots hold distinct guest callbacks, never host pointers", [] {
    Fixture f;
    std::vector<Address> addresses;
    for (const auto& binding : f.host.bindings()) {
      check(binding.trampoline >= 0xffe00000 && binding.trampoline < 0xffe02000, "callback outside reserved guest arena");
      check(!(binding.trampoline & 3), "callback not ARM aligned");
      const auto table = f.memory.readU32(binding.table == Table::Environment ? f.env : f.host.vmPointer());
      check(f.memory.readU32(table + 4 * binding.index) == binding.trampoline, "table offset mismatch");
      addresses.push_back(binding.trampoline);
    }
    std::sort(addresses.begin(), addresses.end());
    check(std::adjacent_find(addresses.begin(), addresses.end()) == addresses.end(), "duplicate callback addresses");
  });
  test("unsupported table callback traps explicitly", [] {
    Fixture f;
    const auto env_table = f.memory.readU32(f.env);
    const auto binding = f.host.bindingForAddress(f.memory.readU32(env_table + 224 * 4));
    check(binding && !binding->supported, "unsupported GetStringCritical was advertised");
    throws<UnsupportedFunction>([&] { call(f.host, binding->function, 1, {f.env, 0}); });
    throws<UnsupportedFunction>([&] { call(f.host, Function::DestroyJavaVM, 1, {f.host.vmPointer()}); });
  });
  test("GetVersion advertises implemented JNI 1.6 profile", [] {
    Fixture f;
    check(call(f.host, Function::GetVersion, 1, {f.env}) == JNI_VERSION_1_6, "wrong JNI version");
  });
  test("GetEnv writes attached environment through guest out-pointer", [] {
    Fixture f;
    const auto out = f.memory.allocate(4, 4);
    check(call(f.host, Function::GetEnv, 1, {f.host.vmPointer(), out, JNI_VERSION_1_6}) == 0, "GetEnv failed");
    check(f.memory.readU32(out) == f.env, "GetEnv did not write JNIEnv pointer");
  });
  test("GetEnv detached thread returns EDETACHED and null", [] {
    Fixture f;
    const auto out = f.memory.allocate(4, 4);
    f.memory.writeU32(out, 0x1234);
    check(call(f.host, Function::GetEnv, 2, {f.host.vmPointer(), out, JNI_VERSION_1_6}) == result(JNI_EDETACHED), "detached thread accepted");
    check(f.memory.readU32(out) == 0 && !f.host.isAttached(2), "GetEnv unexpectedly attached thread");
  });
  test("GetEnv unsupported version returns EVERSION and null", [] {
    Fixture f;
    const auto out = f.memory.allocate(4, 4);
    check(call(f.host, Function::GetEnv, 1, {f.host.vmPointer(), out, 0x00010008}) == result(JNI_EVERSION), "JNI1.8 falsely accepted");
    check(f.memory.readU32(out) == 0, "unsupported version returned env");
  });
  test("attach detach and daemon attach maintain separate environments", [] {
    Fixture f;
    const auto out = f.memory.allocate(4, 4);
    check(call(f.host, Function::AttachCurrentThreadAsDaemon, 2, {f.host.vmPointer(), out, 0}) == 0, "daemon attach failed");
    const auto env2 = f.memory.readU32(out);
    check(env2 != f.env && f.host.envPointer(2) == env2, "thread envs alias");
    check(call(f.host, Function::GetVersion, 2, {env2}) == JNI_VERSION_1_6, "env2 unusable");
    call(f.host, Function::AttachCurrentThread, 2, {f.host.vmPointer(), out, 0xffffffff});
    check(f.memory.readU32(out) == env2, "reattach changed env or inspected ignored args");
    call(f.host, Function::DetachCurrentThread, 2, {f.host.vmPointer()});
    check(!f.host.isAttached(2), "detach did not remove thread");
    check(call(f.host, Function::DetachCurrentThread, 2, {f.host.vmPointer()}) == 0, "detached no-op did not succeed");
  });
  test("attach validates version and writable output before thread mutation", [] {
    Fixture f;
    const auto out = f.memory.allocate(4, 4), args = f.memory.allocate(12, 4);
    f.memory.writeU32(args, 0x00010008);
    check(call(f.host, Function::AttachCurrentThread, 2, {f.host.vmPointer(), out, args}) == result(JNI_EVERSION), "invalid attach version accepted");
    check(!f.host.isAttached(2) && f.memory.readU32(out) == 0, "failed attach changed state");
    throws<MemoryError>([&] { call(f.host, Function::AttachCurrentThread, 2, {f.host.vmPointer(), 0xfffffffe, 0}); });
    check(!f.host.isAttached(2), "invalid output pointer attached thread");
  });
  test("foreign and stale environment pointers are rejected", [] {
    Fixture f;
    const auto env2 = f.host.attachThread(2);
    throws([&] { call(f.host, Function::GetVersion, 2, {f.env}); });
    f.host.detachThread(2);
    f.host.attachThread(2);
    throws([&] { call(f.host, Function::GetVersion, 2, {env2}); });
    throws([&] { call(f.host, Function::GetEnv, 1, {f.host.vmPointer() + 4, f.memory.allocate(4, 4), JNI_VERSION_1_6}); });
  });
  test("FindClass resolves only explicitly registered classes", [] {
    Fixture f;
    check(f.klass() != 0, "registered class not found");
    check(call(f.host, Function::FindClass, 1, {f.env, f.memory.string("absent/Class")}) == 0, "missing class fabricated");
    check(f.host.pendingException(1)->class_name == "java/lang/NoClassDefFoundError", "missing class did not set exception");
  });
  test("pending exceptions are thread-local and must be cleared", [] {
    Fixture f;
    const auto env2 = f.host.attachThread(2);
    call(f.host, Function::FindClass, 1, {f.env, f.memory.string("missing/Class")});
    check(call(f.host, Function::ExceptionCheck, 1, {f.env}) == 1, "missing pending exception");
    check(call(f.host, Function::ExceptionCheck, 2, {env2}) == 0, "exception leaked across threads");
    throws([&] { call(f.host, Function::GetVersion, 1, {f.env}); });
    f.clear();
    check(call(f.host, Function::ExceptionCheck, 1, {f.env}) == 0, "clear failed");
  });
  test("ExceptionOccurred creates a reference surviving clear and rethrow", [] {
    Fixture f;
    call(f.host, Function::FindClass, 1, {f.env, f.memory.string("missing/Class")});
    const auto throwable = call(f.host, Function::ExceptionOccurred, 1, {f.env});
    check(throwable != 0, "pending exception has no reference");
    f.clear();
    check(call(f.host, Function::Throw, 1, {f.env, throwable}) == 0, "rethrow failed");
    check(f.host.pendingException(1)->message == "missing/Class", "exception object lost content");
  });
  test("ThrowNew requires a registered throwable class", [] {
    Fixture f;
    throws([&] { call(f.host, Function::ThrowNew, 1, {f.env, f.klass(), 0}); });
    f.host.registerClass("test/Failure", true);
    const auto klass = call(f.host, Function::FindClass, 1, {f.env, f.memory.string("test/Failure")});
    call(f.host, Function::ThrowNew, 1, {f.env, klass, f.memory.string("test reason")});
    check(f.host.pendingException(1)->class_name == "test/Failure" && f.host.pendingException(1)->message == "test reason", "ThrowNew state incorrect");
  });
  test("host-created throwable objects can be thrown", [] {
    Fixture f;
    f.host.registerClass("test/Failure", true);
    const auto object = f.host.makeObject(1, "test/Failure");
    check(call(f.host, Function::Throw, 1, {f.env, object}) == 0, "host throwable rejected");
    check(f.host.pendingException(1)->class_name == "test/Failure", "host throwable class lost");
  });
  test("local and global references preserve identity but use distinct handles", [] {
    Fixture f;
    const auto local = f.klass();
    const auto global = call(f.host, Function::NewGlobalRef, 1, {f.env, local});
    const auto local2 = call(f.host, Function::NewLocalRef, 1, {f.env, global});
    check(local != global && global != local2, "references not distinct");
    check(call(f.host, Function::IsSameObject, 1, {f.env, local, global}) == 1, "reference identity lost");
    check(call(f.host, Function::GetObjectRefType, 1, {f.env, local}) == 1, "wrong local reference kind");
    check(call(f.host, Function::GetObjectRefType, 1, {f.env, global}) == 2, "wrong global reference kind");
  });
  test("local references cannot cross guest threads; globals can", [] {
    Fixture f;
    const auto local = f.klass(), global = call(f.host, Function::NewGlobalRef, 1, {f.env, local});
    const auto env2 = f.host.attachThread(2);
    check(call(f.host, Function::GetObjectRefType, 2, {env2, local}) == 0, "foreign local reported valid");
    throws([&] { call(f.host, Function::NewLocalRef, 2, {env2, local}); });
    check(call(f.host, Function::NewLocalRef, 2, {env2, global}) != 0, "global unusable in second thread");
  });
  test("null references follow JNI reference operation semantics", [] {
    Fixture f;
    check(call(f.host, Function::NewLocalRef, 1, {f.env, 0}) == 0, "null local changed");
    check(call(f.host, Function::NewGlobalRef, 1, {f.env, 0}) == 0, "null global changed");
    check(call(f.host, Function::IsSameObject, 1, {f.env, 0, 0}) == 1, "null identity incorrect");
    call(f.host, Function::DeleteLocalRef, 1, {f.env, 0});
    call(f.host, Function::DeleteGlobalRef, 1, {f.env, 0});
  });
  test("reference deletion validates kind and invalidates only that handle", [] {
    Fixture f;
    const auto local = f.klass(), global = call(f.host, Function::NewGlobalRef, 1, {f.env, local});
    throws([&] { call(f.host, Function::DeleteGlobalRef, 1, {f.env, local}); });
    call(f.host, Function::DeleteLocalRef, 1, {f.env, local});
    check(call(f.host, Function::GetObjectRefType, 1, {f.env, local}) == 0, "deleted local remains valid");
    check(call(f.host, Function::NewLocalRef, 1, {f.env, global}) != 0, "global was deleted with local");
  });
  test("popping frames invalidates locals and preserves requested result", [] {
    Fixture f;
    const auto outer = f.klass();
    call(f.host, Function::PushLocalFrame, 1, {f.env, 4});
    const auto inner = f.klass();
    const auto preserved = call(f.host, Function::PopLocalFrame, 1, {f.env, inner});
    check(preserved && preserved != inner, "result not promoted to outer frame");
    check(call(f.host, Function::GetObjectRefType, 1, {f.env, inner}) == 0, "popped local survives");
    check(call(f.host, Function::IsSameObject, 1, {f.env, outer, preserved}) == 1, "promoted result identity incorrect");
    throws([&] { call(f.host, Function::PopLocalFrame, 1, {f.env, 0}); });
  });
  test("detach invalidates locals while globals survive", [] {
    Fixture f;
    const auto local = f.klass(), global = call(f.host, Function::NewGlobalRef, 1, {f.env, local});
    f.host.detachThread(1);
    f.env = f.host.attachThread(1);
    check(call(f.host, Function::GetObjectRefType, 1, {f.env, local}) == 0, "detached local survives");
    check(call(f.host, Function::NewLocalRef, 1, {f.env, global}) != 0, "global lost on detach");
  });
  test("local reference capacity reports OOM and recovers after deletion", [] {
    Fixture f(Limits{.max_local_refs = 2});
    const auto first = f.klass();
    f.klass();
    check(f.klass() == 0, "local limit ignored");
    check(f.host.pendingException(1)->class_name == "java/lang/OutOfMemoryError", "no OOM pending");
    call(f.host, Function::DeleteLocalRef, 1, {f.env, first});
    f.clear();
    check(f.klass() != 0, "capacity not recovered");
    check(call(f.host, Function::EnsureLocalCapacity, 1, {f.env, 1}) == result(JNI_ENOMEM), "capacity guarantee incorrectly succeeded");
  });
  test("global reference capacity is bounded and deletion recovers capacity", [] {
    Fixture f(Limits{.max_global_refs = 1});
    const auto local = f.klass();
    const auto global = call(f.host, Function::NewGlobalRef, 1, {f.env, local});
    check(global != 0, "first global allocation failed");
    check(call(f.host, Function::NewGlobalRef, 1, {f.env, local}) == 0, "global limit ignored");
    check(f.host.pendingException(1)->class_name == "java/lang/OutOfMemoryError", "global exhaustion missing OOM");
    call(f.host, Function::DeleteGlobalRef, 1, {f.env, global});
    f.clear();
    check(call(f.host, Function::NewGlobalRef, 1, {f.env, local}) != 0, "global capacity did not recover");
  });
  test("frame limit and negative capacity fail without corrupting frames", [] {
    Fixture f(Limits{.max_frames = 2});
    throws([&] { call(f.host, Function::PushLocalFrame, 1, {f.env, 0xffffffff}); });
    check(call(f.host, Function::PushLocalFrame, 1, {f.env, 0}) == 0, "first frame failed");
    check(call(f.host, Function::PushLocalFrame, 1, {f.env, 0}) == result(JNI_ENOMEM), "frame limit ignored");
    f.clear();
    call(f.host, Function::PopLocalFrame, 1, {f.env, 0});
  });
  test("explicit method lookup distinguishes static and instance methods", [] {
    Fixture f;
    const auto instance = f.host.registerMethod("test/Engine", "start", "(I)V", false, true);
    const auto statik = f.host.registerMethod("test/Engine", "version", "()I", true, false);
    const auto klass = f.klass();
    check(call(f.host, Function::GetMethodID, 1, {f.env, klass, f.memory.string("start"), f.memory.string("(I)V")}) == instance, "instance method ID wrong");
    check(call(f.host, Function::GetStaticMethodID, 1, {f.env, klass, f.memory.string("version"), f.memory.string("()I")}) == statik, "static method ID wrong");
    check(call(f.host, Function::GetMethodID, 1, {f.env, klass, f.memory.string("version"), f.memory.string("()I")}) == 0, "static method found as instance");
    check(f.host.pendingException(1)->class_name == "java/lang/NoSuchMethodError", "missing method did not set exception");
  });
  test("RegisterNatives parses 12-byte guest records and preserves Thumb tag", [] {
    Fixture f;
    const auto method_id = f.host.registerMethod("test/Engine", "start", "(I)V", false, true);
    const auto table = f.method("start", "(I)V", 0x812345);
    check(call(f.host, Function::RegisterNatives, 1, {f.env, f.klass(), table, 1}) == 0, "RegisterNatives failed");
    const auto binding = f.host.lookupNative("test/Engine", "start", "(I)V");
    check(binding && binding->method_id == method_id && binding->guest_entry == 0x812345 && !binding->is_static, "native binding/tag lost");
  });
  test("RegisterNatives rejects undeclared and non-native methods", [] {
    Fixture f;
    f.host.registerMethod("test/Engine", "ordinary", "()V", false, false);
    const auto klass = f.klass();
    for (const auto& name : {"absent", "ordinary"}) {
      check(call(f.host, Function::RegisterNatives, 1, {f.env, klass, f.method(name, "()V", 0x8000), 1}) == result(JNI_ERR), "invalid native declaration accepted");
      check(f.host.pendingException(1)->class_name == "java/lang/NoSuchMethodError", "wrong registration exception");
      f.clear();
    }
  });
  test("native registration is transactional across a failed batch", [] {
    Fixture f;
    f.host.registerMethod("test/Engine", "first", "()V", true, true);
    const auto first = f.method("first", "()V", 0x8001);
    const auto second = f.method("absent", "()V", 0x9001);
    const auto array = f.memory.allocate(24, 4);
    std::array<std::uint8_t, 12> record{};
    f.memory.read(first, record); f.memory.write(array, record);
    f.memory.read(second, record); f.memory.write(array + 12, record);
    check(call(f.host, Function::RegisterNatives, 1, {f.env, f.klass(), array, 2}) == result(JNI_ERR), "bad batch succeeded");
    check(!f.host.lookupNative("test/Engine", "first", "()V"), "first mapping partially committed");
  });
  test("re-register and unregister update only native mappings", [] {
    Fixture f;
    const auto method_id = f.host.registerMethod("test/Engine", "start", "()V", true, true);
    const auto klass = f.klass();
    call(f.host, Function::RegisterNatives, 1, {f.env, klass, f.method("start", "()V", 0x8000), 1});
    call(f.host, Function::RegisterNatives, 1, {f.env, klass, f.method("start", "()V", 0x9001), 1});
    check(f.host.lookupNative("test/Engine", "start", "()V")->guest_entry == 0x9001, "native replacement failed");
    call(f.host, Function::UnregisterNatives, 1, {f.env, klass});
    check(!f.host.lookupNative("test/Engine", "start", "()V"), "unregister left mapping");
    check(call(f.host, Function::GetStaticMethodID, 1, {f.env, klass, f.memory.string("start"), f.memory.string("()V")}) == method_id, "unregister removed declaration");
  });
  test("native null/misaligned entries and invalid counts are rejected", [] {
    Fixture f;
    f.host.registerMethod("test/Engine", "start", "()V", false, true);
    const auto klass = f.klass();
    for (const Address entry : {0u, 1u, 0x8002u}) {
      check(call(f.host, Function::RegisterNatives, 1, {f.env, klass, f.method("start", "()V", entry), 1}) == result(JNI_ERR), "invalid entry accepted");
      f.clear();
    }
    for (const auto count : {0u, 0xffffffffu, 4097u}) {
      check(call(f.host, Function::RegisterNatives, 1, {f.env, klass, 0, count}) == result(JNI_ERR), "invalid count accepted");
      f.clear();
    }
  });
  test("bad native table memory does not install mappings", [] {
    Fixture f;
    f.host.registerMethod("test/Engine", "start", "()V", false, true);
    const auto klass = f.klass();
    throws<MemoryError>([&] { call(f.host, Function::RegisterNatives, 1, {f.env, klass, 0xfffffff8, 1}); });
    throws<MemoryError>([&] { call(f.host, Function::RegisterNatives, 1, {f.env, klass, static_cast<Address>(f.memory.bytes.size() - 4), 1}); });
    check(!f.host.lookupNative("test/Engine", "start", "()V"), "bad memory installed mapping");
  });
  test("GetJavaVM writes the actual VM pointer shape", [] {
    Fixture f;
    const auto out = f.memory.allocate(4, 4);
    check(call(f.host, Function::GetJavaVM, 1, {f.env, out}) == 0, "GetJavaVM failed");
    check(f.memory.readU32(out) == f.host.vmPointer(), "GetJavaVM wrote table instead of VM pointer");
  });
  test("generic host objects retain registered class identity", [] {
    Fixture f;
    const auto object = f.host.makeObject(1, "test/Engine");
    const auto klass = call(f.host, Function::GetObjectClass, 1, {f.env, object});
    check(call(f.host, Function::IsSameObject, 1, {f.env, klass, f.klass()}) == 1, "object class identity wrong");
    throws([&] { f.host.makeObject(1, "missing/Class"); });
  });
  test("host class and method declarations are validated", [] {
    Fixture f;
    throws([&] { f.host.registerClass("test/Engine"); });
    throws([&] { f.host.registerClass("invalid.Class"); });
    throws([&] { f.host.registerMethod("missing/Class", "start", "()V"); });
    for (const auto signature : {"", "()", "(V)V", "(I", "()Q", "([V)V", "(L;)V"})
      throws([&] { f.host.registerMethod("test/Engine", "bad", signature); });
    throws([&] { f.host.registerMethod("test/Engine", "bad", std::string("(Lbad\0name;)V", 13)); });
    check(f.host.registerMethod("test/Engine", "good", "([ILjava/lang/String;J)Ljava/lang/Object;") != 0, "valid descriptor rejected");
  });
  test("bounded little-endian memory helpers reject null, wrap and unterminated strings", [] {
    Memory memory;
    memory.writeU32(5, 0x78563412);
    check(memory.bytes[5] == 0x12 && memory.bytes[8] == 0x78 && memory.readU32(5) == 0x78563412, "guest word endian wrong");
    throws<MemoryError>([&] { memory.readU32(0); });
    throws<MemoryError>([&] { memory.writeU32(0xfffffffe, 0); });
    throws<MemoryError>([&] { memory.readCString(0); });
    const auto text = memory.string("abcd");
    throws<MemoryError>([&] { memory.readCString(text, 4); });
    check(memory.readCString(text, 5) == "abcd", "bounded C string wrong");
  });
  test("callback arena and allocation overlap guards reject invalid layout", [] {
    Memory memory;
    throws([&] { Host host(memory, 3); });
    throws([&] { Host host(memory, 0xfffff000); });
    throws<MemoryError>([&] { Host host(memory, 0x1000); });
  });
  test("callback argument counts fail diagnostically", [] {
    Fixture f;
    throws([&] { call(f.host, Function::GetVersion, 1, {}); });
    throws([&] { call(f.host, Function::FindClass, 1, {f.env}); });
    throws([&] { call(f.host, Function::GetEnv, 1, {f.host.vmPointer()}); });
  });
  test("concurrent attached guest threads maintain isolated local references", [] {
    Fixture f;
    const auto global = call(f.host, Function::NewGlobalRef, 1, {f.env, f.klass()});
    std::atomic<unsigned> errors = 0;
    std::vector<std::thread> workers;
    for (unsigned index = 0; index < 8; ++index) workers.emplace_back([&, index] {
      try {
        const ThreadId id = 100 + index;
        const auto env = f.host.attachThread(id);
        const auto local = call(f.host, Function::NewLocalRef, id, {env, global});
        check(call(f.host, Function::IsSameObject, id, {env, local, global}) == 1, "concurrent ref identity wrong");
        f.host.detachThread(id);
      } catch (...) { ++errors; }
    });
    for (auto& worker : workers) worker.join();
    check(errors == 0, "concurrent JNI host operation failed");
  });
  test("host class entry creates real thread-local class references", [] {
    Memory memory;Host host(memory);const auto env=host.attachThread(1);
    host.registerClass("test/Host");const auto first=host.classHandle(1,"test/Host");
    const auto second=host.classHandle(1,"test/Host");
    check(first!=second,"local handles must be distinct");
    check(call(host,Function::IsSameObject,1,{env,first,second})==1,"host class identity lost");
    check(call(host,Function::GetObjectRefType,1,{env,first})==1,"host entry did not make local reference");
    throws([&]{host.classHandle(2,"test/Host");});
    throws([&]{host.classHandle(1,"test/Unknown");});
    const auto missing=memory.string("test/Missing");
    call(host,Function::FindClass,1,{env,missing});
    throws([&]{host.classHandle(1,"test/Host");});
  });
  test("native return releases inner frames but preserves outer and global refs", [] {
    Memory memory;Host host(memory);const auto env=host.attachThread(1);host.registerClass("test/Native");
    auto outer=host.classHandle(1,"test/Native");host.beginNativeCall(1);
    auto local=host.classHandle(1,"test/Native");
    auto global=call(host,Function::NewGlobalRef,1,{env,local});
    call(host,Function::PushLocalFrame,1,{env,1});
    auto inner=host.classHandle(1,"test/Native");
    host.endNativeCall(1);
    for(auto ref:{local,inner})check(call(host,Function::GetObjectRefType,1,{env,ref})==0,"native local leaked");
    check(call(host,Function::GetObjectRefType,1,{env,outer})==1,"outer local destroyed");
    check(call(host,Function::GetObjectRefType,1,{env,global})==2,"global destroyed");
    throws([&]{host.endNativeCall(1);});
  });
  test("nested native calls protect caller frames and prohibit active detach", [] {
    Memory memory;Host host(memory);const auto env=host.attachThread(1);host.registerClass("test/Native");
    host.beginNativeCall(1);auto outer=host.classHandle(1,"test/Native");
    throws([&]{call(host,Function::PopLocalFrame,1,{env,0});});
    throws([&]{host.detachThread(1);});
    host.beginNativeCall(1);auto inner=host.classHandle(1,"test/Native");host.endNativeCall(1);
    check(call(host,Function::GetObjectRefType,1,{env,inner})==0,"nested local leaked");
    check(call(host,Function::GetObjectRefType,1,{env,outer})==1,"caller local invalidated");
    host.endNativeCall(1);check(call(host,Function::GetObjectRefType,1,{env,outer})==0,"outer native local leaked");
  });
  test("native frame limits and pending exceptions fail before entry", [] {
    Memory memory;Limits limits;limits.max_frames=2;Host host(memory,0xffe00000,limits);
    auto env=host.attachThread(1);host.registerClass("test/Native");host.beginNativeCall(1);
    throws([&]{host.beginNativeCall(1);});host.endNativeCall(1);
    auto name=memory.string("test/Missing");call(host,Function::FindClass,1,{env,name});
    throws([&]{host.beginNativeCall(1);});throws([&]{host.endNativeCall(1);});
    call(host,Function::ExceptionClear,1,{env});host.beginNativeCall(1);host.endNativeCall(1);
  });
  test("JNI string table indices and intrinsic class identity", [] {
    Fixture f;
    check(static_cast<unsigned>(Function::NewString)==163&&static_cast<unsigned>(Function::GetStringUTFRegion)==221,"wrong string ABI indices");
    auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("abc")});
    auto klass=call(f.host,Function::GetObjectClass,1,{f.env,text});
    auto expected=call(f.host,Function::FindClass,1,{f.env,f.memory.string("java/lang/String")});
    check(call(f.host,Function::IsSameObject,1,{f.env,klass,expected})==1,"string intrinsic class mismatch");
  });
  test("modified UTF-8 string lengths include NUL and surrogate units correctly", [] {
    Fixture f;const auto bytes=std::string("A\xc0\x80\xed\xa0\xbd\xed\xb8\x80");
    auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string(bytes)});
    check(call(f.host,Function::GetStringLength,1,{f.env,text})==4,"wrong UTF16 length");
    check(call(f.host,Function::GetStringUTFLength,1,{f.env,text})==9,"wrong modified UTF8 length");
  });
  test("NewString reads guest UTF16 little endian and permits embedded zero", [] {
    Fixture f;auto at=f.memory.allocate(8,4);f.memory.writeU32(at,0x00000041);f.memory.writeU32(at+4,0xde00d83d);
    auto text=call(f.host,Function::NewString,1,{f.env,at,4});
    check(call(f.host,Function::GetStringLength,1,{f.env,text})==4&&call(f.host,Function::GetStringUTFLength,1,{f.env,text})==9,"UTF16 construction mismatch");
    auto out=f.memory.allocate(8,4);call(f.host,Function::GetStringRegion,1,{f.env,text,0,4,out});
    check(f.memory.readU32(out)==0x00000041&&f.memory.readU32(out+4)==0xde00d83d,"UTF16 region mismatch");
  });
  test("string UTF regions write exact bytes without a terminator", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("ab\xc0\x80")});
    auto out=f.memory.allocate(8,4);f.memory.writeU32(out,0xaaaaaaaa);f.memory.writeU32(out+4,0xbbbbbbbb);
    call(f.host,Function::GetStringUTFRegion,1,{f.env,text,1,2,out});
    check(f.memory.readU32(out)==0xaa80c062&&f.memory.readU32(out+4)==0xbbbbbbbb,"UTF region over-wrote terminator guard");
  });
  test("invalid string regions raise pending exception without writing", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("abc")});auto out=f.memory.allocate(4,4);f.memory.writeU32(out,0x12345678);
    for(auto start:{0xffffffffu,4u,0x7fffffffu}){call(f.host,Function::GetStringRegion,1,{f.env,text,start,1,out});check(f.host.pendingException(1)->class_name=="java/lang/StringIndexOutOfBoundsException","missing region exception");check(f.memory.readU32(out)==0x12345678,"failed region wrote output");f.clear();}
  });
  test("zero-length strings and regions make no guest access", [] {
    Fixture f;auto text=call(f.host,Function::NewString,1,{f.env,0,0});check(!call(f.host,Function::GetStringLength,1,{f.env,text}),"empty string not empty");call(f.host,Function::GetStringRegion,1,{f.env,text,0,0,0});call(f.host,Function::GetStringUTFRegion,1,{f.env,text,0,0,0});
  });
  test("string bounds malformed input and wrong object kinds fail diagnostically", [] {
    Fixture f;throws([&]{call(f.host,Function::NewString,1,{f.env,0,0xffffffff});});throws([&]{call(f.host,Function::NewString,1,{f.env,0,4097});});
    throws([&]{call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("\xf0\x9f\x98\x80")});});
    throws([&]{call(f.host,Function::GetStringLength,1,{f.env,f.klass()});});
    auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("a")});throws([&]{call(f.host,Function::GetStringRegion,1,{f.env,text,0,1,0});});
  });
  test("string references obey native-call cleanup and global lifetime", [] {
    Fixture f;f.host.beginNativeCall(1);auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("kept")});auto global=call(f.host,Function::NewGlobalRef,1,{f.env,text});f.host.endNativeCall(1);
    throws([&]{call(f.host,Function::GetStringLength,1,{f.env,text});});check(call(f.host,Function::GetStringLength,1,{f.env,global})==4,"global string lost");
  });
  test("live string byte budget follows shared refs and recovers on release", [] {
    Fixture f(Limits{.max_string_storage_bytes=8});auto input=f.memory.string("abcd");
    auto local=call(f.host,Function::NewStringUTF,1,{f.env,input});check(local,"initial string failed");
    auto global=call(f.host,Function::NewGlobalRef,1,{f.env,local});call(f.host,Function::DeleteLocalRef,1,{f.env,local});
    check(!call(f.host,Function::NewStringUTF,1,{f.env,input}),"global-held string was not charged");
    check(f.host.pendingException(1)->class_name=="java/lang/OutOfMemoryError","missing string OOM");f.clear();
    call(f.host,Function::DeleteGlobalRef,1,{f.env,global});check(call(f.host,Function::NewStringUTF,1,{f.env,input}),"released string budget not recovered");
  });
  test("UTF string leases return a real terminated copy and isCopy true", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("abc")});auto flag=f.memory.allocate(1,1);
    auto chars=call(f.host,Function::GetStringUTFChars,1,{f.env,text,flag});
    check(chars&&f.memory.readCString(chars)=="abc","wrong copied UTF string");std::array<std::uint8_t,1> yes{};f.memory.read(flag,yes);check(yes[0]==1,"isCopy not true");
    call(f.host,Function::ReleaseStringUTFChars,1,{f.env,text,chars});
  });
  test("UTF16 string leases preserve NUL and surrogate units", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("A\xc0\x80\xed\xa0\xbd\xed\xb8\x80")});
    auto chars=call(f.host,Function::GetStringChars,1,{f.env,text,0});check(f.memory.readU32(chars)==0x00000041&&f.memory.readU32(chars+4)==0xde00d83d,"wrong UTF16 lease");
    call(f.host,Function::ReleaseStringChars,1,{f.env,text,chars});
  });
  test("2000 string acquisitions reuse released guest storage", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("reuse")});auto first=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});auto next=f.memory.next;
    call(f.host,Function::ReleaseStringUTFChars,1,{f.env,text,first});
    for(unsigned n=0;n<2000;++n){auto chars=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});check(chars==first,"released storage not reused");call(f.host,Function::ReleaseStringUTFChars,1,{f.env,text,chars});}check(f.memory.next==next,"released buffer leaked guest arena bytes");
  });
  test("wrong string encoding and stale release are rejected", [] {
    Fixture f;auto a=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("a")}),b=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("b")});
    auto chars=call(f.host,Function::GetStringUTFChars,1,{f.env,a,0});
    throws([&]{call(f.host,Function::ReleaseStringUTFChars,1,{f.env,b,chars});});throws([&]{call(f.host,Function::ReleaseStringChars,1,{f.env,a,chars});});
    call(f.host,Function::ReleaseStringUTFChars,1,{f.env,a,chars});throws([&]{call(f.host,Function::ReleaseStringUTFChars,1,{f.env,a,chars});});
  });
  test("global string aliases permit release from another attached thread", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("cross")});auto global=call(f.host,Function::NewGlobalRef,1,{f.env,text});
    auto chars=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});auto second=f.host.attachThread(2);call(f.host,Function::DeleteLocalRef,1,{f.env,text});
    call(f.host,Function::ReleaseStringUTFChars,2,{second,global,chars});check(call(f.host,Function::GetStringLength,2,{second,global})==5,"global string corrupted");
  });
  test("string release remains legal with pending exception", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("release")});auto chars=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});
    call(f.host,Function::FindClass,1,{f.env,f.memory.string("missing/Class")});call(f.host,Function::ReleaseStringUTFChars,1,{f.env,text,chars});check(bool(f.host.pendingException(1)),"release cleared exception");f.clear();
    check(call(f.host,Function::GetStringUTFChars,1,{f.env,text,0})==chars,"pending-exception release did not free buffer");
  });
  test("bounded string pool reports OOM and recovers through reuse", [] {
    Fixture f(Limits{.max_string_buffer_bytes=8,.max_string_buffers=2});auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("abc")});
    auto a=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0}),b=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});check(a&&b&&a!=b,"concurrent leases alias");
    check(!call(f.host,Function::GetStringUTFChars,1,{f.env,text,0}),"pool quota ignored");check(f.host.pendingException(1)->class_name=="java/lang/OutOfMemoryError","missing pool OOM");f.clear();
    call(f.host,Function::ReleaseStringUTFChars,1,{f.env,text,a});check(call(f.host,Function::GetStringUTFChars,1,{f.env,text,0})==a,"quota not recovered");
  });
  test("bad isCopy output fails before reserving copy storage", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("abc")});auto before=f.memory.next;
    throws([&]{call(f.host,Function::GetStringUTFChars,1,{f.env,text,0xffff0000});});check(f.memory.next==before,"bad output leaked allocation");
  });
  test("unreleased copy pins string storage after last local reference", [] {
    Fixture f(Limits{.max_string_storage_bytes=8});auto input=f.memory.string("abcd");auto text=call(f.host,Function::NewStringUTF,1,{f.env,input});
    check(call(f.host,Function::GetStringUTFChars,1,{f.env,text,0}),"lease acquisition failed");call(f.host,Function::DeleteLocalRef,1,{f.env,text});
    check(!call(f.host,Function::NewStringUTF,1,{f.env,input}),"leased object was released prematurely");
  });
  test("copied buffer writes never mutate immutable Java string storage", [] {
    Fixture f;auto text=call(f.host,Function::NewStringUTF,1,{f.env,f.memory.string("abc")});auto chars=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});f.memory.writeU32(chars,0);
    call(f.host,Function::ReleaseStringUTFChars,1,{f.env,text,chars});auto again=call(f.host,Function::GetStringUTFChars,1,{f.env,text,0});check(f.memory.readCString(again)=="abc","copy was incorrectly written back");
  });
  test("attached thread quota fails without altering existing environment", [] {
    Fixture f(Limits{.max_threads=1});auto next=f.memory.next;throws([&]{f.host.attachThread(2);});check(f.memory.next==next&&call(f.host,Function::GetVersion,1,{f.env})==JNI_VERSION_1_6,"thread quota corrupted active state");
  });
  std::cout << "JNI32 tests: " << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
