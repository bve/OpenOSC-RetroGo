#pragma once
#include "retrogo/core.h"
#include "retrogo/media.hpp"

namespace retrogo {
enum class SaveStatus { Ok, Empty, Damaged, NoSpace, Io };
class SavePair {
public:
    SavePair(const char *root, const char *id, uint32_t identity, const char *kind);
    SaveStatus load(void *data, uint32_t capacity, uint32_t &bytes) const;
    SaveStatus write(const void *data, uint32_t bytes) const;

private:
    struct Record {
        uint32_t magic, generation, payload, crc, guard;
    };
    struct Entry { Record header{}; SaveStatus status = SaveStatus::Empty; };
    Entry inspect(unsigned slot, void *data, uint32_t capacity) const;
    static unsigned newest(const Entry (&records)[2]);
    void path(unsigned slot, char (&result)[PathSize]) const;
    static constexpr uint32_t Magic = 0x31534752; // RGS1
    const char *root_;
    const char *id_;
    uint32_t identity_;
    const char *kind_;
};
}
