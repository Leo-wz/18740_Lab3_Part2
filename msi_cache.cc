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

void MsiCache::evict() {
    // TODO: if line is Modified and dirty, write back dirty data.
    // DPRINTF(CCache, "Msi[%d] bus is blocked: %s dirty: %s\n\n", cacheId, blocked, dirty);
    // dirty = false;
    // state = MsiState::Invalid;
    // bus->sendWriteback(cacheId, tag, data);
    // DPRINTF(CCache, "Msi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
        
    dirty = false;
    state = MsiState::Invalid;
    bus->sendWriteback(cacheId, tag, data);
    DPRINTF(CCache, "Msi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
    // if (dirty) {
    //     dirty = false;
    //     state = MsiState::Invalid;
    //     bus->sendWriteback(cacheId, tag, data);
    //     DPRINTF(CCache, "Msi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
    // }
}

void MsiCache::handleCoherentCpuReq(PacketPtr pkt) {
    char *mystate;
    if (state == MsiState::Modified) {mystate = "M";}
    else if (state == MsiState::Shared) {mystate = "S";}
    else if (state == MsiState::Invalid) {mystate = "I";}
    DPRINTF(CCache, "Msi[%d] cpu req: %s state: %s \n\n", cacheId, pkt->print(), mystate);
    blocked = true; // stop accepting new reqs from CPU until this one is done
    // your implementation here. See MiCache for reference.

    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);

    if (cacheHit) {
        assert(state == MsiState::Modified || state == MsiState::Shared);
        
        pkt->makeResponse();


        if (isRead) {
            DPRINTF(CCache, "Msi[%d] read hit %#x\n\n", cacheId, addr);
            // set response data to cached value. This will be returned to CPU.
            pkt->setData(&data);

        } else {
            DPRINTF(CCache, "Msi[%d] write hit %#x\n\n", cacheId, addr);
            // Case: State == Shared
            if (state == MsiState::Shared) {
                // evict block, cause writeback if dirty
                DPRINTF(CCache, "Msi[%d] Right before calling evict\n\n", cacheId);
                evict();
                state = MsiState::Modified; // Upgrade to M
            }
            // TODO: Upgrade?
            // Case: State == Modified
            dirty = true;
            // this cache already has the line in M, so must be exclusive, no need to send to snoop bus.
            // writeback cache: no need to send to memory, just update cache data using packet data.
            data = *pkt->getPtr<unsigned char>();
            DPRINTF(CCache, "Msi[%d] updating to %d in cache\n\n", cacheId, data);

        }
        // return the response packet to CPU
        sendCpuResp(pkt);

        // start accepting new requests
        blocked = false;
    } else {
        DPRINTF(CCache, "Msi[%d] cache miss %#x\n\n", cacheId, addr);
        // cache needs to do a memory request for both read and write
        // so that other caches can snoop it since it is allocating a new block.

        // Only evict/allocate new block AFTER bus is granted and BEFORE bus is released,
        // since a snoop for this addr could come in the middle.

        // In this implementation, the cache only evicts/allocates once memory response is received.

        // store the packet, the data to write and request bus access
        // store data to write since getting data from a DRAM write response packet may not work.
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
    assert(!isHit(pkt->getAddr()));

    // since this happened on miss, evict old block
    // Potentially sends a writeback to memory.
    evict();

    // allocate new
    allocate(pkt->getAddr());

    bool isRead = pkt->isRead();
    if (isRead) {
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
    // your implementation here. See MiCache for reference.
    bool snoopHit = isHit(pkt->getAddr());

    // cache snooped a request on the shared bus. Update internal state if needed.
    // only need to care about snoop hit on M
    if (snoopHit) {
        assert((state == MsiState::Modified || state == MsiState::Shared));
        DPRINTF(CCache, "Msi[%d] snoop hit! \n\n", cacheId);

        // if state is M, or state is M and Write, evict and invalidate
        // DPRINTF(CCache, "Msi[%d] Right before the if \n\n", cacheId);
        if (state == MsiState::Modified || (state == MsiState::Shared && !pkt->isRead())) {
            DPRINTF(CCache, "Msi[%d] Right before calling evict \n\n", cacheId);
            evict();
            // DPRINTF(CCache, "Msi[%d] Right after calling evict \n\n", cacheId);
            // invalidate
            state = MsiState::Invalid;

        } // Otherwise do nothing (state is S and snoop Read)

        
    } else {
        DPRINTF(CCache, "Msi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}