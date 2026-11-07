// SPDX-License-Identifier: Apache-2.0
#include "link/linker.hpp"

#include <algorithm>
#include <functional>
#include <iostream>

using rearm::elf::Error;
using rearm::elf::Image;
using rearm::link::ModuleInput;
namespace {
void require(bool value, const std::string& reason) { if (!value) throw std::runtime_error(reason); }
void fails(const std::function<void()>& run, const std::string& expected) {
  try { run(); } catch (const Error& error) {
    if (std::string(error.what()).find(expected) == std::string::npos) throw std::runtime_error(std::string("unexpected diagnostic: ") + error.what());
    return;
  }
  throw std::runtime_error("expected diagnostic containing " + expected);
}
std::uint32_t symbol(const Image& image, const std::string& name) {
  const auto result = image.find_symbol(name);
  if (!result) throw std::runtime_error("test symbol missing: " + name);
  return result->value;
}
}
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: link_tests FIXTURE_DIRECTORY\n"; return 2; }
  const std::filesystem::path directory = argv[1];
  auto module = [&](const std::string& name, std::uint32_t bias) {
    return ModuleInput{"lib" + name + ".so", directory / ("lib" + name + ".so"), bias};
  };
  const auto consumer = module("consumer", 0x10000);
  const auto provider = module("provider", 0x20000);
  unsigned tests = 0;
  auto test = [&](const char* name, const std::function<void()>& run) {
    try { run(); ++tests; } catch (const std::exception& error) {
      std::cerr << "FAIL " << name << ": " << error.what() << '\n'; throw;
    }
  };
  try {
    test("dynamic dependency metadata", [&] {
      const auto image = Image::load(consumer.path, consumer.load_bias);
      require(image.dynamic().soname == "libconsumer.so", "SONAME metadata");
      require(image.dynamic().needed == std::vector<std::string>{"libprovider.so"}, "NEEDED metadata");
      require(image.dynamic().init_array_size == 4 && image.dynamic().fini_array_size == 4, "initializer metadata");
    });
    test("cross module function and data", [&] {
      const auto result = rearm::link::link({consumer, provider});
      const auto source = Image::load(provider.path, provider.load_bias);
      require(result.image.read32(symbol(result.image, "imported_data")) == symbol(source, "shared_data"), "ABS32 imported data pointer");
      require(result.image.read32(symbol(result.image, "imported_fn")) == symbol(source, "shared_fn"), "ABS32 Thumb function pointer");
      require((symbol(source, "shared_fn") & 1) == 1, "provider fixture must be Thumb");
      require(result.image.relocations().empty(), "combined image must not retain pending relocations");
      fails([&] { auto copy = result.image; copy.relocate(); }, "already applied");
    });
    test("dependency ordered constructors and destructors", [&] {
      const auto result = rearm::link::link({consumer, provider});
      const auto provider_image = Image::load(provider.path, provider.load_bias);
      const auto consumer_image = Image::load(consumer.path, consumer.load_bias);
      require(result.constructors == std::vector<std::uint32_t>{symbol(provider_image, "provider_init"), symbol(consumer_image, "consumer_init")}, "dependency must initialize first");
      require(result.destructors == std::vector<std::uint32_t>{symbol(consumer_image, "consumer_fini"), symbol(provider_image, "provider_fini")}, "owner must finalize first");
      require(result.image.read32(symbol(provider_image, "shared_data")) == 11, "linking must not execute constructors");
    });
    test("defined globals are interposed", [&] {
      const auto main = module("interposer", 0x10000);
      const auto result = rearm::link::link({main, provider});
      const auto original = Image::load(provider.path, provider.load_bias);
      const auto owner = Image::load(main.path, main.load_bias);
      bool tested = false;
      for (const auto& relocation : original.relocations()) {
        if (relocation.symbol < original.symbols().size() && original.symbols()[relocation.symbol].name == "shared_data") {
          require(result.image.read32(relocation.address) == symbol(owner, "shared_data"), "main global must preempt provider global");
          tested = true;
        }
      }
      require(tested, "real fixture must contain defined-symbol relocation");
    });
    test("protected definition binds locally", [&] {
      const auto main = module("interposer", 0x10000);
      const auto result = rearm::link::link({main, provider});
      const auto original = Image::load(provider.path, provider.load_bias);
      require(result.image.read32(symbol(original, "protected_pointer")) == symbol(original, "protected_value"), "protected symbol was incorrectly preempted");
    });
    test("hidden definition remains local", [&] {
      const auto result = rearm::link::link({consumer, provider});
      const auto original = Image::load(provider.path, provider.load_bias);
      require(result.image.read32(symbol(original, "hidden_pointer")) == symbol(original, "hidden_value"), "hidden definition address changed");
      fails([&] { rearm::link::link({module("hidden_consumer", 0x10000), provider}); }, "Unresolved ELF symbol 'hidden_value'");
    });
    test("strong definition beats weak fallback", [&] {
      const auto weak = module("weak", 0x10000);
      const auto strong = module("strong", 0x20000);
      const auto result = rearm::link::link({weak, strong});
      const auto original = Image::load(strong.path, strong.load_bias);
      require(result.image.read32(symbol(result.image, "weak_choice_pointer")) == symbol(original, "choice"), "later strong did not replace weak fallback");
    });
    test("weak fallback works without strong", [&] {
      const auto result = rearm::link::link({module("weak", 0x10000)});
      require(result.image.read32(symbol(result.image, "weak_choice_pointer")) == symbol(result.image, "choice"), "weak definition address");
    });
    test("first strong scope definition wins", [&] {
      const auto main = module("interposer", 0x10000);
      const auto weak = module("weak", 0x30000);
      const auto strong = module("strong", 0x40000);
      const auto result = rearm::link::link({main, provider, weak, strong});
      const auto original = Image::load(main.path, main.load_bias);
      require(result.image.read32(symbol(result.image, "strong_choice_pointer")) == symbol(original, "choice"), "main-first strong lookup order");
    });
    test("missing dependency rejected", [&] { fails([&] { rearm::link::link({consumer}); }, "missing DT_NEEDED 'libprovider.so'"); });
    test("declared external dependency resolves through callback", [&] {
      const auto result = rearm::link::link({consumer}, [](const auto& requested) -> std::optional<std::uint32_t> {
        if (requested.name == "shared_fn") return 0x90000001;
        if (requested.name == "shared_data") return 0x90001000;
        return {};
      }, {"libprovider.so"});
      require(result.image.read32(symbol(result.image, "imported_fn")) == 0x90000001, "external guest function address");
      require(result.image.read32(symbol(result.image, "imported_data")) == 0x90001000, "external guest data address");
    });
    test("external declaration alone does not resolve import", [&] { fails([&] { rearm::link::link({consumer}, {}, {"libprovider.so"}); }, "Unresolved ELF symbol"); });
    test("mapping collisions rejected", [&] { fails([&] { rearm::link::link({consumer, module("provider", 0x10000)}); }, "Overlapping ELF module mappings"); });
    test("duplicate SONAME rejected", [&] { auto duplicate = provider; duplicate.name = "alias"; duplicate.load_bias = 0x30000; fails([&] { rearm::link::link({provider, duplicate}); }, "Duplicate ELF module name/SONAME"); });
    test("SONAME resolves input aliases", [&] { auto alias = provider; alias.name = "provider-alias"; const auto result = rearm::link::link({consumer, alias}); require(result.modules[1].name == "provider-alias", "input alias was not retained"); });
    test("empty namespace rejected", [&] { fails([&] { rearm::link::link({}); }, "1..128"); });
    test("unrelocated combine rejected", [&] { fails([&] { Image::combine({Image::load(provider.path, provider.load_bias)}); }, "already-relocated"); });
    test("single image defined binding unchanged", [&] {
      auto image = Image::load(provider.path, provider.load_bias);
      const auto local = symbol(image, "shared_data");
      image.relocate([](const auto&) { return std::optional<std::uint32_t>{0x90000000}; });
      for (const auto& relocation : image.relocations()) if (relocation.symbol < image.symbols().size() && image.symbols()[relocation.symbol].name == "shared_data")
        require(image.read32(relocation.address) == local, "default single-image self-binding changed");
    });
  } catch (...) { return 1; }
  std::cout << "PASS " << tests << " real ELF multi-module linker tests\n";
}
