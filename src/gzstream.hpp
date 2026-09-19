#pragma once

#include <zlib.h>

#include <istream>
#include <streambuf>
#include <string>

namespace BLib {

// gzread reads uncompressed files unchanged, so this opens plain and .gz inputs alike.
class GzStreamBuf : public std::streambuf {
public:
    explicit GzStreamBuf(const std::string& fname) : f(gzopen(fname.c_str(), "rb")) {}
    ~GzStreamBuf() override {
        if (f) gzclose(f);
    }
    GzStreamBuf(const GzStreamBuf&) = delete;
    GzStreamBuf& operator=(const GzStreamBuf&) = delete;
    bool is_open() const { return f != nullptr; }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        if (!f) return traits_type::eof();
        const int n = gzread(f, buf, sizeof(buf));
        if (n <= 0) return traits_type::eof();
        setg(buf, buf, buf + n);
        return traits_type::to_int_type(*gptr());
    }

private:
    gzFile f;
    char buf[1 << 16];
};

class GzIfstream : public std::istream {
public:
    explicit GzIfstream(const std::string& fname) : std::istream(nullptr), sb(fname) {
        rdbuf(&sb);
        if (!sb.is_open()) setstate(std::ios::failbit);
    }

private:
    GzStreamBuf sb;
};

} // namespace BLib
