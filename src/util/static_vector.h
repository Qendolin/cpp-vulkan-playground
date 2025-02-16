#pragma once

#include <array>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <cassert>

#include <array>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <cassert>

#include <array>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <cassert>

namespace util {
    template<typename T, std::size_t N>
    class static_vector {
        std::array<T, N> storage_;
        std::size_t length_ = 0;

    public:
        using value_type = T;
        using size_type = std::size_t;
        using iterator = typename std::array<T, N>::iterator;
        using const_iterator = typename std::array<T, N>::const_iterator;

        constexpr static_vector() = default;

        constexpr static_vector(std::initializer_list<T> init) {
            if (init.size() > N) throw std::out_of_range("Initializer list too large");
            std::copy(init.begin(), init.end(), storage_.begin());
            length_ = init.size();
        }

        template<typename InputIt>
        constexpr static_vector(InputIt first, InputIt last) {
            size_type count = std::distance(first, last);
            if (count > N) throw std::out_of_range("Range exceeds static_vector capacity");
            std::copy(first, last, storage_.begin());
            length_ = count;
        }

        template<std::size_t M>
        constexpr static_vector(const std::array<T, M> &arr) // NOLINT(*-explicit-constructor)
        {
            static_assert(M <= N, "Array size exceeds static_vector capacity");
            std::copy(arr.begin(), arr.end(), storage_.begin());
            length_ = M;
        }

        [[nodiscard]] constexpr bool empty() const noexcept { return length_ == 0; }
        [[nodiscard]] constexpr bool full() const noexcept { return length_ == N; }
        [[nodiscard]] constexpr size_type size() const noexcept { return length_; }
        [[nodiscard]] static constexpr size_type capacity() noexcept { return N; }
        [[nodiscard]] constexpr T *data() noexcept { return storage_.data(); }
        [[nodiscard]] constexpr const T *data() const noexcept { return storage_.data(); }

        constexpr void clear() noexcept { length_ = 0; }

        constexpr void push_back(const T &value) {
            if (length_ >= N) throw std::out_of_range("static_vector capacity exceeded");
            storage_[length_++] = value;
        }

        constexpr void push_back(T &&value) {
            if (length_ >= N) throw std::out_of_range("static_vector capacity exceeded");
            storage_[length_++] = std::move(value);
        }

        template<typename... Args>
        constexpr T &emplace_back(Args &&... args) {
            if (length_ >= N) throw std::out_of_range("static_vector capacity exceeded");
            return storage_[length_++] = T(std::forward<Args>(args)...);
        }

        constexpr void pop_back() {
            assert(length_ > 0 && "static_vector is empty");
            --length_;
        }

        constexpr T &front() {
            assert(length_ > 0 && "static_vector is empty");
            return storage_[0];
        }

        constexpr const T &front() const {
            assert(length_ > 0 && "static_vector is empty");
            return storage_[0];
        }

        constexpr T &back() {
            assert(length_ > 0 && "static_vector is empty");
            return storage_[length_ - 1];
        }

        constexpr const T &back() const {
            assert(length_ > 0 && "static_vector is empty");
            return storage_[length_ - 1];
        }

        constexpr T &operator[](size_type index) { return storage_[index]; }
        constexpr const T &operator[](size_type index) const { return storage_[index]; }

        constexpr T &at(size_type index) {
            if (index >= length_) throw std::out_of_range("Index out of range");
            return storage_[index];
        }

        constexpr const T &at(size_type index) const {
            if (index >= length_) throw std::out_of_range("Index out of range");
            return storage_[index];
        }

        constexpr iterator begin() noexcept { return storage_.begin(); }
        constexpr const_iterator begin() const noexcept { return storage_.begin(); }
        constexpr const_iterator cbegin() const noexcept { return storage_.cbegin(); }

        constexpr iterator end() noexcept { return storage_.begin() + length_; }
        constexpr const_iterator end() const noexcept { return storage_.begin() + length_; }
        constexpr const_iterator cend() const noexcept { return storage_.cbegin() + length_; }

        constexpr void erase(iterator pos) {
            if (pos < begin() || pos >= end()) throw std::out_of_range("Iterator out of range");
            std::move(pos + 1, end(), pos);
            --length_;
        }

        constexpr void erase(iterator first, iterator last) {
            if (first < begin() || last > end() || first > last) throw std::out_of_range("Iterator range invalid");
            std::move(last, end(), first);
            length_ -= last - first;
        }
    };
}
