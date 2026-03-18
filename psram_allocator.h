#pragma once
#include <esp_heap_caps.h>
#include <memory>

template <class T>
struct PsramAllocator {
    typedef T value_type;
    
    PsramAllocator() = default;
    template <class U> constexpr PsramAllocator(const PsramAllocator<U>&) noexcept {}
    
    T* allocate(std::size_t n) {
        if (n > std::size_t(-1) / sizeof(T)) throw std::bad_alloc();
        // Attempt PSRAM first, fallback to internal if full/unavailable
        if (auto p = static_cast<T*>(heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM))) return p;
        if (auto p = static_cast<T*>(malloc(n * sizeof(T)))) return p;
        throw std::bad_alloc();
    }
    
    void deallocate(T* p, std::size_t) noexcept { free(p); }
};

template <class T, class U> bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) { return true; }
template <class T, class U> bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) { return false; }