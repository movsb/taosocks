#pragma once

#include <cassert>
#include <cstring>
#include <string>

namespace taosocks {

class DataWindow
{
public:
    DataWindow(size_t size = 1024)
        : _dat(nullptr)
        , _cap(size)
        , _beg(0)
        , _end(0)
    {
        if(size == 0) size = 1;
        _dat = new unsigned char[size];
    }

public:
    void append(unsigned char c)
    {
        return append(&c, sizeof(c));
    }

    void append(const void* data, size_t size)
    {
        ensure_capacity(size);
        std::memcpy(_dat + _end, data, size);
        _end += size;
    }

    size_t size() const
    {
        return _end - _beg;
    }

    void clear()
    {
        _beg = 0;
        _end = 0;
    }

    void get(void* p, size_t n)
    {
        check_size(0, n);
        std::memcpy(p, _dat + _beg, n);
        _beg += n;
    }

    void* data() const
    {
        return _dat + _beg;
    }

    unsigned char get_byte()
    {
        return __get<unsigned char>(0, true);
    }

    unsigned short get_short()
    {
        return __get<unsigned short>(0, true);
    }

    unsigned int get_int()
    {
        return __get<unsigned int>(0, true);
    }

    unsigned char peek_byte(size_t offset)
    {
        return __get<unsigned char>(offset, false);
    }

    unsigned short peek_short(size_t offset)
    {
        return __get<unsigned short>(offset, false);
    }

    unsigned int peek_int(size_t offset)
    {
        return __get<unsigned int>(offset, false);
    }

    std::string get_string(size_t count)
    {
        check_size(0, count);
        std::string s = {_dat + _beg, _dat + _beg + count};
        _beg += count;
        return std::move(s);
    }

    int index_char(unsigned char c)
    {
        int index = -1;

        for(size_t i = _beg; i < _end; i++) {
            if(_dat[i] == c) {
                index = i - _beg;
                break;
            }
        }

        return index;
    }

    template<typename T>
    T* try_cast()
    {
        return size() >= sizeof(T)
            ? reinterpret_cast<T*>(_dat + _beg)
            : nullptr
            ;
    }

    template<typename T>
    T* get_obj()
    {
        return __get<T>(0, true);
    }

private:
    template<typename T>
    inline T __get(size_t offset, bool remove)
    {
        check_size(offset, sizeof(T));

        T t = *(T*)&_dat[_beg + offset];
        if(offset == 0 && remove) {
            _beg += sizeof(T);
        }
        return t;
    }

    void ensure_capacity(size_t increment)
    {
        // if enough from current offset
        if(_cap - _end >= increment) {
            return;
        }

        // if enough from the beginning
        if(_beg > 0 && _beg + (_cap - _end) >= increment) {
            std::memmove(_dat + 0, _dat + _beg, size());
            _end = size();
            _beg = 0;
            return;
        }

        // re-allocate
        size_t required = size() + increment;
        size_t scale = 2;
        size_t capacity = required * scale;
        unsigned char* p = new unsigned char[capacity];
        std::memcpy(p, _dat + _beg, size());
        delete[] _dat;

        _end = size();
        _beg = 0;
        _cap = capacity;
        _dat = p;
    }

    inline void check_size(size_t offset, size_t n)
    {
        if(size() - offset < n) {
            throw "invalid size";
        }
    }

private:
    size_t _beg;
    size_t _end;
    size_t _cap;
    unsigned char* _dat;
};

/*
int main()
{
    DataWindow dw(16);

    assert(dw.size() == 0);

    dw.append((unsigned char*)"asdf", 4);
    assert(dw.size() == 4);

    assert(dw.index_char('d') == 2);
    assert(dw.index_char('x') == -1);

    dw.append((unsigned char*)"asdf", 4);
    assert(dw.size() == 8);

    assert(dw.peek_byte(0) == 'a');
    assert(dw.peek_byte(3) == 'f');

    assert(dw.get_int() == 'fdsa');
    assert(dw.size() == 4);

    assert(dw.get_byte() == 'a');
    assert(dw.peek_short(0) == 'ds');

    dw.clear();
    dw.append((unsigned char*)"01234567890123456789", 20);

    assert(dw.get_string(10) == "0123456789");
    assert(dw.size() == 10);

    dw.append((unsigned char*)"01234567890123456789123", 23);
}
*/

}

