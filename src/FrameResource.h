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
    { static_cast<bool>(t) }; // Must be convertible to bool
};

template<PointerType T, typename Supplier, size_t Size>
class FrameResource {
    const FrameResourceManager<Size> *manager;
    std::array<T, Size> pool = {};
    Supplier supplier;

public:
    FrameResource(const FrameResourceManager<Size> *manager, Supplier supplier)
        : manager(manager), supplier(supplier) {
    }

    ~FrameResource() = default;

    auto get() {
        int frame = manager->frame();
        if (!pool[frame]) {
            pool[frame] = supplier();
        }
        return pool[frame].get();
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
    auto create(Supplier &&supplier) requires PointerType<std::invoke_result_t<Supplier> > {
        using T = std::invoke_result_t<Supplier>;
        return FrameResource<T, Supplier, Size>(this, std::forward<Supplier &&>(supplier));
    }
};
