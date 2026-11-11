/** SM8850 firmware handoff for myron (Redmi K90 Pro Max).
    SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Library/PlatformSecLib.h>

VOID
PlatformInitialize (VOID)
{
  // ABL hands over a live display and live Qualcomm firmware clients: the MDP
  // scan-out is running, and ADSP/AOP/SOCCP keep their SMMU translations.
  //
  // The inherited KailuaPkg default calls
  //   ArmSmmuDetach (MdpStreams, ARRAY_SIZE (MdpStreams));
  // which walks every context bank and clears TTBR/TCR before the MMU and DXE
  // exist. That tears down the translations of clients UEFI does not own: the
  // panel stops being refreshed and the faulting client can reset the SoC.
  //
  // Keep the stock QSMMUv500 state intact. A peripheral driver must quiesce its
  // own client and take over only its own stream/context bank.
}
