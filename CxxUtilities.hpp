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
template <auto FuncPtr> struct Releaser { void operator()(typename Arg0<decltype(FuncPtr)>::type p) { if (p != nullptr) FuncPtr(p); } };

//====================================================================
template <typename T, typename Lambda>
struct ScopedReleaser {
    ScopedReleaser(T _what, Lambda && _lambda) : what(_what), lambda(std::move(_lambda)) {}
    ~ScopedReleaser() { lambda(what); }
    T get()      { return what; }
    operator T() { return what; }
private:
    T what;
    Lambda lambda;
};

template <typename T, typename Lambda>
auto callAtEndOfScope(T what, Lambda && lambda) { return ScopedReleaser<T, Lambda>(what, std::move(lambda)); }

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
    using Type = typename decltype(std::shared_ptr(factory(std::forward<Args>(args)...)))::element_type;
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
}
