#include "core/Zip.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <utility>

namespace {

// The CRC-32 every zip entry carries, over its uncompressed data.
uint32_t Crc32(const std::string& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values = {};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            values[n] = c;
        }
        return values;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void Put16(std::string& out, uint32_t value) {
    out += static_cast<char>(value & 0xFF);
    out += static_cast<char>((value >> 8) & 0xFF);
}

void Put32(std::string& out, uint32_t value) {
    Put16(out, value & 0xFFFF);
    Put16(out, value >> 16);
}

uint32_t Get16(const std::string& in, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(in[at])) |
           static_cast<uint32_t>(static_cast<unsigned char>(in[at + 1])) << 8;
}

uint32_t Get32(const std::string& in, size_t at) {
    return Get16(in, at) | Get16(in, at + 2) << 16;
}

// The moment of writing in the DOS format of the headers: time, then date.
std::pair<uint32_t, uint32_t> DosNow() {
    std::time_t now = std::time(nullptr);
    std::tm local = *std::localtime(&now);
    uint32_t time = static_cast<uint32_t>(local.tm_hour << 11 | local.tm_min << 5 | local.tm_sec / 2);
    uint32_t date = static_cast<uint32_t>((local.tm_year - 80) << 9 | (local.tm_mon + 1) << 5 |
                                          local.tm_mday);
    return {time, date};
}

// A Huffman code in the canonical form of deflate: how many codes of each
// length, and the symbols in code order.
struct Huffman {
    std::array<int, 16> count = {};
    std::array<int, 320> symbol = {};
};

// Undoes the deflate of RFC 1951, the only compression zip workbooks use.
class Inflater {
public:
    Inflater(const std::string& in, size_t from, size_t size, size_t expected)
        : in_(in), pos_(from), end_(from + size) {
        out_.reserve(expected);
    }

    // The inflated bytes, or empty when the stream is damaged.
    std::string Run() {
        int last = 0;
        do {
            last = Bits(1);
            int type = Bits(2);
            if (type == 0) {
                Stored();
            } else if (type == 1) {
                Fixed();
            } else if (type == 2) {
                Dynamic();
            } else {
                bad_ = true;
            }
        } while (last == 0 && !bad_);
        return bad_ ? std::string() : out_;
    }

private:
    int Bits(int need) {
        uint64_t value = bitBuffer_;
        while (bitCount_ < need) {
            if (pos_ >= end_) {
                bad_ = true;
                return 0;
            }
            value |= static_cast<uint64_t>(static_cast<unsigned char>(in_[pos_++])) << bitCount_;
            bitCount_ += 8;
        }
        bitBuffer_ = value >> need;
        bitCount_ -= need;
        return static_cast<int>(value & ((1ull << need) - 1));
    }

    void Stored() {
        bitBuffer_ = 0;
        bitCount_ = 0;
        if (pos_ + 4 > end_) {
            bad_ = true;
            return;
        }
        uint32_t length = Get16(in_, pos_);
        uint32_t check = Get16(in_, pos_ + 2);
        pos_ += 4;
        if ((length ^ 0xFFFF) != check || pos_ + length > end_) {
            bad_ = true;
            return;
        }
        out_.append(in_, pos_, length);
        pos_ += length;
    }

    // Builds a code from the length of each symbol; false when the lengths
    // ask for more codes than there are.
    static bool Build(Huffman& code, const int* lengths, int count) {
        code.count.fill(0);
        for (int symbol = 0; symbol < count; ++symbol) {
            ++code.count[static_cast<size_t>(lengths[symbol])];
        }
        int left = 1;
        for (int length = 1; length < 16; ++length) {
            left <<= 1;
            left -= code.count[static_cast<size_t>(length)];
            if (left < 0) {
                return false;
            }
        }
        std::array<int, 16> offsets = {};
        for (int length = 1; length < 15; ++length) {
            offsets[static_cast<size_t>(length + 1)] =
                offsets[static_cast<size_t>(length)] + code.count[static_cast<size_t>(length)];
        }
        for (int symbol = 0; symbol < count; ++symbol) {
            if (lengths[symbol] != 0) {
                code.symbol[static_cast<size_t>(offsets[static_cast<size_t>(lengths[symbol])]++)] =
                    symbol;
            }
        }
        return true;
    }

    int Decode(const Huffman& code) {
        int value = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length < 16; ++length) {
            value |= Bits(1);
            int count = code.count[static_cast<size_t>(length)];
            if (value - count < first) {
                return code.symbol[static_cast<size_t>(index + (value - first))];
            }
            index += count;
            first += count;
            first <<= 1;
            value <<= 1;
        }
        bad_ = true;
        return -1;
    }

    void Codes(const Huffman& lengthCode, const Huffman& distanceCode) {
        static const int kBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const int kExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const int kDistance[30] = {1,   2,   3,   4,    5,    7,    9,    13,    17,    25,
                                          33,  49,  65,  97,   129,  193,  257,  385,   513,   769,
                                          1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const int kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        while (!bad_) {
            int symbol = Decode(lengthCode);
            if (bad_ || symbol == 256) {
                return;
            }
            if (symbol < 256) {
                out_ += static_cast<char>(symbol);
                continue;
            }
            symbol -= 257;
            if (symbol >= 29) {
                bad_ = true;
                return;
            }
            int length = kBase[symbol] + Bits(kExtra[symbol]);
            int which = Decode(distanceCode);
            if (bad_ || which < 0 || which >= 30) {
                bad_ = true;
                return;
            }
            size_t distance = static_cast<size_t>(kDistance[which] + Bits(kDistanceExtra[which]));
            if (distance > out_.size()) {
                bad_ = true;
                return;
            }
            for (int copied = 0; copied < length; ++copied) {
                out_ += out_[out_.size() - distance];
            }
        }
    }

    void Fixed() {
        // Built once, on first use; the initialisation of a local static is
        // safe across the threads an import may run on.
        static const std::pair<Huffman, Huffman> kCodes = [] {
            std::pair<Huffman, Huffman> codes;
            int lengths[288] = {};
            for (int symbol = 0; symbol < 288; ++symbol) {
                lengths[symbol] = symbol < 144 ? 8 : symbol < 256 ? 9 : symbol < 280 ? 7 : 8;
            }
            Build(codes.first, lengths, 288);
            int distances[30] = {};
            for (int& length : distances) {
                length = 5;
            }
            Build(codes.second, distances, 30);
            return codes;
        }();
        Codes(kCodes.first, kCodes.second);
    }

    void Dynamic() {
        static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int literals = Bits(5) + 257;
        int distances = Bits(5) + 1;
        int codes = Bits(4) + 4;
        if (bad_ || literals > 286 || distances > 30) {
            bad_ = true;
            return;
        }
        int lengths[320] = {};
        for (int index = 0; index < codes; ++index) {
            lengths[kOrder[index]] = Bits(3);
        }
        Huffman lengthCode;
        if (!Build(lengthCode, lengths, 19)) {
            bad_ = true;
            return;
        }
        int index = 0;
        while (index < literals + distances && !bad_) {
            int symbol = Decode(lengthCode);
            if (symbol < 16) {
                lengths[index++] = symbol;
                continue;
            }
            int repeat = 0;
            int value = 0;
            if (symbol == 16) {
                if (index == 0) {
                    bad_ = true;
                    return;
                }
                value = lengths[index - 1];
                repeat = 3 + Bits(2);
            } else if (symbol == 17) {
                repeat = 3 + Bits(3);
            } else {
                repeat = 11 + Bits(7);
            }
            if (index + repeat > literals + distances) {
                bad_ = true;
                return;
            }
            while (repeat-- > 0) {
                lengths[index++] = value;
            }
        }
        if (bad_ || lengths[256] == 0) {
            bad_ = true;
            return;
        }
        Huffman literalCode;
        Huffman distanceCode;
        if (!Build(literalCode, lengths, literals) ||
            !Build(distanceCode, lengths + literals, distances)) {
            bad_ = true;
            return;
        }
        Codes(literalCode, distanceCode);
    }

    const std::string& in_;
    size_t pos_;
    size_t end_;
    uint64_t bitBuffer_ = 0;
    int bitCount_ = 0;
    bool bad_ = false;
    std::string out_;
};

constexpr uint32_t kLocalHeader = 0x04034b50;
constexpr uint32_t kCentralHeader = 0x02014b50;
constexpr uint32_t kEndOfDirectory = 0x06054b50;
constexpr uint32_t kUtf8Names = 0x0800;

}  // namespace

namespace zip {

bool IsZip(const std::string& bytes) {
    return bytes.size() >= 4 && Get32(bytes, 0) == kLocalHeader;
}

std::string Write(const std::vector<Entry>& entries) {
    auto [time, date] = DosNow();
    std::string out;
    std::string directory;
    for (const Entry& entry : entries) {
        uint32_t offset = static_cast<uint32_t>(out.size());
        uint32_t crc = Crc32(entry.data);
        uint32_t size = static_cast<uint32_t>(entry.data.size());
        uint32_t nameSize = static_cast<uint32_t>(entry.name.size());

        Put32(out, kLocalHeader);
        Put16(out, 20);
        Put16(out, kUtf8Names);
        Put16(out, 0);
        Put16(out, time);
        Put16(out, date);
        Put32(out, crc);
        Put32(out, size);
        Put32(out, size);
        Put16(out, nameSize);
        Put16(out, 0);
        out += entry.name;
        out += entry.data;

        Put32(directory, kCentralHeader);
        Put16(directory, 20);
        Put16(directory, 20);
        Put16(directory, kUtf8Names);
        Put16(directory, 0);
        Put16(directory, time);
        Put16(directory, date);
        Put32(directory, crc);
        Put32(directory, size);
        Put32(directory, size);
        Put16(directory, nameSize);
        Put16(directory, 0);
        Put16(directory, 0);
        Put16(directory, 0);
        Put16(directory, 0);
        Put32(directory, 0);
        Put32(directory, offset);
        directory += entry.name;
    }
    uint32_t start = static_cast<uint32_t>(out.size());
    out += directory;
    Put32(out, kEndOfDirectory);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, static_cast<uint32_t>(entries.size()));
    Put16(out, static_cast<uint32_t>(entries.size()));
    Put32(out, static_cast<uint32_t>(directory.size()));
    Put32(out, start);
    Put16(out, 0);
    return out;
}

std::vector<Entry> Read(const std::string& archive) {
    std::vector<Entry> entries;
    if (archive.size() < 22) {
        return entries;
    }
    // The end record sits last, after a comment of at most 64 KB.
    size_t end = std::string::npos;
    size_t lowest = archive.size() > 22 + 0xFFFF ? archive.size() - 22 - 0xFFFF : 0;
    for (size_t at = archive.size() - 22; at + 1 > lowest; --at) {
        if (Get32(archive, at) == kEndOfDirectory) {
            end = at;
            break;
        }
        if (at == 0) {
            break;
        }
    }
    if (end == std::string::npos) {
        return entries;
    }
    uint32_t count = Get16(archive, end + 10);
    size_t at = Get32(archive, end + 16);
    for (uint32_t index = 0; index < count; ++index) {
        if (at + 46 > archive.size() || Get32(archive, at) != kCentralHeader) {
            return {};
        }
        uint32_t method = Get16(archive, at + 10);
        size_t packed = Get32(archive, at + 20);
        size_t size = Get32(archive, at + 24);
        size_t nameSize = Get16(archive, at + 28);
        size_t extraSize = Get16(archive, at + 30);
        size_t commentSize = Get16(archive, at + 32);
        size_t local = Get32(archive, at + 42);
        if (at + 46 + nameSize > archive.size() || local + 30 > archive.size() ||
            Get32(archive, local) != kLocalHeader) {
            return {};
        }
        Entry entry;
        entry.name = archive.substr(at + 46, nameSize);
        size_t data = local + 30 + Get16(archive, local + 26) + Get16(archive, local + 28);
        if (data + packed > archive.size()) {
            return {};
        }
        if (method == 0) {
            entry.data = archive.substr(data, packed);
        } else if (method == 8) {
            entry.data = Inflater(archive, data, packed, size).Run();
            if (entry.data.size() != size) {
                return {};
            }
        } else {
            return {};
        }
        entries.push_back(std::move(entry));
        at += 46 + nameSize + extraSize + commentSize;
    }
    return entries;
}

std::string Find(const std::vector<Entry>& entries, const std::string& name) {
    for (const Entry& entry : entries) {
        if (entry.name == name) {
            return entry.data;
        }
    }
    return std::string();
}

}  // namespace zip
