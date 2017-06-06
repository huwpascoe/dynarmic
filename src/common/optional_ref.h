/* This file is part of the dynarmic project.
 * Copyright (c) 2017 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <memory>
#include <optional>

namespace Dynarmic {
namespace Common {

template <typename T>
class optional_ref {
public:
    constexpr optional_ref() noexcept {}
    constexpr optional_ref(std::nullopt_t) noexcept {}
    constexpr optional_ref(T& v) : ptr(std::addressof(v)) {}

    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    optional_ref(const optional_ref<U>& src) noexcept : ptr(src.ptr) {}

    constexpr bool has_value() const noexcept {
        return ptr != nullptr;
    }

    constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    constexpr T& operator*() const {
        return *ptr;
    }

    constexpr T* operator->() const {
        return ptr;
    }

    constexpr T& value() const {
        if (!has_value())
            throw std::bad_optional_access();
        return *ptr;
    }

    template <typename U, typename T_ = T, typename = std::enable_if_t<std::is_copy_constructible<T_>::value>>
    constexpr T_ value_or(U&& default_value) const {
        return has_value() ? **this : static_cast<T>(std::forward<U>(default_value));
    }

    optional_ref& operator=(const optional_ref&) = delete;

private:
    T* ptr = nullptr;

    template <typename>
    friend class optional_ref;
};

template <typename T>
constexpr optional_ref<T> make_optional_ref(T& t) noexcept {
    return t;
}

} // namespace Common
} // namespace Dynarmic
