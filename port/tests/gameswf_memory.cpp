#include <base/tu_file.h>

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        std::array<unsigned char, 8> bytes{1, 2, 3, 4, 5, 6, 7, 8};
        tu_file borrowed(tu_file::memory_buffer, static_cast<int>(bytes.size()), bytes.data());
        require(borrowed.read_le32() == 0x04030201u, "Borrowed memory scalar read failed.");
        std::array<unsigned char, 10> tail{};
        require(borrowed.read_bytes(tail.data(), static_cast<int>(tail.size())) == 4,
                "Borrowed memory read did not stop at EOF.");
        require(tail[0] == 5 && tail[3] == 8 && borrowed.read_bytes(tail.data(), 1) == 0,
                "Borrowed memory tail or EOF read changed.");
        borrowed.set_position(2);
        require(borrowed.read_le16() == 0x0403u, "Borrowed memory seek/read failed.");
        require(borrowed.size() == 8 && borrowed.get_position() == 4,
                "Borrowed memory size query changed the cursor.");
        require(bytes == std::array<unsigned char, 8>{1, 2, 3, 4, 5, 6, 7, 8},
                "Reading modified borrowed storage.");
        bytes[0] = 0xabu;
        borrowed.set_position(0);
        require(borrowed.read_byte() == 0xabu, "Memory reader stopped borrowing its supplied storage.");

        tu_file owned(tu_file::memory_buffer);
        require(owned.write_bytes(bytes.data(), 6) == 6, "Owned memory write failed.");
        owned.set_position(2);
        owned.write_byte(0xffu);
        require(owned.size() == 6 && owned.get_position() == 3,
                "Owned memory overwrite changed its size or cursor.");
        owned.set_position(0);
        std::array<unsigned char, 6> result{};
        require(owned.read_bytes(result.data(), static_cast<int>(result.size())) == 6 &&
                    result == std::array<unsigned char, 6>{0xabu, 2, 0xffu, 4, 5, 6},
                "Owned memory read/write behavior changed.");
        std::cout << "GameSWF borrowed read-only and owned writable memory checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
