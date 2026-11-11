/*
 * Mu-Silicium BootShim for myron (Redmi K90 Pro Max, SM8850 / Canoe).
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * Two corrections against the stock shim, both about the handoff state ABL
 * leaves behind:
 *
 *  - The ARM64 Linux Image boot protocol requires every interrupt to be masked
 *    on entry. ABL does not always honour that, and the clients it keeps
 *    running (MDP scan-out, timers, WLAN) can deliver an IRQ while the firmware
 *    still has no exception vectors installed. An unhandled IRQ at the wrong
 *    moment resets the SoC, which is why the failure looked time-dependent.
 *
 *  - The FD copy is a plain physical store loop with the MMU and D-cache off.
 *    Complete the stores and drop stale instruction lines before branching into
 *    the copied SEC image: SEC's own cache maintenance runs too late to protect
 *    its entry.
 */
_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  .quad FD_BASE         /* Text Offset */

_StackSize:
  .quad FD_SIZE         /* Image Size */
  .quad 0               /* Flags */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
  /* Mask Dbg/SError/IRQ/FIQ until SEC installs its vector table. */
  msr   daifset, #0xf
  isb
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, =FD_BASE

_Entry:
  dsb   sy
  ic    iallu
  dsb   sy
  isb
  br    x5

_Dead:
  b     _Dead

.text
.align 4

_Payload:
