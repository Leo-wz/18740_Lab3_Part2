#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MesiCache::MesiCache(const MesiCacheParams& params) 
: CoherentCacheBase(params) {}

bool MesiCache::isHit(long addr) {
    return (state == MesiState::Modified 
         || state == MesiState::Shared 
         || state == MesiState::Exclusive) 
         && tag == addr;
}

void MesiCache::allocate(long addr) {
    tag = addr;
    dirty = false; // TODO: Do I even need the dirty bit?
}

void MesiCache::evict() {
    // If line is dirty, write back dirty data.
    if (dirty) {
        dirty = false;
        state = MesiState::Invalid;
        bus->sendWriteback(cacheId, tag, data);
        DPRINTF(CCache, "Mesi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
    }
}

void MesiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
    blocked = true; // stop accepting new reqs from CPU until this one is done
    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);
    if (cacheHit) {
        assert(state != MesiState::Invalid);
        if (isRead) {
            DPRINTF(CCache, "Mesi[%d] M read hit %#x\n\n", cacheId, addr);
            pkt->makeResponse();
            pkt->setData(&data);
            sendCpuResp(pkt);
            blocked = false;
        } else { // Is write
            DPRINTF(CCache, "Mesi[%d] M write hit %#x\n\n", cacheId, addr);
            dirty = true;
            if (state == MesiState::Modified) {
                data = *pkt->getPtr<unsigned char>();
                pkt->makeResponse();
                // return the response packet to CPU
                sendCpuResp(pkt);
                // start accepting new requests
                blocked = false;
            } else if (state == MesiState::Shared) {
                requestPacket = pkt;
                dataToWrite = *pkt->getPtr<unsigned char>();
                bus->request(cacheId); // Invalidate other cache
                state = MesiState::Modified; // Upgrade to M
            } else if (state == MesiState::Exclusive) {
                data = *pkt->getPtr<unsigned char>();
                pkt->makeResponse();
                // return the response packet to CPU
                sendCpuResp(pkt);
                // start accepting new requests
                state = MesiState::Modified; // Change to M
                blocked = false;
            }
        }
    } else { // Cache miss
        DPRINTF(CCache, "Mesi[%d] cache miss %#x\n\n", cacheId, addr);
        requestPacket = pkt;
        if (pkt->isWrite()) {
            dataToWrite = *pkt->getPtr<unsigned char>();
        }
        // request bus access
        // this will lead to handleCoherentBusGrant() being called eventually
        bus->request(cacheId);
    }
}


void MesiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Mesi[%d] bus granted\n\n", cacheId);
    // your implementation here. See MiCache/MsiCache for reference.
    bool isRead = requestPacket->isRead();
    if (isRead) {
        bus->sendMemReq(requestPacket, true);
    }
    else {
        bus->sendMemReq(requestPacket, false);
    }
}

void MesiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
    // allocate new
    allocate(pkt->getAddr());
    bool isRead = pkt->isRead();
    
    if (isRead) {
        if (isShared) {
            state = MesiState::Shared;
        } else {
            state = MesiState::Exclusive;
        }
        data = *pkt->getPtr<unsigned char>();
        DPRINTF(CCache, "Mesi[%d] got data %d from read\n\n", cacheId, data);
    }
    else {
        DPRINTF(CCache, "Mesi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
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

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] snoop: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
    bool snoopHit = isHit(pkt->getAddr());

    if (snoopHit) {
        DPRINTF(CCache, "Mesi[%d] snoop hit!\n\n", cacheId);
        if (state == MesiState::Modified) {
            evict();
            if (pkt->isRead()) {
                state = MesiState::Shared;
            } else {
                state = MesiState::Invalid;
            }
        }
        else if (state == MesiState::Shared && !pkt->isRead()) {
            evict();
            // invalidate
            state = MesiState::Invalid;
        } 
        else if (state == MesiState::Exclusive) {
            if (pkt->isRead()) {
                state = MesiState::Shared;
            }
            else {
                evict();
                state = MesiState::Invalid;
            }
        }

    } else {
        DPRINTF(CCache, "Mesi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}