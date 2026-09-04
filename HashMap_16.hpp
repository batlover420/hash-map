#pragma once

#include <emmintrin.h>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <stdexcept>
#include <new>
#include <type_traits>
#include <immintrin.h>
#include <bit>

template <typename Key, typename Value>

class HashMap_16 {
    static constexpr std::size_t GROUP_SIZE = 16;
    static constexpr std::uint8_t EMPTY = 0x80;
    static constexpr std::uint8_t TOMB = 0xFE;

    inline static const __m128i empty_vector =
        _mm_set1_epi8(static_cast<char>(EMPTY));

    inline static const __m128i tombstone_vector = 
        _mm_set1_epi8(static_cast<char>(TOMB));

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

private:
    std::uint8_t* control;
    Key* keys;
    Value* values;
    std::size_t capacity;
    std::size_t elements;
    std::size_t occupancy;  // live + dead entries

    static std::uint8_t fingerprint(std::size_t hash) {
        return static_cast<std::uint8_t>((hash >> 57) & 0x7F);
    }

    void setControl(std::size_t index, std::uint8_t fp) {
        control[index] = fp;

        if (index < GROUP_SIZE - 1) {
            control[capacity + index] = fp;
        }
    }


    void constructEntry(std::size_t index, const Key& key) {
        std::construct_at(keys + index, key);

        try {
            std::construct_at(values + index);
        } catch (...) {
            std::destroy_at(keys + index);
            throw;
        }
    }

    bool attemptRehash() {
        std::size_t tombstones = occupancy - elements;

        if (elements * 20 >= capacity * 17) {  // If load factor > 85%
            rehash(capacity * 2);
    
            return true;
        } else if (tombstones * 10 >= capacity * 3) {  // OR if grave factor >= 30%
            rehash(capacity);

            return true;
        }

        return false;
    }

    void rehash(std::size_t new_cap) {
        std::uint8_t* new_control = nullptr;
        Key* new_keys = nullptr;
        Value* new_values = nullptr;

        try {
            new_control = new std::uint8_t[new_cap + (GROUP_SIZE - 1)];

            for (std::size_t i = 0; i < new_cap + (GROUP_SIZE - 1); i++) {
                new_control[i] = EMPTY;
            }

            new_keys = static_cast<Key*>(
                ::operator new(
                    sizeof(Key) * new_cap,
                    std::align_val_t{alignof(Key)}
                )
            );

            new_values = static_cast<Value*>(
                ::operator new(
                    sizeof(Value) * new_cap,
                    std::align_val_t{alignof(Value)}
                )
            );
        }
        catch (...) {
            delete[] new_control;

            if (new_keys != nullptr) {
                ::operator delete(
                    new_keys,
                    std::align_val_t{alignof(Key)}
                );
            }

            if (new_values != nullptr) {
                ::operator delete(
                    new_values,
                    std::align_val_t{alignof(Value)}
                );
            }

            throw;
        }

        for (std::size_t index = 0; index < capacity; index += GROUP_SIZE) {
            __m128i group = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(control + index)
            );

            std::uint16_t unused =
                static_cast<std::uint16_t>(_mm_movemask_epi8(group));

            std::uint16_t living = static_cast<std::uint16_t>(~unused);

            while (living != 0) {
                unsigned offset = std::countr_zero(living);

                std::size_t old_index = index + offset;

                std::size_t hash = std::hash<Key>{}(keys[old_index]);

                std::uint8_t fp = fingerprint(hash);

                std::size_t target = hash & (new_cap - 1);

                while (new_control[target] != EMPTY) {
                    target = (target + 1) & (new_cap - 1);
                }

                std::construct_at(new_keys + target, std::move(keys[old_index]));
                std::construct_at(new_values + target, std::move(values[old_index]));

                new_control[target] = fp;

                if (target < GROUP_SIZE - 1) {
                    new_control[new_cap + target] = fp;
                }

                std::destroy_at(keys + old_index);
                std::destroy_at(values + old_index);

                living = static_cast<std::uint16_t>(
                    living & (living - 1)
                );
            }
        }

        delete[] control;
        ::operator delete(keys, std::align_val_t{alignof(Key)});
        ::operator delete(values, std::align_val_t{alignof(Value)});

        control = new_control;
        keys = new_keys;
        values = new_values;

        capacity = new_cap;
        occupancy = elements;  // Tombstones removed when rehashing
    }

    void swap(HashMap_16& other) noexcept {
        std::swap(control, other.control);
        std::swap(keys, other.keys);
        std::swap(values, other.values);
        std::swap(capacity, other.capacity);
        std::swap(elements, other.elements);
        std::swap(occupancy, other.occupancy);
    }



public:
    HashMap_16(std::size_t initial_capacity = 16) : capacity(0),
                                                    elements(0),
                                                    occupancy(0)
    {
        if (initial_capacity < 16) {
            initial_capacity = 16;
        }

        capacity = std::bit_ceil(initial_capacity);

        std::uint8_t* new_control = nullptr;
        Key* new_keys = nullptr;
        Value* new_values = nullptr;

        try {
            new_control = new std::uint8_t[capacity + (GROUP_SIZE - 1)];

            for (std::size_t i = 0; i < capacity + (GROUP_SIZE - 1); i++) {
                new_control[i] = EMPTY;
            }

            new_keys = static_cast<Key*>(
                ::operator new(
                    sizeof(Key) * capacity,
                    std::align_val_t{alignof(Key)}
                )
            );

            new_values = static_cast<Value*>(
                ::operator new(
                    sizeof(Value) * capacity,
                    std::align_val_t{alignof(Value)}
                )
            );
        } catch (...) {
            delete[] new_control;

            if (new_keys != nullptr) {
                ::operator delete(
                    new_keys,
                    std::align_val_t{alignof(Key)}
                );
            }

            if (new_values != nullptr) {
                ::operator delete(
                    new_values,
                    std::align_val_t{alignof(Value)}
                );
            }

            throw;
        }

        control = new_control;
        keys = new_keys;
        values = new_values;
    }

    HashMap_16(const HashMap_16& other) : capacity(other.capacity),
                                            elements(other.elements),
                                            occupancy(other.occupancy)
    {
        std::uint8_t* new_control = nullptr;
        Key* new_keys = nullptr;
        Value* new_values = nullptr;

        try {
            new_control = new std::uint8_t[capacity + (GROUP_SIZE -1)];

            new_keys = static_cast<Key*>(
                ::operator new(
                    sizeof(Key) * capacity,
                    std::align_val_t{alignof(Key)}
                )
            );

            new_values = static_cast<Value*>(
                ::operator new(
                    sizeof(Value) * capacity,
                    std::align_val_t{alignof(Value)}
                )
            );
        } catch (...) {
            delete[] new_control;

            if (new_keys != nullptr) {
                ::operator delete(
                    new_keys,
                    std::align_val_t{alignof(Key)}
                );
            }

            if (new_values != nullptr) {
                ::operator delete(
                    new_values,
                    std::align_val_t{alignof(Value)}
                );
            }

            throw;
        }
        std::size_t i = 0;
        try {
            for (; i < capacity; i++) {
                if ((other.control[i] & EMPTY) == 0) {
                    std::construct_at(
                        new_keys + i,
                        other.keys[i]
                    );

                    try {
                        std::construct_at(
                            new_values + i,
                            other.values[i]
                        );
                    } catch (...) {
                        std::destroy_at(new_keys + i);
                        throw;
                    }
                }

                new_control[i] = other.control[i];
            }
        } catch  (...) {
            for (std::size_t j = 0; j < i; j++) {
                if ((other.control[j] & EMPTY) == 0) {
                    std::destroy_at(new_keys + j);
                    std::destroy_at(new_values + j);
                }
            }

            delete[] new_control;

            ::operator delete(
                new_keys,
                std::align_val_t{alignof(Key)}
            );

            ::operator delete(
                new_values,
                std::align_val_t{alignof(Value)}
            );

            throw;
        }

    for (std::size_t i = 0; i < (GROUP_SIZE -1); i++) {
        new_control[capacity + i] = new_control[i];
    }

    control = new_control;
    keys = new_keys;
    values = new_values;
    }

    HashMap_16& operator=(const HashMap_16& other)  {
        if (this == &other) {
            return *this;
        }

        HashMap_16 temp(other);
        swap(temp);

        return *this;
    }

    ~HashMap_16() {
        for (std::size_t i = 0; i < capacity; i++) {
            if ((control[i] & EMPTY) == 0) {
                std::destroy_at(keys + i);
                std::destroy_at(values + i);
            }
        }

        delete[] control;
        ::operator delete(keys, std::align_val_t{alignof(Key)});
        ::operator delete(values, std::align_val_t{alignof(Value)});
    }

    struct Entry{
        const Key& first;
        Value& second;
    };

    class Iterator {
    private:

        HashMap_16* map;
        std::size_t index;
        std::size_t group_index;
        std::uint16_t occupied;

        void skipUnused() {
            while (group_index < map->capacity) {
                if (occupied == 0) {
                    __m128i group = _mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(map->control + group_index)
                    );

                    std::uint16_t unused =
                        static_cast<std::uint16_t>(
                            _mm_movemask_epi8(group)
                        );

                    occupied = static_cast<std::uint16_t>(~unused);
                }

                if (occupied == 0) {
                    group_index += GROUP_SIZE;
                    continue;
                }

                    unsigned offset = std::countr_zero(occupied);

                    index = group_index + offset;

                    occupied = static_cast<std::uint16_t>(occupied & (occupied - 1));

                    return;   
            }

            index = map->capacity;
        }

    public:

        Iterator(HashMap_16* map, std::size_t index) : map(map),
                                                       index(index),
                                                       group_index(index),
                                                       occupied(0) 
        {
                skipUnused();
        }

        Entry operator*() const {
            return Entry{
                map->keys[index],
                map->values[index]
            };
        }

        Iterator& operator++() {
            if (occupied == 0) {
                group_index += GROUP_SIZE;
            }
            skipUnused();
            return *this;
        }

        bool operator==(const Iterator& other) const {
            return map == other.map && index == other.index;
        }

        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }

    };

    Iterator begin() {
        return Iterator(this, 0);
    }

    Iterator end() {
        return Iterator(this, capacity);
    }

    struct ConstEntry{
        const Key& first;
        const Value& second;
    };

    class ConstIterator {
    private:

        const HashMap_16* map;
        std::size_t index;
        std::size_t group_index;
        std::uint16_t occupied;

        void skipUnused() {
            while (group_index < map->capacity) {
                if (occupied == 0) {
                    __m128i group = _mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(map->control + group_index)
                    );

                    std::uint16_t unused =
                        static_cast<std::uint16_t>(
                            _mm_movemask_epi8(group)
                        );

                    occupied = static_cast<std::uint16_t>(~unused);
                }

                if (occupied == 0) {
                    group_index += GROUP_SIZE;
                    continue;
                }

                    unsigned offset = std::countr_zero(occupied);

                    index = group_index + offset;

                    occupied = static_cast<std::uint16_t>(occupied & (occupied - 1));

                    return;   
            }

            index = map->capacity;
        }

    public:

        ConstIterator(const HashMap_16* map, std::size_t index) : map(map),
                                                                  index(index),
                                                                  group_index(index),
                                                                  occupied(0)
        {
                skipUnused();
        }

        ConstEntry operator*() const {
            return ConstEntry{
                map->keys[index],
                map->values[index]
            };
        }

        ConstIterator& operator++() {
            if (occupied == 0) {
                group_index += GROUP_SIZE;
            }
            skipUnused();
            return *this;
        }

        bool operator==(const ConstIterator& other) const {
            return map == other.map && index == other.index;
        }

        bool operator!=(const ConstIterator& other) const {
            return !(*this == other);
        }

    };

    ConstIterator begin() const {
        return ConstIterator(this, 0);
    }

    ConstIterator end() const {
        return ConstIterator(this, capacity);
    }

    Value& operator[](const Key& key) {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t index = hash & (capacity - 1);
        std::size_t d_index = capacity;

        std::uint8_t fp = fingerprint(hash);

        if (control[index] == fp && keys[index] == key) {
            return values[index];
        }

        if (control[index] == EMPTY) {
            if (attemptRehash()) {
                index = hash & (capacity - 1);
                d_index = capacity;
            } else {
                constructEntry(index, key);

                setControl(index, fp);
                elements++;
                occupancy++;

                return values[index];
            }
        }

        __m128i fp_vector =
            _mm_set1_epi8(static_cast<char>(fp));

        while (true) {
                __m128i group = _mm_loadu_si128(
                    reinterpret_cast<const __m128i*>(control + index)
                );

                std::uint16_t matches =
                    static_cast<std::uint16_t>(
                        _mm_movemask_epi8(
                            _mm_cmpeq_epi8(group, fp_vector)
                        )
                    );

                while (matches != 0) {
                    unsigned offset = std::countr_zero(matches);

                    std::size_t target = (index + offset) & (capacity - 1);

                    if (keys[target] == key) {
                        return values[target];
                    }

                    matches = static_cast<std::uint16_t>(
                        matches & (matches - 1)
                    );
                }

                if (d_index == capacity) {
                    std::uint16_t unused = static_cast<std::uint16_t>(
                        _mm_movemask_epi8(group)
                    );

                    if (unused == 0) {
                        index = (index + GROUP_SIZE) & (capacity - 1);
                        continue;
                    }

                    std::uint16_t empties =
                        static_cast<std::uint16_t>(
                            _mm_movemask_epi8(
                                _mm_cmpeq_epi8(group, empty_vector)
                            )
                        );

                    unsigned first_empty = 0;

                    if (empties != 0) {
                        first_empty = std::countr_zero(empties);
                    }
                    
                    std::uint16_t tombstones =
                        static_cast<std::uint16_t>(
                            unused ^ empties
                        );

                    if (empties != 0) {
                        std::uint16_t before_empty =
                            static_cast<std::uint16_t>(
                                (1u << first_empty) - 1u
                            );

                        tombstones &= before_empty;
                    }

                    if (tombstones != 0) {
                        unsigned tombstone_offset =
                            std::countr_zero(tombstones);

                        d_index = (index + tombstone_offset) & (capacity - 1);
                    }

                    if (empties != 0) {
                        if (attemptRehash()) {
                            index = hash & (capacity - 1);
                            d_index = capacity;
                            continue;
                        }

                        std::size_t empty_index = (index + first_empty) & (capacity - 1);

                        std::size_t target;

                        if (d_index != capacity) {
                            target = d_index;
                        } else {
                            target = empty_index;
                        }

                        constructEntry(target, key);

                        setControl(target, fp);
                        elements++;

                        if (target != d_index) {
                            occupancy++;
                        }

                        return values[target];
                    }
                } else {
                    std::uint16_t empties =
                        static_cast<std::uint16_t>(
                            _mm_movemask_epi8(
                                _mm_cmpeq_epi8(group, empty_vector)
                            )
                        );


                    if (empties != 0) {
                        if (attemptRehash()) {
                            index = hash & (capacity - 1);
                            d_index = capacity;
                            continue;
                        }

                        constructEntry(d_index, key);

                        setControl(d_index, fp);
                        elements++;

                        return values[d_index];
                    }
                }

            index = (index + GROUP_SIZE) & (capacity - 1);
        }
    }

    void clear() {
        for (std::size_t index = 0; index < capacity; index += (GROUP_SIZE)) {
            __m128i group = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(control + index)
            );

            __m128i empty_comparison =
                _mm_cmpeq_epi8(group, empty_vector);

            std::uint16_t unused =
                static_cast<std::uint16_t>(
                    _mm_movemask_epi8(group)
                );

            std::uint16_t occupied = static_cast<std::uint16_t>(~unused);

            while (occupied != 0) {
                unsigned offset = std::countr_zero(occupied);

                std::size_t target = (index + offset) & (capacity - 1);

                std::destroy_at(keys + target);
                std::destroy_at(values + target);

                occupied = static_cast<std::uint16_t>(occupied & (occupied -1));
            }

            _mm_storeu_si128(
                reinterpret_cast<__m128i*>(
                    control + index
                ),
                empty_vector
            );
        }

        for (std::size_t index = capacity; index < capacity + (GROUP_SIZE -1); index++) {
            control[index] = EMPTY;
        }

        elements = 0;
        occupancy = 0;
    }

    bool allocate(std::size_t cap) {
        std::size_t min_cap = (elements * 20 / 17) + 1;

        if (min_cap < 16) {
            min_cap = 16;
        }

        if (cap < min_cap) {
            cap = min_cap;
        }

        std::size_t new_cap = std::bit_ceil(cap);

        if (new_cap != capacity) {
            rehash(new_cap);
            return true;
        } else {
            return false;
        }
    }

    bool shrink() {
        std::size_t min_cap = (elements * 20 / 17) + 1;

        if (min_cap < 16) {
            min_cap = 16;
        }

        std::size_t new_cap = std::bit_ceil(min_cap);
        
        if (new_cap != capacity) {
            rehash(new_cap);
            return true;
        } else {
            return false;
        }
    }

    const Value& at(const Key& key) const {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t index = hash & (capacity - 1);
        std::uint8_t fp = fingerprint(hash);

        if (control[index] == fp && keys[index] == key) {
            return values[index];
        }

        if (control[index] == EMPTY) {
            throw std::out_of_range("Key not found");
        }

        __m128i fp_vector =
            _mm_set1_epi8(static_cast<char>(fp));

       while (true) {
            __m128i group = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(control + index)
            );

            __m128i fp_comparison =
                _mm_cmpeq_epi8(group, fp_vector);

            __m128i empty_comparison =
                _mm_cmpeq_epi8(group, empty_vector);

            std::uint16_t matches =
                static_cast<std::uint16_t>(
                    _mm_movemask_epi8(fp_comparison)
                );

            std::uint16_t empties =
                static_cast<std::uint16_t>(
                    _mm_movemask_epi8(empty_comparison)
                );

            if (empties != 0) {
                unsigned first_empty = std::countr_zero(empties);

                std::uint16_t before_empty =
                    static_cast<std::uint16_t>(
                        (1u << first_empty) - 1u
                    );

                matches &= before_empty;
            }

            while (matches != 0) {
                unsigned offset = std::countr_zero(matches);

                std::size_t target = (index + offset) & (capacity - 1);

                if (keys[target] == key) {
                    return values[target];
                }

                matches = static_cast<std::uint16_t>(
                    matches & (matches - 1)
                );
            }

            if (empties != 0) {
                throw std::out_of_range("Key not found");
            }

            index = (index + GROUP_SIZE) & (capacity - 1);

        }
    }

    bool contains(const Key& key) const {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t index = hash & (capacity - 1);
        std::uint8_t fp = fingerprint(hash);

        if (control[index] == fp && keys[index] == key) {
            return true;
        }

        if (control[index] == EMPTY) {
            return false;
        }

        __m128i fp_vector =
            _mm_set1_epi8(static_cast<char>(fp));

        while (true) {
            __m128i group = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(control + index)
            );

            __m128i fp_comparison =
                _mm_cmpeq_epi8(group, fp_vector);

            __m128i empty_comparison =
                _mm_cmpeq_epi8(group, empty_vector);

            std::uint16_t matches =
                static_cast<std::uint16_t>(
                    _mm_movemask_epi8(fp_comparison)
                );

            std::uint16_t empties =
                static_cast<std::uint16_t>(
                    _mm_movemask_epi8(empty_comparison)
                );

            if (empties != 0) {
                unsigned first_empty = std::countr_zero(empties);

                std::uint16_t before_empty =
                    static_cast<std::uint16_t>(
                        (1u << first_empty) - 1u
                    );

                matches &= before_empty;
            }

            while (matches != 0) {
                unsigned offset = std::countr_zero(matches);
                
                std::size_t target = (index + offset) & (capacity - 1);

                if (keys[target] == key) {
                    return true;
                }

                matches = static_cast<std::uint16_t>(
                    matches & (matches - 1)
                );
            }

            if (empties != 0) {
                return false;
            }

            index = (index + GROUP_SIZE) & (capacity - 1);
        }
    }

    bool erase(const Key& key) {
        std::size_t hash = std::hash<Key>{}(key);
        std::size_t index = hash & (capacity - 1);
        std::uint8_t fp = fingerprint(hash);

        if (control[index] == fp && keys[index] == key) {
            std::destroy_at(keys + index);
            std::destroy_at(values + index);

            setControl(index, TOMB);
            elements--;

            return true;
        }

        if (control[index] == EMPTY) {
            return false;
        }

        __m128i fp_vector = 
            _mm_set1_epi8(static_cast<char>(fp));

        while (true) {
            __m128i group = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(control + index)
            );

            __m128i fp_comparison = 
                _mm_cmpeq_epi8(group, fp_vector);

            __m128i empty_comparison =
                _mm_cmpeq_epi8(group, empty_vector);

            std::uint16_t matches =
                _mm_movemask_epi8(fp_comparison);

            std::uint16_t empties =
                _mm_movemask_epi8(empty_comparison);

            if (empties != 0) {
                unsigned first_empty = std::countr_zero(empties);

                std::uint16_t before_empty =
                static_cast<std::uint16_t>(
                    (1u << first_empty) - 1u
                );

                matches &= before_empty;
            }

            while (matches != 0) {
                unsigned offset = std::countr_zero(matches);

                std::size_t target = (index + offset) & (capacity - 1);

                if (keys[target] == key) {
                    std::destroy_at(keys + target);
                    std::destroy_at(values + target);

                    setControl(target, TOMB);
                    elements--;

                    return true;
                }

                matches = static_cast<std::uint16_t>(
                    matches & (matches - 1)
                );
            }

            if (empties != 0) {
                return false;
            }

            index = (index + GROUP_SIZE) & (capacity - 1);
        }
    }

    std::size_t size() const {
        return capacity;
    }

    std::size_t count() const {
        return elements;
    }

    bool empty() const {
        return elements == 0;
    }
};
