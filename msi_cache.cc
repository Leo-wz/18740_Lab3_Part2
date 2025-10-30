#include "src_740/msi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MsiCache::MsiCache(const MsiCacheParams& params) 
: CoherentCacheBase(params) {}

// Can hit in both S and M
bool MsiCache::isHit(long addr) {
    return (state == MsiState::Modified 
         || state == MsiState::Shared) 
         && tag == addr;
}

void MsiCache::allocate(long addr) {
    tag = addr;
    dirty = false;
}

void MsiCache::evict() {
    // If line is dirty, write back dirty data.
    if (dirty) {
        dirty = false;
        state = MsiState::Invalid;
        bus->sendWriteback(cacheId, tag, data);
        DPRINTF(CCache, "Msi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
    }
}

void MsiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    blocked = true; // stop accepting new reqs from CPU until this one is done
    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);
    if (cacheHit) {
        assert(state == MsiState::Modified || state == MsiState::Shared);
        if (isRead) {
            DPRINTF(CCache, "Msi[%d] M read hit %#x\n\n", cacheId, addr);
            pkt->makeResponse();
            // set response data to cached value. This will be returned to CPU.
            pkt->setData(&data);
            sendCpuResp(pkt);
            blocked = false;
        } else {
            DPRINTF(CCache, "Msi[%d] M write hit %#x\n\n", cacheId, addr);
            dirty = true;
            if (state == MsiState::Modified) {
                // this cache already has the line in M, so must be exclusive, no need to send to snoop bus.
                // writeback cache: no need to send to memory, just update cache data using packet data.
                data = *pkt->getPtr<unsigned char>();
                pkt->makeResponse();
                // return the response packet to CPU
                sendCpuResp(pkt);
                // start accepting new requests
                blocked = false;
            } else if (state == MsiState::Shared) {
                requestPacket = pkt;
                dataToWrite = *pkt->getPtr<unsigned char>();
                bus->request(cacheId); // Invalidate other cache
                state = MsiState::Modified; // Upgrade to M
            }
        }
    } else { // Cache miss
        DPRINTF(CCache, "Msi[%d] cache miss %#x\n\n", cacheId, addr);
        requestPacket = pkt;
        if (pkt->isWrite()) {
            dataToWrite = *pkt->getPtr<unsigned char>();
        }

        // request bus access
        // this will lead to handleCoherentBusGrant() being called eventually
        bus->request(cacheId);
    }
}


void MsiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Msi[%d] bus granted\n\n", cacheId);
    bool isRead = requestPacket->isRead();
    if (isRead) {
        bus->sendMemReq(requestPacket, true);
    } else {
        bus->sendMemReq(requestPacket, false);
    }
}

void MsiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] mem resp: %s\n", cacheId, pkt->print());
    // allocate new
    allocate(pkt->getAddr());

    bool isRead = pkt->isRead();
    if (isRead) {
        // Read cause I->S
        state = MsiState::Shared;
        data = *pkt->getPtr<unsigned char>();
        DPRINTF(CCache, "Msi[%d] got data %d from read\n\n", cacheId, data);
    } else {
        state = MsiState::Modified;
        // do not read data from a write response packet. Use stored value.
        DPRINTF(CCache, "Msi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
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
    bool snoopHit = isHit(pkt->getAddr());
    if (snoopHit) {
        assert((state == MsiState::Modified || state == MsiState::Shared));
        DPRINTF(CCache, "Msi[%d] snoop hit! \n\n", cacheId);
        // if state is M, or state is M and Write, evict and invalidate
        if (state == MsiState::Modified || (state == MsiState::Shared && !pkt->isRead())) {
            evict();
            // invalidate
            state = MsiState::Invalid;
        } // Otherwise do nothing (state is S and snoop Read)
    } else {
        DPRINTF(CCache, "Msi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}