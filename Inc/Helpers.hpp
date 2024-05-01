#include <windows.h>

#include <memory>
#include <string>
#include <system_error>

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

struct Flag {
 private:
  bool Value;

 public:
  explicit Flag(bool val);
  Flag(const Flag&) = delete;
  Flag& operator=(const Flag&) = delete;

  Flag(Flag&& rhs);
  Flag& operator=(Flag&& rhs);

  bool& Access();
};

}  // namespace Helpers
}  // namespace GigOn