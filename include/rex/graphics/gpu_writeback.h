#pragma once
// GPU write-back notification: the command processor bumps a sequence number
// after every write it makes to guest memory (scratch-register writeback,
// MEM_WRITE, COND_WRITE, EVENT_WRITE_SHD/EXT/ZPD), so guest code that polls
// such a word (a D3D fence) can block on the host instead of spinning.
//
// Waiter: seq = GpuWritebackSeq(); re-check the guest word; if it has not
// changed, WaitGpuWriteback(seq, timeout). Reading the sequence first means a
// write between the check and the wait is never missed.
#include <chrono>
#include <cstdint>

namespace rex::graphics {

// Command processor thread, after storing to guest memory.
void NotifyGpuWriteback();
// Any thread.
uint32_t GpuWritebackSeq();
// Blocks until the sequence differs from `seen` or `timeout` passes; true if
// it changed. Wakes only when a waiter is registered (no cost to the GPU
// thread otherwise beyond one atomic increment).
bool WaitGpuWriteback(uint32_t seen, std::chrono::microseconds timeout);

}  // namespace rex::graphics
