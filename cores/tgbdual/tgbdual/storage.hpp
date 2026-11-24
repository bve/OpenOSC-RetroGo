#pragma once
#include <new>

template<class T, class... Args>
T *make_object(Args... args)
{
    alignas(T) static unsigned char storage[sizeof(T)];
    return new (storage) T(args...);
}
