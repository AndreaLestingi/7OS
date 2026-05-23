#ifndef TYPES_H
#define TYPES_H

typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

void* kmalloc(u32 size);
void kfree(void* ptr);

template <typename T>
class ArrayList {
    T* data;
    u32 len;
    u32 cap;

    void grow(u32 new_cap) {
        T* new_data = (T*)kmalloc(sizeof(T) * new_cap);
        for (u32 i = 0; i < len; i++) {
            new_data[i] = data[i];
        }
        if (data) {
            kfree(data);
        }
        data = new_data;
        cap = new_cap;
    }

public:
    ArrayList() : data(0), len(0), cap(0) {}
    ~ArrayList() {
        if (data) {
            kfree(data);
        }
    }

    ArrayList(const ArrayList&) = delete;
    ArrayList& operator=(const ArrayList&) = delete;
    ArrayList(ArrayList&& other) : data(other.data), len(other.len), cap(other.cap) {
        other.data = 0;
        other.len = 0;
        other.cap = 0;
    }
    ArrayList& operator=(ArrayList&& other) {
        if (this == &other) return *this;
        if (data) {
            kfree(data);
        }
        data = other.data;
        len = other.len;
        cap = other.cap;
        other.data = 0;
        other.len = 0;
        other.cap = 0;
        return *this;
    }

    u32 size() const { return len; }
    u32 capacity() const { return cap; }
    bool empty() const { return len == 0; }

    T& operator[](u32 index) { return data[index]; }
    const T& operator[](u32 index) const { return data[index]; }

    void clear() { len = 0; }

    bool contains(const T& value) const {
        for (u32 i = 0; i < len; i++) {
            if (value.equals(data[i])) {
                return true;
            }
        }
        return false;
    }

    void release() {
        if (data) {
            kfree(data);
            data = 0;
        }
        len = 0;
        cap = 0;
    }

    void reserve(u32 new_cap) {
        if (new_cap <= cap) return;
        grow(new_cap);
    }

    void push_back(const T& value) {
        if (len + 1 > cap) {
            u32 new_cap = (cap == 0) ? 4 : (cap * 2);
            grow(new_cap);
        }
        data[len++] = value;
    }

    void pop_back() {
        if (len == 0) return;
        len--;
    }

    void remove_at(u32 index) {
        if (index >= len) return;
        for (u32 i = index + 1; i < len; i++) {
            data[i - 1] = data[i];
        }
        len--;
    }
};

#endif
