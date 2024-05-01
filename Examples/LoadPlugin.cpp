#include <clocale>

#include "Vst2Effect.hpp"
#define TAB "  "

void PrintUsageAndExit(const char* msg) {
    std::cout << "Wrong usage: " << msg << std::endl;
    std::cout << "Usage:       ./LoadPlugin <PATH>" << std::endl;
    std::cout << "Example:     ./LoadPlugin Parallax.dll" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) try {
  setlocale(LC_ALL, "");

  if (argc != 2)
    PrintUsageAndExit("Incorrect argument count");

  const char* path = argv[1];

  auto loader = GigOn::Helpers::DllLoader(path);
  auto effect = GigOn::Vst2Effect(loader);

  auto info = effect.GetInfo();

  std::cout << "*** VST2 Plugin Info ***" << std::endl;
  std::cout << TAB "Name:    " << info.Effect << std::endl;
  std::cout << TAB "Vendor:  " << info.Vendor << std::endl;
  std::cout << TAB "Product: " << info.Product << std::endl;
  std::cout << TAB "Inputs:  " << info.NumInputs << std::endl;
  std::cout << TAB "Outputs: " << info.NumOutputs << std::endl;

  effect.Configure(48000.f, 64);
  effect.Start();

  GigOn::VstProcessBuffer input(64, info.NumInputs);
  GigOn::VstProcessBuffer output(64, info.NumOutputs);

  effect.Process(input, output);

  effect.Stop();

} catch (std::exception& e) {
  std::cout << e.what() << std::endl;
}