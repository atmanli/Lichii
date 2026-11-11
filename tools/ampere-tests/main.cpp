// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_config.hpp>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    Catch::Session session;
    const int arguments = session.applyCommandLine(argc, argv);
    if (arguments != 0) {
        return arguments;
    }
    session.configData().allowZeroTests = false;
    return session.run();
}
