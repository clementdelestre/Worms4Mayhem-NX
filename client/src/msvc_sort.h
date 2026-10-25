#pragma once
#include <cstddef>
#include <utility>
#include <vector>

// W4M's std::sort of the render bins (MSVC 8 introsort: 0x6ed4d3 / 0x6ed417, partition 0x6ec919, median 0x6ec2f2, insertion
// sort 0x6ec3bb, heap 0x6ec385 / 0x6ecf96): unstable past 32 elements, ported so equal keys come out in the game's order
namespace MsvcSort {
template <class T, class L> void med3(T *a, T *b, T *c, L &less) {
    if (less(*b, *a)) std::swap(*b, *a);
    if (less(*c, *b)) std::swap(*c, *b);
    if (less(*b, *a)) std::swap(*b, *a);
}
template <class T, class L> void median(T *f, T *m, T *l, L &less) {  // l: the last element
    if (40 < l - f) {
        ptrdiff_t s = (l - f + 1) / 8;
        med3(f, f + s, f + 2 * s, less), med3(m - s, m, m + s, less), med3(l - 2 * s, l - s, l, less), med3(f + s, m, l - s, less);
    } else med3(f, m, l, less);
}
template <class T, class L> std::pair<T *, T *> partition(T *first, T *last, L &less) {
    T *mid = first + (last - first) / 2;
    median(first, mid, last - 1, less);
    T *pf = mid, *pl = pf + 1;
    while (first < pf && !less(*(pf - 1), *pf) && !less(*pf, *(pf - 1))) --pf;
    while (pl < last && !less(*pl, *pf) && !less(*pf, *pl)) ++pl;
    T *gf = pl, *gl = pf;
    for (;;) {
        for (; gf < last; ++gf)
            if (less(*pf, *gf)) {}
            else if (less(*gf, *pf)) break;
            else std::swap(*pl++, *gf);
        for (; first < gl; --gl)
            if (less(*(gl - 1), *pf)) {}
            else if (less(*pf, *(gl - 1))) break;
            else std::swap(*--pf, *(gl - 1));
        if (gl == first && gf == last) return {pf, pl};
        if (gl == first) {
            if (pl != gf) std::swap(*pf, *pl);
            ++pl;
            std::swap(*pf++, *gf++);
        } else if (gf == last) {
            if (--gl != --pf) std::swap(*gl, *pf);
            std::swap(*pf, *--pl);
        } else std::swap(*gf++, *--gl);
    }
}
template <class T, class L> void adjustHeap(T *first, ptrdiff_t hole, ptrdiff_t bottom, T val, L &less) {
    ptrdiff_t top = hole, idx = 2 * hole + 2;
    for (; idx < bottom; idx = 2 * idx + 2) {
        if (less(first[idx], first[idx - 1])) --idx;
        first[hole] = first[idx], hole = idx;
    }
    if (idx == bottom) first[hole] = first[bottom - 1], hole = bottom - 1;
    for (idx = (hole - 1) / 2; top < hole && less(first[idx], val); idx = (hole - 1) / 2) first[hole] = first[idx], hole = idx;
    first[hole] = val;
}
template <class T, class L> void sortRange(T *first, T *last, ptrdiff_t ideal, L &less) {
    ptrdiff_t n;
    while (32 < (n = last - first) && 0 < ideal) {
        std::pair<T *, T *> mid = partition(first, last, less);
        ideal /= 2, ideal += ideal / 2;
        if (mid.first - first < last - mid.second) sortRange(first, mid.first, ideal, less), first = mid.second;
        else sortRange(mid.second, last, ideal, less), last = mid.first;
    }
    if (32 < n) {
        for (ptrdiff_t hole = n / 2; 0 < hole;) --hole, adjustHeap(first, hole, n, first[hole], less);
        for (; 1 < last - first; --last) {
            T val = last[-1];
            last[-1] = *first;
            adjustHeap(first, 0, last - 1 - first, val, less);
        }
    } else if (1 < n)
        for (T *next = first + 1; next != last; ++next) {
            T val = *next;
            if (less(val, *first)) {
                for (T *p = next; p != first; --p) *p = p[-1];
                *first = val;
            } else {
                T *hole = next;
                for (T *prev = hole - 1; less(val, *prev); hole = prev--) *hole = *prev;
                *hole = val;
            }
        }
}
template <class T, class L> void sort(std::vector<T> &v, L less) {
    if (!v.empty()) sortRange(v.data(), v.data() + v.size(), (ptrdiff_t)v.size(), less);
}
}
