/* mc4_kuser.c -- the kernel helper pointers in the game's DATA.
 *
 * The runtime points libgcc's literal-pool references to the ARM Linux
 * "kuser" helpers (0xffff0fc0 cmpxchg, 0xffff0fa0 memory barrier) at its own
 * kuser.S (so_fix_kuser_helpers), scanning the executable segments. This
 * library also holds them in writable data: protobuf's atomicops keep
 *   pLinuxKernelCmpxchg = 0xffff0fc0, pLinuxKernelMemoryBarrier = 0xffff0fa0
 * as globals (three copies, six words, in 1.2.3e) and call through them. On
 * Horizon nothing is mapped there: hardware, the first start died in a
 * constructor, google_utils::protobuf::internal::OnShutdown, at pc ffff0fa0.
 * Those words are pointed at kuser.S too, in the staged image, before the
 * module is sealed. Only exact values in writable PT_LOADs are touched. MIT.
 */
#include <stdint.h>

#include "so_util.h"
#include "util.h"

#include "mc4.h"

void dcr_kuser_cmpxchg(void);        /* the runtime's kuser.S */
void dcr_kuser_memory_barrier(void);

int mc4_fix_kuser_data(so_module *m) {
  if (m->state != SO_STAGED) {
    debugPrintf("[kuser] %s is not staged: data pointers not rewritten\n", m->base_name);
    return -1;
  }
  uint8_t *img = (uint8_t *)m->load_base;
  int n = 0;
  for (int i = 0; i < m->phnum; i++) {
    const Elf32_Phdr *ph = &m->phdr[i];
    if (ph->p_type != PT_LOAD || (ph->p_flags & PF_X) || !(ph->p_flags & PF_W))
      continue;
    uint32_t start = (ph->p_vaddr + 3) & ~3u, end = ph->p_vaddr + ph->p_filesz;
    for (uint32_t a = start; a + 4 <= end; a += 4) {
      uint32_t *w = (uint32_t *)(img + a);
      if (*w == 0xffff0fc0u)
        *w = (uint32_t)(uintptr_t)dcr_kuser_cmpxchg;
      else if (*w == 0xffff0fa0u)
        *w = (uint32_t)(uintptr_t)dcr_kuser_memory_barrier;
      else
        continue;
      n++;
    }
  }
  debugPrintf("[kuser] %s: %d kernel helper pointer(s) in data -> kuser.S\n", m->base_name, n);
  return n;
}
