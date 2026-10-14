#pragma once
#include <cstddef>
#include <cstdint>
namespace bvr {
// Steam cOt layout, verified against cOt::draw (RVA 0x8706E0).
// MovieDraw has one bucket in each of five groups. Inspect the render bank,
// not the bank the update thread is filling; never follow a command chain.
struct MovieDrawQueue {
    struct Bucket { const void* head; const void* tail; };
    uint32_t prefix[8];
    uint32_t buckets,writeBank,groups,pool;
    const Bucket* banks[2][6];
    bool Pending()const {
        if(buckets!=1 || groups!=5 || writeBank>1)return false;
        for(unsigned i=0;i<groups;++i) {
            const auto* bucket=banks[(writeBank-1)&1][i];
            if(bucket && bucket->head)return true;
        }
        return false;
    }
};
static_assert(sizeof(void*)==4 && offsetof(MovieDrawQueue,banks)==0x30 && sizeof(MovieDrawQueue)==0x60,
    "Steam x86 OT layout");
}
