#include "ob/Bench.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<ob::u64> gAllocs{0};
std::atomic<ob::u64> gFrees{0};
std::atomic<ob::u64> gBytes{0};

void* Allocate(std::size_t n) {
    if (n == 0)
        n = 1;
    void* p = std::malloc(n);
    if (!p)
        throw std::bad_alloc();
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    gBytes.fetch_add(n, std::memory_order_relaxed);
    return p;
}

void* AllocateAligned(std::size_t n, std::size_t align) {
    if (n == 0)
        n = 1;
    if (align < sizeof(void*))
        align = sizeof(void*);
    void* p = nullptr;
    if (posix_memalign(&p, align, n) != 0)
        throw std::bad_alloc();
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    gBytes.fetch_add(n, std::memory_order_relaxed);
    return p;
}

void Release(void* p) noexcept {
    if (!p)
        return;
    gFrees.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}

} // namespace

namespace ob {

u64 AllocationCount() {
    return gAllocs.load(std::memory_order_relaxed);
}
u64 DeallocationCount() {
    return gFrees.load(std::memory_order_relaxed);
}
u64 AllocatedBytes() {
    return gBytes.load(std::memory_order_relaxed);
}

} // namespace ob

void* operator new(std::size_t n) {
    return Allocate(n);
}
void* operator new[](std::size_t n) {
    return Allocate(n);
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return Allocate(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return Allocate(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(std::size_t n, std::align_val_t a) {
    return AllocateAligned(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return AllocateAligned(n, static_cast<std::size_t>(a));
}
void* operator new(std::size_t n, std::align_val_t a,
                   const std::nothrow_t&) noexcept {
    try {
        return AllocateAligned(n, static_cast<std::size_t>(a));
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, std::align_val_t a,
                     const std::nothrow_t&) noexcept {
    try {
        return AllocateAligned(n, static_cast<std::size_t>(a));
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept {
    Release(p);
}
void operator delete[](void* p) noexcept {
    Release(p);
}
void operator delete(void* p, std::size_t) noexcept {
    Release(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    Release(p);
}
void operator delete(void* p, std::align_val_t) noexcept {
    Release(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    Release(p);
}
void operator delete(void* p, std::size_t,
                     std::align_val_t) noexcept {
    Release(p);
}
void operator delete[](void* p, std::size_t,
                       std::align_val_t) noexcept {
    Release(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    Release(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    Release(p);
}
