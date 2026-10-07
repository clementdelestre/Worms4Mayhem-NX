// A per-voxel array stored per 32³ chunk (index: Terrain::idx). A chunk of one value points at a shared read-only block of
// that value; writes give it its own block first (copy on write).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

template <class T> struct Bricks {
    static_assert(sizeof(T) == 1, "one byte per voxel");
    static constexpr int B = 15;
    static constexpr size_t N = (size_t)1 << B, M = N - 1;

    Bricks() = default;
    Bricks(Bricks &&) = default;
    Bricks &operator=(Bricks &&) = default;
    Bricks(const Bricks &o) { *this = o; }
    Bricks &operator=(const Bricks &o) {
        if (this == &o) return *this;
        blk = o.blk, own.clear(), own.resize(o.own.size());
        for (size_t c = 0; c < own.size(); c++)
            if (o.own[c]) own[c].reset(new T[N]), memcpy(own[c].get(), o.blk[c], N), blk[c] = own[c].get();
        return *this;
    }
    bool operator==(const Bricks &o) const {
        if (blk.size() != o.blk.size()) return false;
        for (size_t c = 0; c < blk.size(); c++) if (blk[c] != o.blk[c] && memcmp(blk[c], o.blk[c], N)) return false;
        return true;
    }

    void assign(size_t chunks, T v) { own.clear(), own.resize(chunks), blk.assign(chunks, uniform(v)); }
    void fill(T v) { assign(blk.size(), v); }
    void clear() { std::vector<const T *>().swap(blk), std::vector<std::unique_ptr<T[]>>().swap(own); }
    bool empty() const { return blk.empty(); }
    size_t size() const { return blk.size() << B; }
    size_t bytes() const { return blk.capacity() * sizeof(T *) + own.capacity() * sizeof(own[0]) + owned() * N; }
    size_t owned() const { return std::count_if(own.begin(), own.end(), [](const auto &p) { return p != nullptr; }); }

    T operator[](size_t i) const { return blk[i >> B][i & M]; }
    const T *chunk(size_t c) const { return blk[c]; }
    bool shared(size_t c) const { return !own[c]; }  // one value throughout: chunk(c)[0]
    T *write(size_t c) {
        if (!own[c]) own[c].reset(new T[N]), memcpy(own[c].get(), blk[c], N), blk[c] = own[c].get();
        return own[c].get();
    }
    T *overwrite(size_t c) {  // its own block, contents left to the caller
        if (!own[c]) own[c].reset(new T[N]), blk[c] = own[c].get();
        return own[c].get();
    }
    T &w(size_t i) { return write(i >> B)[i & M]; }
    void set(size_t i, T v) { if ((*this)[i] != v) w(i) = v; }
    void share(size_t c) {  // an own block of one value goes back to the shared one
        const T *p = blk[c];
        if (own[c] && std::all_of(p, p + N, [&](T v) { return v == p[0]; })) blk[c] = uniform(p[0]), own[c].reset();
    }
    void fillChunk(size_t c, T v) { own[c].reset(), blk[c] = uniform(v); }
    void shareAll() { for (size_t c = 0; c < own.size(); c++) share(c); }

    static const T *uniform(T v) {  // blocks live for the whole run, one per value used
        static std::mutex mu;
        static std::unique_ptr<T[]> u[256];
        std::lock_guard<std::mutex> l(mu);
        auto &p = u[(uint8_t)v];
        if (!p) p.reset(new T[N]), memset(p.get(), (uint8_t)v, N);
        return p.get();
    }

private:
    std::vector<const T *> blk;  // per chunk: own[c] or a shared block
    std::vector<std::unique_ptr<T[]>> own;
};
