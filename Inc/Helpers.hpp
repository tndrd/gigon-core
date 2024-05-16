#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

namespace GigOn {
namespace Helpers {

std::string WinErrToStr(DWORD err);

struct LabelException : public std::runtime_error {
  LabelException(const std::string& label, const std::string& msg);
  virtual ~LabelException() = default;
};

struct WinException : public LabelException {
  static constexpr auto Label = "Windows error";
  WinException(const std::string& msg, DWORD err);
  virtual ~WinException() = default;
};

// RAII DLL Handle
class DllLoader final {
  static constexpr auto Label = "Dll loader";

  using ModuleVal = HINSTANCE__;  // aka decltype(*HMODULE)

  struct ModuleDeleter final {
    void operator()(ModuleVal* ptr) const;
  };

  std::unique_ptr<ModuleVal, ModuleDeleter> Module;
  std::string Path;

 public:
  DllLoader(const std::string& path);

  DllLoader(const DllLoader&) = delete;
  DllLoader& operator=(const DllLoader&) = delete;
  DllLoader& operator=(DllLoader&&) = default;
  DllLoader(DllLoader&&) = default;

  ~DllLoader() = default;

 public:
  FARPROC GetProcAddress(const std::string& procName) const;
  const std::string& GetPath() const;
};

template <typename T>
struct Moveable {
 private:
  T Value;

 public:
  // TODO: Universal reference
  explicit Moveable(T val) : Value{val} {}

  Moveable(const Moveable&) = default;
  Moveable& operator=(const Moveable&) = default;

  Moveable(Moveable&& rhs) : Value{std::move(rhs.Value)} { rhs.Value = {}; }

  Moveable& operator=(Moveable&& rhs) {
    std::swap(Value, rhs.Value);
    return *this;
  }

  T& Access() { return Value; }
  const T& Access() const { return Value; }
};

}  // namespace Helpers
}  // namespace GigOn