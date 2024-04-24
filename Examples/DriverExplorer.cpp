#include "AsioContext.hpp"

const size_t DRIVERS_TO_LIST = 10;

#define TAB "  "
#define LINE "----------------------------"

int main() try {
  auto& asio = GigOn::AsioContext::Get();

  auto drivers = asio.GetDriverNames(DRIVERS_TO_LIST);

  std::cout << "Found drivers: " << std::endl;

  for (int i = 0; i < drivers.size(); ++i) {
    std::cout << "[" << i << "] " << drivers[i] << std::endl;
  }

  std::cout << "Enter driver id to explore driver's properties: ";

  size_t driverId = 0;
  std::cin >> driverId;

  std::cout << LINE << std::endl;

  if (driverId >= drivers.size()) {
    std::cout << "Error: incorrect number" << std::endl;
    return 1;
  }

  std::string driverName = drivers[driverId];
  std::cout << "Loading driver \"" << driverName << "\"..." << std::endl;
  std::cout << LINE << std::endl;

  asio.LoadDriver(driverName);
  asio.InitDriver();

  GigOn::Helpers::DumpAsioInfo(std::cout, asio.GetAsioInfo());
  GigOn::Helpers::DumpDeviceInfo(std::cout, asio.GetDeviceInfo());

} catch (std::exception& e) {
  std::cout << "Got an exception: " << e.what() << std::endl;
}