#include "src_740/msi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MsiCache::MsiCache(const MsiCacheParams& params) 
: CoherentCacheBase(params) {}

bool MsiCache::isHit(long addr) {
    return (state == MiState::Modified || state == MiState::Shared) && tag == addr;
}

void MsiCache::allocate(long addr) {
    tag = addr;
    dirty = false; // TODO: Do I even need the dirty bit?
}

void MsiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    // your implementation here. See MiCache for reference.
    blocked = true; // stop accepting new reqs from CPU until this one is done

    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);

    if (cacheHit) {
        assert(state == MiState::Modified || state == MiState::Shared);
        
        pkt->makeResponse();


        if (isRead) {
            DPRINTF(CCache, "Mi[%d] M read hit %#x\n\n", cacheId, addr);
            // set response data to cached value. This will be returned to CPU.
            pkt->setData(&data);

        } else {
            DPRINTF(CCache, "Mi[%d] M write hit %#x\n\n", cacheId, addr);
            // Case: State == Shared
            if (state == MiState::Shared) {
                // evict block, cause writeback if dirty
                evict();
                state == MiState::Modified; // Upgrade to M
            }
            // TODO: Upgrade?
            // Case: State == Modified
            dirty = true;
            // this cache already has the line in M, so must be exclusive, no need to send to snoop bus.
            // writeback cache: no need to send to memory, just update cache data using packet data.
            data = *pkt->getPtr<unsigned char>();

        }
        // return the response packet to CPU
        sendCpuResp(pkt);

        // start accepting new requests
        blocked = false;
    }
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