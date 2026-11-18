// Converts a Nasdaq TotalView-ITCH 5.0 file into events.bin (plain array of ob::Event)

#include "../event.hpp"
#include "../itch_parser.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: itch-parse IN.gz OUT.bin [SYMBOL ...]\n");
        return 2;
    }

    FILE* out = std::fopen(argv[2], "wb");
    if (!out) {
        std::fprintf(stderr, "cannot create %s\n", argv[2]);
        return 1;
    }

    std::vector<std::string> symbols(argv + 3, argv + argc);
    uint64_t count = 0;

    try {
        ob::itch::ItchParser parser(symbols, [&](const ob::Event& e) {
            std::fwrite(&e, sizeof e, 1, out);
            ++count;
        });

        ob::itch::parse_file(argv[1], parser);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "itch_parse: %s\n", e.what());
        return 1;
    }

    std::fclose(out);
    std::fprintf(stderr, "wrote %llu events to %s\n", (unsigned long long)count, argv[2]);
    return 0;
}