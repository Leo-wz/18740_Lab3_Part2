#include "src_740/msi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MsiCache::MsiCache(const MsiCacheParams& params) 
: CoherentCacheBase(params) {}

bool MsiCache::isHit(long addr) {
    if (state == MsiState::Modified && tag == addr)
    || (state == MsiState::Shared && tag == addr) {
        return true;
    } else return false;
}

void MsiCache::allocate(long addr) {
    tag = addr;
    dirty = false;
}

void MsiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    // your implementation here. See MiCache for reference.
}


void MsiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Msi[%d] bus granted\n\n", cacheId);
    // your implementation here. See MiCache for reference.
}

void MsiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] mem resp: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache for reference.
}

void MsiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] snoop: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache for reference.
}



}