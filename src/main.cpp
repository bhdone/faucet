#include <iostream>

#include <cxxopts.hpp>

int main(int argc, char const* argv[]) {
    cxxopts::Options opts("btchd-faucet", "Provide a service that can send amount to BHD address with countable management.");
    opts.add_options()
        ("help", "Show help document") // --help
        ;
    auto result = opts.parse(argc, argv);
    if (result.count("help")) {
        std::cout << opts.help() << std::endl;
        return 0;
    }
    return 0;
}
