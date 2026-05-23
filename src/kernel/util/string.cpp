#include "string.h"
#include "util.h"

String::String() : data(0), len(0) {
    data = new char[1];
    data[0] = '\0';
}

String::String(const char* s) {
    len = 0;
    while(s[len]) len++;
    data = new char[len + 1];
    for(u32 i = 0; i < len; i++) data[i] = s[i];
    data[len] = '\0';
}

String::String(const String& other) {
    len = other.len;
    data = new char[len + 1];
    for(u32 i = 0; i <= len; i++) data[i] = other.data[i];
}

String::~String() { /* Libera se avessimo delete */ }

void String::append(char c) {
    char* new_data = new char[len + 2];
    for(u32 i = 0; i < len; i++) new_data[i] = data[i];
    new_data[len] = c;
    new_data[len + 1] = '\0';
    data = new_data;
    len++;
}

void String::pop_back() {
    if (len > 0) {
        len--;
        data[len] = '\0';
    }
}

bool String::equals(const char* s) const {
    u32 i = 0;
    while (data[i] && s[i]) {
        if (data[i] != s[i]) return false;
        i++;
    }
    return data[i] == s[i];
}

bool String::equals(const String& other) const {
    if (len != other.len) return false;
    for (u32 i = 0; i < len; i++) {
        if (data[i] != other.data[i]) return false;
    }
    return true;
}

char String::operator[](int index) const {
    if(index < 0 || index >= (int)len)
        return '\0';

    return data[index];
}

String& String::operator+=(char c) {
    char* new_data = new char[len + 2];

    for (u32 i = 0; i < len; i++)
        new_data[i] = data[i];

    new_data[len] = c;
    new_data[len + 1] = '\0';

    delete[] data;
    data = new_data;
    len++;

    return *this;
}

String& String::operator+=(const String& other) {
    char* new_data = new char[len + other.len + 1];

    for (u32 i = 0; i < len; i++)
        new_data[i] = data[i];

    for (u32 i = 0; i < other.len; i++)
        new_data[len + i] = other.data[i];

    new_data[len + other.len] = '\0';

    delete[] data;
    data = new_data;
    len += other.len;

    return *this;
}

String toString(int x) {
    if (x == 0) return String("0");

    char buf[16];
    int i = 0;

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    String s;
    for (int j = i - 1; j >= 0; j--) {
        s += buf[j];
    }

    return s;
}

int String::size() const {
    return len;
}

ArrayList<String> String::split(char delimiter) const {
    ArrayList<String> parts;
    u32 start = 0;

    for (u32 i = 0; i <= len; i++) {
        if (i == len || data[i] == delimiter) {
            u32 seg_len = i - start;
            if (seg_len > 0) {
                char* temp = new char[seg_len + 1];
                for (u32 j = 0; j < seg_len; j++) {
                    temp[j] = data[start + j];
                }
                temp[seg_len] = '\0';
                String part(temp);
                delete[] temp;
                parts.push_back(part);
            }
            start = i + 1;
        }
    }

    return parts;
}

ArrayList<String> String::split(const char* delimiter) const {
    if (!delimiter || delimiter[0] == '\0') {
        ArrayList<String> parts;
        String copy(data);
        parts.push_back(copy);
        return parts;
    }

    return split(delimiter[0]);
}