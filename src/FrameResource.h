#pragma once

#include <functional>
#include <array>

template<size_t Size>
class FrameResourceManager;

template<typename T>
concept PointerType = std::is_pointer_v<T> || requires(T t)
{
    { *t }; // Dereferenceable
    { t.get() }; // Has a get() method like smart pointers
};

template<typename T, size_t Size>
class FrameResource {
    const FrameResourceManager<Size> *manager;
    const std::array<T, Size> pool = {};

public:
    FrameResource(const FrameResourceManager<Size> *manager, std::array<T, Size> &&pool) : manager(manager),
                                                                                           pool(std::move(pool)) {
    }

    ~FrameResource() = default;

    auto &get() {
        int frame = manager->frame();
        if constexpr (PointerType<T>) {
            return pool[frame].get();
        } else {
            return pool[frame];
        }
    }

    auto &get(int i) {
        if constexpr (PointerType<T>) {
            return pool[i % Size].get();
        } else {
            return pool[i % Size];
        }
    }
};

template<size_t Size>
class FrameResourceManager {
private:
    int current = 0;

public:
    explicit FrameResourceManager() = default;

    ~FrameResourceManager() = default;

    [[nodiscard]] int frame() const {
        return current;
    }

    void advance() {
        current = (current + 1) % Size;
    }

    [[nodiscard]] int size() const {
        return Size;
    }

    template<typename Supplier>
    auto create(Supplier &&supplier) {
        using T = std::invoke_result_t<Supplier>;
        std::array<T, Size> pool = {};
        for (int i = 0; i < Size; ++i) {
            pool[i] = supplier();
        }
        return FrameResource<T, Size>(this, std::move(pool));
    }
};
