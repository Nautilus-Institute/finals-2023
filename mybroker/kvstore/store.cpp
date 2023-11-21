#include <map>
#include <string>
#include <iostream>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>

namespace bip = boost::interprocess;

template <typename T> using Alloc = bip::allocator<T, bip::managed_shared_memory::segment_manager>;
using ShString = bip::basic_string<char, std::char_traits<char>, Alloc<char>>;
using ShmemAllocator = Alloc<std::pair<ShString const, ShString>>;
using StringMap = bip::map<ShString, ShString, std::less<ShString>, ShmemAllocator>;

class SharedKeyValueStore
{
public:
    SharedKeyValueStore(const char* segmentName)
        : segment(bip::open_or_create, segmentName, 65536) // Shared memory size set to 64KB
        , map(segment.find_or_construct<StringMap>("StringMap")(std::less<ShString>(), segment.get_segment_manager())) {
    }

    void store(const std::string& key, const std::string& value) {
        auto sa = segment.get_segment_manager();
        ShString sh_key = ShString(key.c_str(), sa);
        ShString sh_value = ShString(value.c_str(), sa);
        map->erase(sh_key);
        map->insert(std::make_pair(sh_key, sh_value));
    }

    std::string retrieve(const std::string& key) const {
        auto sa = segment.get_segment_manager();
        ShString sh_key = ShString(key.c_str(), sa);
        auto it = map->find(sh_key);
        if (it == map->end()) {
            throw std::runtime_error("Key not found");
        }
        return std::string(it->second.c_str());
    }

private:
    bip::managed_shared_memory segment;
    StringMap* map;
};


const int type_int = 0x30;
const int type_string = 0x31;
const int type_py = 0x39;


typedef struct item {
    int type;
    std::string value;
} item_t;


std::string item_to_string(item_t & item)
{
    std::ostringstream ss;
    ss << (char)item.type << item.value;
    return ss.str();
}

item_t string_to_item(std::string &str)
{
    item_t item;
    item.type = str[0];
    item.value = std::string(str, 1, str.length() - 1);
    return item;
}

void store_value(std::string key, std::string value)
{
    SharedKeyValueStore store("shared_mem");
    item_t item;
    item.type = type_string;
    item.value = value;
    std::string item_str = item_to_string(item);
    store.store(key, item_str);
}

std::string load_string_value(std::string key)
{
    SharedKeyValueStore store("shared_mem");
    std::string value;
    try {
        value = store.retrieve(key);
    }
    catch (std::runtime_error &) {
        return "";
    }
    item_t item = string_to_item(value);
    if (item.type == type_string) {
        return item.value;
    }
    return "";
}

void initialize()
{
    bip::shared_memory_object::remove("shared_mem");
}

// void remove_value(std::string key)
// {
//     store.erase(key);
// }

