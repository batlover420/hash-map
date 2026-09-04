#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <typename Key, typename Value>

class HashMap {
private:

    static constexpr std::size_t MAX_INDEXABLE =
        static_cast<std::size_t>(std::numeric_limits<std::size_t>::max());

    static constexpr std::uint8_t EMPTY = 0x80;
    static constexpr std::uint8_t TOMB = 0xFE;

    struct Entry {
        Key key;
        Value value;
    };

    static_assert(
        std::is_nothrow_move_constructible_v<Key>,
        "Key must be nothrow move constructible"
    );
    static_assert(
        std::is_nothrow_move_constructible_v<Value>,
        "Value must be nothrow move constructible"
    );
    static_assert(
        std::is_default_constructible_v<Value>,
        "Value must be default constructible"
    );
    static_assert(
        noexcept(std::hash<Key>{}(std::declval<const Key&>())),
        "Key hashing must be noexcept"
    );

    std::uint8_t* control = nullptr;
    Entry* entries = nullptr;
    std::size_t* ctoe = nullptr;
    std::size_t* etoc = nullptr;

    std::size_t capacity = 0;
    std::size_t entry_capacity = 0;
    std::size_t elements = 0;
    std::size_t occupancy = 0;  // live + dead entries

    static std::uint8_t fingerprint(std::size_t hash) {
        return static_cast<std::uint8_t>((hash >> 57) & 0x7F);
    }

    void constructDefaultEntry(const Key& key) {
        std::construct_at(
            entries + elements,
            Entry{key, Value{}}
        );
    }

    bool attemptRehash() {
        std::size_t tombstones = occupancy - elements;

        if ((elements + 1) * 20 >= capacity * 17) {  // If load factor > 85%
            rehash(capacity * 2);
    
            return true;
        } else if (tombstones * 20 >= capacity * 3) {  // OR if grave factor >= 15%
            rehash(capacity);

            return true;
        }

        return false;
    }

    void rehash(std::size_t new_cap) {
        std::uint8_t* new_control = nullptr;

        std::size_t* new_ctoe = nullptr;

        validateCapacity(new_cap);

        try {
            new_control = new std::uint8_t[new_cap];
            new_ctoe = new std::size_t[new_cap];
        } catch (...) {
            delete[] new_control;
            delete[] new_ctoe;
            throw;
        }

        std::memset(
            new_control,
            EMPTY,
            new_cap
        );

        for (std::size_t i = 0; i < elements; ++i) {
            std::size_t hash =
                std::hash<Key>{}(entries[i].key);

            std::size_t index = hash & (new_cap - 1);

            while (new_control[index] != EMPTY) {
                index = (index + 1) & (new_cap - 1);
            }

            new_control[index] = fingerprint(hash);
            new_ctoe[index] = i;
            etoc[i] = index;
        }

        delete[] control;
        delete[] ctoe;

        control = new_control;
        ctoe = new_ctoe;

        capacity = new_cap;
        occupancy = elements;
    }

    void swap(HashMap& other) noexcept {
        std::swap(control, other.control);
        std::swap(ctoe, other.ctoe);
        std::swap(etoc, other.etoc);

        std::swap(entries, other.entries);
        std::swap(entry_capacity, other.entry_capacity);

        std::swap(capacity, other.capacity);
        std::swap(elements, other.elements);
        std::swap(occupancy, other.occupancy);
    }

    static void validateCapacity(std::size_t cap) {
        if (cap > MAX_INDEXABLE) {
            throw std::length_error(
                "HashMap capacity exceeds 32-bit range"
            );
        }
    }

    void ensureEntryCapacity(std::size_t n) {
        if (n <= entry_capacity) {
            return;
        }

        std::size_t new_cap = std::max<std::size_t>(entry_capacity, 16);

        while (new_cap < n) {
            new_cap *= 2;
        }

        validateCapacity(new_cap);
        
        Entry* new_entries = nullptr;
        std::size_t* new_etoc = nullptr;

        try {
            new_entries = allocateDense(new_cap);
            new_etoc = new std::size_t[new_cap];
        } catch (...) {
            freeDense(new_entries);
            delete[] new_etoc;
            throw;
        }

        for (std::size_t i = 0; i < elements; i ++) {
            std::construct_at(
                new_entries + i,
                std::move(entries[i])
            );

            new_etoc[i] = etoc[i];

            std::destroy_at(
                entries + i
            );
        }
        freeDense(entries);
        delete[] etoc;

        entries = new_entries;
        etoc = new_etoc;

        entry_capacity = new_cap;
    }

    static Entry* allocateDense(std::size_t count) {
        return static_cast<Entry*>(
            ::operator new(
                sizeof(Entry) * count,
                std::align_val_t{alignof(Entry)}
            )
        );
    }

    static void freeDense(Entry* ptr) {
            ::operator delete(
                ptr,
                std::align_val_t{alignof(Entry)}
            );
    }

    void allocateStorage(std::size_t cap) {
        validateCapacity(cap);

        std::uint8_t* new_control = nullptr;
        std::size_t * new_ctoe = nullptr;
        std::size_t* new_etoc = nullptr;
        Entry* new_entries = nullptr;

        try {
            new_control = new std::uint8_t[cap];

            std::memset(new_control, EMPTY, cap);

            new_ctoe = new std::size_t[cap];
            new_etoc = new std::size_t[cap];

            new_entries = allocateDense(cap);
    
        } catch (...) {
            delete[] new_control;
            delete[] new_ctoe;
            delete[] new_etoc;
            freeDense(new_entries);

            throw;
        }

        freeDense(entries);
        delete[] control;
        delete[] ctoe;
        delete[] etoc;

        entries = new_entries;
        control = new_control;
        ctoe = new_ctoe;
        etoc = new_etoc;

        capacity = cap;
        entry_capacity = cap;
    }

public:

    HashMap(std::size_t cap = 16) {
        cap = std::max<std::size_t>(cap, 16);

        cap = std::bit_ceil(cap);

        allocateStorage(cap);
    }

    HashMap(const HashMap& other) : capacity(other.capacity),
                                    entry_capacity(other.entry_capacity),
                                    elements(other.elements),
                                    occupancy(other.occupancy)
    {
        std::uint8_t* new_control = nullptr;
        std::size_t* new_ctoe = nullptr;
        std::size_t* new_etoc = nullptr;
        Entry* new_entries = nullptr;

        std::size_t constructed = 0;

        try {
            new_control = new std::uint8_t[capacity];

            std::memcpy(
                new_control,
                other.control,
                capacity * sizeof(std::uint8_t)
            );

            new_ctoe = new std::size_t[capacity];

            for (std::size_t i = 0; i < capacity; ++i) {
                if ((other.control[i] & EMPTY) == 0) {
                    new_ctoe[i] = other.ctoe[i];
                }
            }

            new_etoc = new std::size_t[entry_capacity];

            std::memcpy(
                new_etoc,
                other.etoc,
                elements * sizeof(std::size_t)
            );

            new_entries = allocateDense(entry_capacity);

            for (; constructed < elements; ++constructed) {
                std::construct_at(
                    new_entries + constructed,
                    other.entries[constructed]
                );
            }
        } catch (...) {
            for (std::size_t i = 0; i < constructed; ++i) {
                std::destroy_at(
                    new_entries + i
                );
            }
            freeDense(new_entries);

            delete[] new_control;
            delete[] new_ctoe;
            delete[] new_etoc;

            throw;
        }

        control = new_control;
        ctoe = new_ctoe;
        etoc = new_etoc;
        entries = new_entries;
    }

    HashMap& operator=(const HashMap& other)  {
        if (this == &other) {
            return *this;
        }

        HashMap temp(other);
        swap(temp);

        return *this;
    }

    ~HashMap() {
        if constexpr (!std::is_trivially_destructible_v<Entry>) {
            for (std::size_t i = 0; i < elements; i++) {
                std::destroy_at(entries + i);
            }
        }
        freeDense(entries);

        delete[] control;
        delete[] ctoe;
        delete[] etoc;
    }

    struct EntryRef{
        const Key& first;
        Value& second;
    };

    class Iterator {
    private:
        HashMap* m;
        std::size_t i;

    public:
        Iterator(HashMap* map, std::size_t index) : m(map),
                                                    i(index) 
        {}

        EntryRef operator*() const noexcept {
            return EntryRef{
                m->entries[i].key,
                m->entries[i].value
            };
        }

        Iterator& operator++() noexcept {
            ++i;
            return *this;
        }

        bool operator==(const Iterator& other) const noexcept {
            return m == other.m && i == other.i;
        }

        bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }
    };

    Iterator begin() {
        return Iterator(this, 0);
    }

    Iterator end() {
        return Iterator(this, elements);
    }

    struct ConstEntryRef{
        const Key& first;
        const Value& second;
    };

    class ConstIterator {
    private:
        const HashMap* m;
        std::size_t i;

    public:
        ConstIterator(const HashMap* map, std::size_t index) : m(map),
                                                         i(index)
        {}

        ConstEntryRef operator*() const noexcept {
            return ConstEntryRef{
                m->entries[i].key,
                m->entries[i].value
            };
        }

        ConstIterator& operator++() {
            ++i;
            return *this;
        }

        bool operator==(const ConstIterator& other) const noexcept {
            return m == other.m && i == other.i;
        }

        bool operator!=(const ConstIterator& other) const noexcept {
            return !(*this == other);
        }
    };

    ConstIterator begin() const {
        return ConstIterator(this, 0);
    }

    ConstIterator end() const {
        return ConstIterator(this, elements);
    }

    Value& operator[](const Key& key) {
        std::size_t hash = std::hash<Key>{}(key);

        std::size_t i = hash & (capacity - 1);

        std::size_t tombstone = capacity;

        std::uint8_t fp = fingerprint(hash);

        while (true) {
            const std::uint8_t ctrl = control[i];

            if ((ctrl & EMPTY) == 0) {
                if (ctrl == fp) {
                    const std::size_t index = ctoe[i];

                    if (entries[index].key == key) {
                        return entries[index].value;
                    }
                }
            } else if (ctrl == TOMB) {
                    if (tombstone == capacity) {
                        tombstone = i;
                    }
            } else {
                if (attemptRehash()) {
                    i = hash & (capacity - 1);
                    tombstone = capacity;
                    continue;
                }

                const std::size_t target = elements;

                ensureEntryCapacity(elements + 1);

                constructDefaultEntry(key);

                if (tombstone != capacity) {
                    i = tombstone;
                } else {
                    occupancy++;
                }

                control[i] = fp;
                etoc[target] = i;
                ctoe[i] = target;

                elements++;

                return (entries[target].value);
            }

            i = (i + 1) & (capacity - 1);
        }
    }

    void clear() noexcept {
        if constexpr (!std::is_trivially_destructible_v<Entry>) {
            for (std::size_t i = 0; i < elements; i++) {
                std::destroy_at(
                    entries + i
                );
            }
        }

        std::memset(
            control,
            EMPTY,
            capacity
        );

        elements = 0;
        occupancy = 0;
    }

    bool allocate(std::size_t requested) {
        std::size_t minimum =
            elements > 0 ? (elements * 20 / 17) + 1 : 16;

        

        requested = std::max<std::size_t>(requested, minimum);

        std::size_t new_cap = std::bit_ceil(requested);

        validateCapacity(new_cap);

        bool changed = false;

        if (new_cap != capacity) {
            rehash(new_cap);
            changed = true;
        }

        if (new_cap > entry_capacity) {
            ensureEntryCapacity(new_cap);
        }

        return true;
    }

    const Value& at(const Key& key) const {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t i = hash & (capacity - 1);
        std::uint8_t fp = fingerprint(hash);

        while (true) {
            std::uint8_t ctrl = control[i];

            if (ctrl == fp) {
                const std::size_t index = ctoe[i];

                if (entries[index].key == key) {
                    return entries[index].value;
                }
            } else if (control[i] == EMPTY) {
                throw std::out_of_range("Key not found");
            }

            i = (i + 1) & (capacity -1);
        }
    }

    bool contains(const Key& key) const {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t i = hash & (capacity - 1);
        std::uint8_t fp = fingerprint(hash);

        while (true) {
            std::uint8_t ctrl = control[i];

            if (ctrl == fp) {
                const std::size_t index = ctoe[i];

                    if (entries[index].key == key) {
                        return true;
                    }
            } else if (ctrl == EMPTY) {
                return false;
            }

            i = (i + 1) & (capacity - 1);
        }
    }

    bool erase(const Key& key) {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t i = hash & (capacity - 1);
        std::uint8_t fp = fingerprint(hash);

        while (true) {
            std::uint8_t ctrl = control[i];

            if (ctrl == fp) {
                const std::size_t index = ctoe[i];

                if (entries[index].key == key) {
                    const std::size_t last = elements - 1;

                    std::destroy_at(
                        entries + index
                    );
                    if (index != last) {
                        std::size_t moved = etoc[last];

                        std::construct_at(
                            entries + index,
                            std::move(entries[last])
                        );

                        std::destroy_at(
                            entries + last
                        );

                        etoc[index] = moved;
                        ctoe[moved] = index;
                    }

                    control[i] = TOMB;

                    --elements;

                    return true;
                }
            } else if (ctrl == EMPTY) {
                return false;
            }

            i = (i + 1) & (capacity - 1);
        }
    }

    std::size_t cap() const {
        return capacity;
    }

    std::size_t size() const {
        return elements;
    }

    bool empty() const {
        return elements == 0;
    }
};
