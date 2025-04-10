//
//  CxxUtilities.hpp
//  cxxutilities - https://github.com/hogliux/cxxutilities
//
//  Created by Fabian Renn-Giles, fabian@fieldingdsp.com on 17th March 2022.
//  Copyright © 2022 Fielding DSP GmbH, All rights reserved.
//
//  Fielding DSP GmbH
//  Jägerstr. 36
//  14467 Potsdam, Germany
//
#pragma once
#include <optional>
#include <utility>
#include <iostream>
#include <cmath>
#include <memory>
#include <functional>
#include <mutex>
#include <type_traits>

// see https://gcc.gnu.org/onlinedocs/cpp/_005f_005fhas_005finclude.html
#if defined __has_include
#  if __has_include (<bit>)
#    include <bit>
#  endif
#endif


namespace cxxutils {
namespace detail {

template <typename T> struct OptionalUnpackerHelper { using type = T; };
template <typename T> struct OptionalUnpackerHelper<std::optional<T>> { using type = T; };
template <typename T> using OptionalUnpacker = typename OptionalUnpackerHelper<T>::type;

template <typename E, typename F, std::size_t... Is>
auto apply_helper(E value, F && _lambda, std::index_sequence<Is...>) {
    using LambdaReturnType = std::invoke_result_t<F, std::integral_constant<E, static_cast<E>(0)>>;
    static constexpr auto hasVoidReturnType = std::is_same_v<LambdaReturnType, void>;
    
    using OurReturnType = std::conditional_t<hasVoidReturnType, bool, std::optional<OptionalUnpacker<LambdaReturnType>>>;
    
    OurReturnType result;
    bool hasValue = false;
    (..., [&result, &hasValue, value, lambda = std::move(_lambda)] (auto _)
    {
        static constexpr auto option = static_cast<E>(decltype(_)::value);
        if ((! hasValue) && option == value) {
            
            if constexpr (hasVoidReturnType) {
                lambda(std::integral_constant<E, option>());
                result = true;
            } else {
                result = lambda(std::integral_constant<E, option>());
            }
            
            hasValue = true;
        }
    }(std::integral_constant<std::size_t, Is>()));
    
    return result;
}
}

template <typename E, typename F, E Max>
auto constexpr_apply(E value, std::integral_constant<E, Max>,  F && lambda) {
    return detail::apply_helper<E, F>(value, std::move(lambda), std::make_index_sequence<static_cast<std::size_t>(Max)>());
}


template <typename Tp, Tp... Ips, typename F>
auto invoke_with_sequence(std::integer_sequence<Tp, Ips...>, F && lambda) {
    return lambda(std::integral_constant<Tp, Ips>()...);
}

//====================================================================
template <typename> struct Arg0 {};
template <typename T> struct Arg0<void (*)(T)> { using type = T; };
template <typename T> struct Arg0<void (*)(T) noexcept> { using type = T; };
template <auto FuncPtr> struct Releaser { void operator()(typename Arg0<decltype(FuncPtr)>::type p) { if (p != nullptr) FuncPtr(p); } };

//====================================================================
template <typename T, typename Lambda>
struct ScopedReleaser {
    ScopedReleaser(T _what, Lambda && _lambda) : what(_what), lambda(std::move(_lambda)) {}
    ~ScopedReleaser() { if (what.has_value()) lambda(*what); }
    T get()      { return *what; }
    operator T() { return *what; }
private:
    std::optional<T> what;
    Lambda lambda;
};

template <typename T, typename Lambda>
auto callAtEndOfScope(T what, Lambda && lambda) { return ScopedReleaser<T, Lambda>(what, std::move(lambda)); }

//====================================================================
template <typename T>
struct ScopedSetter {
    ScopedSetter(T& what, T  const& newValue) : value(what), previous(what) { what = newValue; }
    ScopedSetter(T& what, T  &&     newValue) : value(what), previous(what) { what = std::move(newValue); }
    ~ScopedSetter() { value = std::move(previous); }
private:
    T& value;
    T previous;
};

//====================================================================
// This works well for samples as they are (usually) between -1 and 1
// This is not a good solution for floats with higher order of magnitudes
// where epsilon would need to be scaled.
// TODO: Consider using AlmostEquals from Google's C++ testing framework
// which is much more performant than using fp routines.
template <typename T> bool fltIsEqual(T const a, T const b)   { return std::fabs(a - b) <= std::numeric_limits<T>::epsilon(); }

//====================================================================
// min, max and clamp utilities
template <typename T> struct Range { T min; T max; Range& operator|=(T value) noexcept { min = std::min(min, value); max = std::max(max, value); return *this; } };
template <typename T> T clamp(T const value, Range<T> const range) noexcept { return std::min(std::max(value, range.min), range.max); }
template <typename T> T clamp(T const value, T const absMax) noexcept { return clamp(value, Range<T> {.min = static_cast<T>(-1) * absMax, .max = absMax}); }
template <typename Arg0> auto min(Arg0 arg0) noexcept { return arg0; }
template <typename Arg0, typename... Args> auto min(Arg0 arg0, Args... args) noexcept { return std::min(arg0, min(args...)); }
template <typename Arg0> auto max(Arg0 arg0) noexcept { return arg0; }
template <typename Arg0, typename... Args> auto max(Arg0 arg0, Args... args) noexcept { return std::max(arg0, max(args...)); }
template <typename... Args> auto range(Args... args) noexcept { return Range<decltype(min(args...))> { .min = min(args...), .max = max(args...) }; }

//====================================================================
// Rounds the number up or down depending on the direction parameter
// TODO: There may be a more performant version of this where we multiply with the sign of dir 
// then ceil and then multiply with the sign of dir again
template <typename T> T dround(T const x, T const dir) noexcept { return dir >= static_cast<T>(0) ? std::ceil(x) : std::floor(x); }

//====================================================================
/** Create a reference counted singleton object */
template <typename Fn, typename... Args>
auto getOrCreate(Fn && factory, Args&&... args) {
    // here we use CTAD to help us deduce the pointer's type
    using PointerType = std::remove_reference_t<std::remove_cv_t<std::invoke_result_t<Fn, Args...>>>;
    using Type = std::pointer_traits<PointerType>::element_type;
    std::shared_ptr<Type> _shared;
    static std::weak_ptr<Type> _weak = std::invoke([&_shared, factory] (Args&&... _args) {
        _shared = std::shared_ptr<Type>(factory(std::forward<Args>(_args)...));
        return _shared;
    }, std::forward<Args>(args)...);

    if (auto ptr = _weak.lock())
        return ptr;

    _shared = std::shared_ptr<Type>(factory(std::forward<Args>(args)...));
    _weak = _shared;
    return _shared;
}

//====================================================================
// A wrapper for C++-23's bit_cast for older versions of C++
template <typename T, typename U>
std::enable_if_t<sizeof(T) == sizeof(U) && std::is_trivially_copyable_v<U> && std::is_trivially_copyable_v<T>, T>
bit_cast(const U& src) noexcept
{
   #if defined(__cpp_lib_bit_cast) && __cplusplus >= __cpp_lib_bit_cast
    return std::bit_cast<T>(src);
   #else
    static_assert(std::is_trivially_constructible_v<T>,
        "This implementation additionally requires "
        "destination type to be trivially constructible");
 
    To dst;
    std::memcpy(&dst, &src, sizeof(T));
    return dst;
   #endif
}

//====================================================================
// Inspired by boosts reverse-lock but also works with STL std::unique_lock
template <typename M>
class reverse_lock
{
public:
    explicit reverse_lock(std::unique_lock<M>& _lock) : lock(_lock) {
        if (lock.owns_lock()) {
            lock.unlock();
            unlocked = true;
        }
    }

    reverse_lock(const reverse_lock&) = delete;
    reverse_lock& operator=(const reverse_lock&) = delete;
    reverse_lock(reverse_lock&& other) noexcept : lock(other.lock), unlocked(other.unlocked) { other.unlocked = false; }

    reverse_lock& operator=(reverse_lock&& other) noexcept
    {
        if (this != &other) {
            if (unlocked) {
                lock.lock(); // Re-lock if currently unlocked
                unlocked = false;
            }
            lock = other.lock;
            std::swap(unlocked, other.unlocked);
        }
        return *this;
    }

    ~reverse_lock() {
        if (unlocked) {
            lock.lock();
        }
    }

private:
    std::unique_lock<M>& lock;
    bool unlocked = false;
};

//====================================================================
template<typename T>
constexpr std::enable_if_t<std::is_integral_v<T>, T> byteswap(T value) noexcept {
   #if defined(cpp_lib_byteswap) && cplusplus >= __cpp_lib_byteswap
    return std::byteswap<T>(value);
   #else
    static_assert(std::has_unique_object_representations_v<T>,  "T may not have padding bits");
    auto byte_representation = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    auto const reverse_byte_representation = std::invoke([&byte_representation] () {
        std::array<std::byte, sizeof(T)> result;

        for (std::size_t i = 0; i < sizeof(T); ++i)
            result[sizeof(T)-i-1] = byte_representation[i];

        return result;
    });
    return std::bit_cast<T>(reverse_byte_representation);
   #endif
}
}
