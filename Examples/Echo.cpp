#include <cassert>
#include <limits>

#include "AsioContext.hpp"

#undef max  // I hate Windows

const unsigned short SCALE_SIZE = 50;
const DWORD UPDATE_PERIOD_MS = 100;

using namespace GigOn;
using namespace GigOn::Helpers;

void DisplayValue(float value) {
  int norm = value * SCALE_SIZE;

  std::cout << "\r|";

  if (norm <= 0) {
    int i = -SCALE_SIZE + 1;

    for (; i < norm; ++i) std::cout << " ";
    for (; i < 0; ++i) std::cout << "#";
    std::cout << "'";

    for (i = 0; i < SCALE_SIZE; ++i) std::cout << " ";
  } else {
    int i = 0;
    for (i = 0; i < SCALE_SIZE; ++i) std::cout << " ";
    std::cout << "'";

    for (i = 0; i < norm; ++i) std::cout << "#";
    for (; i <= 0; ++i) std::cout << " ";
  }
  std::cout << "|" << std::flush;
}

void PrintUsageAndExit(const char* reason) {
  std::cout << "Incorrect " << reason << std::endl;
  std::cout << "Usage:   ./Echo <DRIVER_NAME> <BUFFER_SIZE> <IN_CHANNEL> "
               "<OUT_CHANNEL>"
            << std::endl;
  std::cout << "Example: ./Echo \"Focusrite USB ASIO\" 64 1 1";
  exit(1);
}

int main(int argc, char* argv[]) try {
  if (argc != 5) PrintUsageAndExit("argument count");

  std::string driverName = argv[1];
  size_t bufferSize = std::stoul(argv[2]);
  size_t inChannel = std::stoul(argv[3]);
  size_t outChannel = std::stoul(argv[4]);

  float average = 0;

  // We guarantee that input callback will
  // be called before output callbacks
  void* inputBuffer = nullptr;

  auto inputCb = [bufferSize, &average, &inputBuffer](
                     long channel, void* buffer, ASIOSampleType stype) {
    assert(stype == ASIOSTInt32LSB);
    assert(buffer);

    auto samplePtr = reinterpret_cast<int32_t*>(buffer);

    average = 0;

    for (int i = 0; i < bufferSize; ++i, ++samplePtr) {
      int32_t max = std::numeric_limits<int32_t>::max();
      int32_t sample = *samplePtr;  // le32toh(*samplePtr);
      average += float(sample) / max;
    }

    average /= bufferSize;

    inputBuffer = buffer;
  };

  auto outputCb = [bufferSize, &inputBuffer](long channel, void* buffer,
                                             ASIOSampleType stype) {
    assert(stype == ASIOSTInt32LSB);
    assert(inputBuffer);
    assert(buffer);

    memcpy(buffer, inputBuffer, 4 * bufferSize);
  };
  auto eventCb = [](GigOn::AsioContext::DriverEvent event) -> void {};
  auto confCb = [](size_t, size_t, size_t) {};

  auto& asio = GigOn::AsioContext::Get();

  std::cout << "Loading driver \"" << driverName << "\"..." << std::endl;
  asio.LoadDriver(driverName);
  asio.InitDriver();

  std::cout << "Creating buffer..." << std::endl;

  auto processor = AsioProcessorMock::Create(inputCb, outputCb, confCb);
  auto handler = AsioHandlerMock::Create(eventCb);

  asio.SetHandlers(std::move(processor), std::move(handler));
  asio.CreateBuffers({inChannel}, {outChannel}, bufferSize);

  std::cout << "Starting..." << std::endl;
  asio.Start();

  std::cout << "Monitoring the channel:" << std::endl;

  while (true) {
    DisplayValue(average);
    Sleep(UPDATE_PERIOD_MS);
  }

  asio.Stop();
  asio.DisposeBuffers();
  asio.DeInitDriver();
  asio.UnloadDriver();

} catch (std::exception& e) {
  std::cout << "Got exception: " << e.what() << std::endl;
}