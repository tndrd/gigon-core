#include "Helpers.hpp"

namespace GigOn {
namespace Helpers {

std::string WinErrToStr(DWORD err) {
  return std::system_category().message(err);
}

LabelException::LabelException(const std::string& label, const std::string& msg)
    : std::runtime_error{label + ": " + msg} {}

WinException::WinException(const std::string& msg, DWORD err)
    : LabelException{Label, msg + ": " + Helpers::WinErrToStr(err)} {}

// RAII DLL Handle

void DllLoader::ModuleDeleter::operator()(ModuleVal* ptr) const {
  if (ptr) FreeLibrary(ptr);
}

DllLoader::DllLoader(const std::string& path) : Path{path} {
  auto module = LoadLibraryA(path.c_str());

  if (!module)
    throw Helpers::WinException("Failed to load module at \"" + path + "\"",
                                GetLastError());

  Module = decltype(Module){module};
}

FARPROC DllLoader::GetProcAddress(const std::string& procName) const {
  if (!Module) throw Helpers::LabelException(Label, "Dll not loaded");

  FARPROC proc = ::GetProcAddress(Module.get(), procName.c_str());
  if (proc) return proc;

  throw Helpers::WinException("Failed to get procedure \"" + procName + "\"",
                              GetLastError());
}

const std::string& DllLoader::GetPath() const { return Path; }

Flag::Flag(bool val) : Value{val} {}

Flag::Flag(Flag&& rhs) : Value{rhs.Value} {}

Flag& Flag::operator=(Flag&& rhs) {
  std::swap(Value, rhs.Value);
  return *this;
}

bool& Flag::Access() { return Value; }

}  // namespace Helpers
}  // namespace GigOn