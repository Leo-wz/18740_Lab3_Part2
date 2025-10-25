#include "src_740/msi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MsiCache::MsiCache(const MsiCacheParams& params) 
: CoherentCacheBase(params) {}

bool MsiCache::isHit(long addr) {
    return (state == MsiState::Modified || state == MsiState::Shared) && tag == addr;
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
        assert(state == MsiState::Modified || state == MsiState::Shared);
        
        pkt->makeResponse();


        if (isRead) {
            DPRINTF(CCache, "Mi[%d] M read hit %#x\n\n", cacheId, addr);
            // set response data to cached value. This will be returned to CPU.
            pkt->setData(&data);

        } else {
            DPRINTF(CCache, "Mi[%d] M write hit %#x\n\n", cacheId, addr);
            // Case: State == Shared
            if (state == MsiState::Shared) {
                // evict block, cause writeback if dirty
                evict();
                state == MsiState::Modified; // Upgrade to M
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
    bool isRead = requestPacket->isRead();
    if (isRead) {
        bus->sendMemReq(requestPacket, true);
    } else {
        bus->sendMemReq(requestPacket, false);
    }
}

void MsiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] mem resp: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache for reference.
    // In MSI, mem req can hit in both S and M
    assert(!isHit(pkt->getAddr()));

    // since this happened on miss, evict old block
    // Potentially sends a writeback to memory.
    evict();

    // allocate new
    allocate(pkt->getAddr());

    bool isRead = pkt->isRead();
    if (isRead) {
        state = MiState::Shared;
        data = *pkt->getPtr<unsigned char>();
        DPRINTF(CCache, "Mi[%d] got data %d from read\n\n", cacheId, data);
    } else {
        state = MiState::Modified;
        // do not read data from a write response packet. Use stored value.
        DPRINTF(CCache, "Mi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
        data = dataToWrite;

        // update dirty bit
        dirty = true;
    }
    // the CPU has been waiting for a response. Send it this one.
    sendCpuResp(pkt);
    
    // release the bus so other caches can use it
    bus->release(cacheId);

    // start accepting new requests
    blocked = false;
}

void MsiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] snoop: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache for reference.
    bool snoopHit = isHit(pkt->getAddr());

    // cache snooped a request on the shared bus. Update internal state if needed.
    // only need to care about snoop hit on M
    if (snoopHit) {
        assert((state == MsiState::Modified || state == MsiState::Shared));
        DPRINTF(CCache, "Mi[%d] snoop hit! invalidate\n\n", cacheId);

        // evict block, cause writeback if dirty
        evict();

        // invalidate
        state = MiState::Invalid;
    } else {
        DPRINTF(CCache, "Mi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}