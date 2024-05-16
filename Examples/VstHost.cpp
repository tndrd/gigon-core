#include <cassert>
#include <limits>

#include "AsioVstBuffers.hpp"
#include "AsioContext.hpp"
#include "Vst2Effect.hpp"


#undef max  // I hate Windows

using namespace GigOn;
using namespace GigOn::Helpers;

void PrintUsageAndExit(const char* reason) {
  std::cout << "Incorrect " << reason << std::endl;
  std::cout
      << "Usage:   ./VstHost <DRIVER_NAME> <VST_NAME> <BUFFER_SIZE>"
      << std::endl;
  std::cout << "Example: ./VstHost \"Focusrite USB ASIO\" Parallax.dll 64";
  exit(1);
}

int main(int argc, char* argv[]) try {
  if (argc != 4) PrintUsageAndExit("argument count");

  setlocale(LC_ALL, "");

  std::string driverName = argv[1];
  std::string pluginName = argv[2];
  size_t bufferSize = std::stoul(argv[3]);

  auto loader = GigOn::Helpers::DllLoader(pluginName);
  auto effect = GigOn::Vst2Effect(loader);
  auto buffers = GigOn::AsioVstBuffers();

  auto& asio = GigOn::AsioContext::Get();

  auto inputCb = [&buffers, &effect](long channel, void* buffer, ASIOSampleType stype) {
    buffers.Asio2VstInput(channel, buffer, stype);
    effect.Process(buffers.GetVstInputs(), buffers.GetVstOutputs());
  };
  
  auto outputCb = [&buffers, &effect](long channel, void* buffer, ASIOSampleType stype) {
    buffers.Vst2AsioOutput(channel, buffer, stype);
  };

  auto eventCb = [](GigOn::AsioContext::DriverEvent event) -> void {};
  
  auto confCb = [&buffers](size_t bufSize, size_t nInputs, size_t nOutputs) {
    buffers.Configure(bufSize, nInputs, nOutputs);
  };

  std::cout << "Loading driver \"" << driverName << "\"..." << std::endl;
  asio.LoadDriver(driverName);
  asio.InitDriver();
  auto devInfo = asio.GetDeviceInfo();

  std::cout << "Configuring effect \"" << effect.GetInfo().Effect << "\"" << std::endl;
  effect.Configure(devInfo.SampleRate, bufferSize);
  effect.Start();

  std::cout << "Creating buffer..." << std::endl;

  auto processor = AsioProcessorMock::Create(inputCb, outputCb, confCb);
  auto handler = AsioHandlerMock::Create(eventCb);

  asio.SetHandlers(std::move(processor), std::move(handler));
  asio.CreateBuffers({0, 1}, {0, 1}, bufferSize);

  std::cout << "Starting..." << std::endl;
  asio.Start();

  std::cout << "Monitoring the channel:" << std::endl;

  getchar();

  

} catch (std::exception& e) {
  std::cout << "Got exception: " << e.what() << std::endl;
}