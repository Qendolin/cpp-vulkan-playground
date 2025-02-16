#pragma once

#include <functional>
#include <array>

class FrameResourceManager;

template<typename T>
concept PointerType = std::is_pointer_v<T> || requires(T t)
{
    { *t }; // Dereferenceable
    { t.get() }; // Has a get() method like smart pointers
};

template<typename T>
class FrameResource {
    const FrameResourceManager *manager;
    const std::vector<T> pool = {};

public:
    FrameResource(const FrameResourceManager *manager, std::vector<T> &&pool) : manager(manager),
                                                                                pool(std::move(pool)) {
    }

    ~FrameResource() = default;

    auto &get() {
        int frame = manager->frame();
        if constexpr (PointerType<T>) {
            return *pool[frame];
        } else {
            return pool[frame];
        }
    }

    auto &get(int i) {
        if constexpr (PointerType<T>) {
            return pool[i % manager->size()].get();
        } else {
            return pool[i % manager->size()];
        }
    }
};

class FrameResourceManager {
    int size_ = 1;
    int current_ = 0;

public:
    explicit FrameResourceManager(int size) : size_(size) {

    }

    ~FrameResourceManager() = default;

    [[nodiscard]] int frame() const {
        return current_;
    }

    void advance() {
        current_ = (current_ + 1) % size_;
    }

    [[nodiscard]] int size() const {
        return size_;
    }

    template<typename Supplier>
    auto create(Supplier &&supplier) {
        using T = std::invoke_result_t<Supplier>;
        std::vector<T> pool(size_);
        for (int i = 0; i < size_; ++i) {
            pool[i] = supplier();
        }
        return FrameResource<T>(this, std::move(pool));
    }
};
