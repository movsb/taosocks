#pragma once

namespace taosocks {

template<typename T, typename V = unsigned int>
class BaseFlags
{
public:
    BaseFlags()
        : _flags(V())
    { }

    template<typename T, typename ...F>
    inline V set(T flag, F... flags)
    {
        _flags |= (V)flag;
        return set(std::forward<F>(flags)...);
    }

    template<typename T, typename ...F>
    inline V clear(T flag, F... flags)
    {
        _flags &= ~(V)flag;
        return clear(std::forward<F>(flags)...);
    }

    template<typename T>
    inline bool test(T flag) const
    {
        return !!(_flags & (V)flag);
    }

protected:
    V set() { return _flags; }
    V clear() { return _flags; }

protected:
    V _flags;
};

}
