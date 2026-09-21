#include "SerialIO.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

int main()
{
#ifndef B2000000
    std::cerr << "B2000000 is unavailable on this Linux toolchain\n";
    return 1;
#else
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        std::cerr << "could not create test PTY: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        std::cerr << "could not initialize test PTY: " << std::strerror(errno) << '\n';
        close(master);
        return 1;
    }

    const char* slave_name = ptsname(master);
    if (!slave_name) {
        std::cerr << "could not get test PTY name: " << std::strerror(errno) << '\n';
        close(master);
        return 1;
    }

    // Open an inspection descriptor before SerialIO applies TIOCEXCL.
    const int inspector = open(slave_name, O_RDWR | O_NOCTTY);
    if (inspector < 0) {
        std::cerr << "could not open test PTY: " << std::strerror(errno) << '\n';
        close(master);
        return 1;
    }

    const std::string path(slave_name);
    SerialIO serial;
    if (serial.openPort(std::wstring(path.begin(), path.end())) !=
        SerialIO::Response::rOK) {
        std::cerr << "SerialIO could not open test PTY\n";
        close(inspector);
        close(master);
        return 1;
    }

    SerialIO::Configuration configuration;
    configuration.baudRate = 2000000;
    if (serial.configurePort(configuration) != SerialIO::Response::rOK) {
        std::cerr << "SerialIO rejected the 2 Mbaud configuration\n";
        close(inspector);
        close(master);
        return 1;
    }

    termios settings{};
    if (tcgetattr(inspector, &settings) != 0 ||
        cfgetospeed(&settings) != B2000000 ||
        cfgetispeed(&settings) != B2000000) {
        std::cerr << "test PTY did not retain the B2000000 speed\n";
        close(inspector);
        close(master);
        return 1;
    }

    close(inspector);
    close(master);
    std::cout << "FloppyBridge Linux 2 Mbaud configuration: PASS\n";
    return 0;
#endif
}
