#ifndef STRING_H
#define STRING_H

#include "../util/types.h"
#include "../mem/kheap.h"

class String {
    char* data;
    u32 len;
public:
    String();
    String(const char* s);
    String(const String& other);
    ~String();

    const char* c_str() const { return data; }
    u32 length() const { return len; }

    char operator[](int index) const;
    String& operator+=(char c);
    String& operator+=(const String& other);
    
    void append(char c);
    void pop_back();
    bool equals(const char* s) const;
    bool equals(const String& other) const;
    int size() const;

    String toString(int c);

    ArrayList<String> split(char delimiter) const;
    ArrayList<String> split(const char* delimiter) const;
};

#endif
