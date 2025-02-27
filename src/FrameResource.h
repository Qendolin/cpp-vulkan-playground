#pragma once

#include <vector>

class FrameResourceManager;

template<typename T>
concept PointerType = std::is_pointer_v<T> || requires(T t)
{
    { *t };
    { t.get() };
};

template<typename T>
class FrameResource {
    const FrameResourceManager *manager;
    std::vector<T> pool = {};

public:
    FrameResource(const FrameResourceManager *manager, std::vector<T> &&pool) : manager(manager),
                                                                                pool(std::move(pool)) {
    }

    ~FrameResource() = default;

    const auto &current() const noexcept;

    auto &current() noexcept;

    const auto &at(int i) const noexcept;

    auto &at(int i) noexcept;

    const auto *operator->() const noexcept;

    auto *operator->() noexcept;
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
        std::vector<T> pool;
        pool.reserve(size_);
        for (int i = 0; i < size_; ++i) {
            pool.emplace_back(supplier());
        }
        return FrameResource<T>(this, std::move(pool));
    }
};


template<typename T>
const auto &FrameResource<T>::current() const noexcept {
    int frame = manager->frame();
    if constexpr (PointerType<T>) {
        return *pool[frame];
    } else {
        return pool[frame];
    }
}


template<typename T>
auto &FrameResource<T>::current() noexcept {
    int frame = manager->frame();
    if constexpr (PointerType<T>) {
        return *pool[frame];
    } else {
        return pool[frame];
    }
}

template<typename T>
const auto &FrameResource<T>::at(int i) const noexcept {
    if constexpr (PointerType<T>) {
        return pool[i % manager->size()].get();
    } else {
        return pool[i % manager->size()];
    }
}

template<typename T>
auto &FrameResource<T>::at(int i) noexcept {
    if constexpr (PointerType<T>) {
        return pool[i % manager->size()].get();
    } else {
        return pool[i % manager->size()];
    }
}

template<typename T>
const auto *FrameResource<T>::operator->() const noexcept {
    return &current();
}

template<typename T>
auto *FrameResource<T>::operator->() noexcept {
    return &current();
}
