/*
 *
 *  This file contains the implementation of the function described in irq.h
 *
 *  Copyright (c) 2009 embedded brains GmbH.
 *
 *  Copyright (C) 1998, 1999 valette@crf.canon.fr
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */

#include <rtems/system.h>
#include <bsp.h>
#include <bsp/irq.h>
#include <bsp/irq-generic.h>
#include <bsp/vectors.h>
#include <bsp/8xx_immap.h>
#include <bsp/mbx.h>
#include <bsp/commproc.h>

volatile unsigned int ppc_cached_irq_mask;

/*
 * Check if symbolic IRQ name is an SIU IRQ
 */
static inline int is_siu_irq(const rtems_irq_number irqLine)
{
  return (((int) irqLine <= BSP_SIU_IRQ_MAX_OFFSET) &
    ((int) irqLine >= BSP_SIU_IRQ_LOWEST_OFFSET)
   );
}

/*
 * Check if symbolic IRQ name is an CPM IRQ
 */
static inline int is_cpm_irq(const rtems_irq_number irqLine)
{
  return (((int) irqLine <= BSP_CPM_IRQ_MAX_OFFSET) &
    ((int) irqLine >= BSP_CPM_IRQ_LOWEST_OFFSET)
   );
}

/*
 * masks used to mask off the interrupts. For exmaple, for ILVL2, the
 * mask is used to mask off interrupts ILVL2, IRQ3, ILVL3, ... IRQ7
 * and ILVL7.
 *
 */
const static unsigned int SIU_IvectMask[BSP_SIU_IRQ_NUMBER] =
{
     /* IRQ0      ILVL0       IRQ1        ILVL1  */
     0x00000000, 0x80000000, 0xC0000000, 0xE0000000,

     /* IRQ2      ILVL2       IRQ3        ILVL3  */
     0xF0000000, 0xF8000000, 0xFC000000, 0xFE000000,

     /* IRQ4      ILVL4       IRQ5        ILVL5  */
     0xFF000000, 0xFF800000, 0xFFC00000, 0xFFE00000,

     /* IRQ6      ILVL6       IRQ7        ILVL7  */
     0xFFF00000, 0xFFF80000, 0xFFFC0000, 0xFFFE0000
};

int BSP_irq_enable_at_cpm(const rtems_irq_number irqLine)
{
  int cpm_irq_index;

  if (!is_cpm_irq(irqLine))
    return 1;

  cpm_irq_index = ((int) (irqLine) - BSP_CPM_IRQ_LOWEST_OFFSET);
  ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr |= (1 << cpm_irq_index);

  return 0;
}

int BSP_irq_disable_at_cpm(const rtems_irq_number irqLine)
{
  int cpm_irq_index;

  if (!is_cpm_irq(irqLine))
    return 1;

  cpm_irq_index = ((int) (irqLine) - BSP_CPM_IRQ_LOWEST_OFFSET);
  ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr &= ~(1 << cpm_irq_index);

  return 0;
}

int BSP_irq_enabled_at_cpm(const rtems_irq_number irqLine)
{
  int cpm_irq_index;

  if (!is_cpm_irq(irqLine))
    return 0;

  cpm_irq_index = ((int) (irqLine) - BSP_CPM_IRQ_LOWEST_OFFSET);
  return (((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr & (1 << cpm_irq_index));
}

int BSP_irq_enable_at_siu(const rtems_irq_number irqLine)
{
  int siu_irq_index;

  if (!is_siu_irq(irqLine))
    return 1;

  siu_irq_index = ((int) (irqLine) - BSP_SIU_IRQ_LOWEST_OFFSET);
  ppc_cached_irq_mask |= (1 << (31-siu_irq_index));
  ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask = ppc_cached_irq_mask;

  return 0;
}

int BSP_irq_disable_at_siu(const rtems_irq_number irqLine)
{
  int siu_irq_index;

  if (!is_siu_irq(irqLine))
    return 1;

  siu_irq_index = ((int) (irqLine) - BSP_SIU_IRQ_LOWEST_OFFSET);
  ppc_cached_irq_mask &= ~(1 << (31-siu_irq_index));
  ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask = ppc_cached_irq_mask;

  return 0;
}

int BSP_irq_enabled_at_siu       (const rtems_irq_number irqLine)
{
  int siu_irq_index;

  if (!is_siu_irq(irqLine))
    return 0;

  siu_irq_index = ((int) (irqLine) - BSP_SIU_IRQ_LOWEST_OFFSET);
  return ppc_cached_irq_mask & (1 << (31-siu_irq_index));
}

#ifdef DISPATCH_HANDLER_STAT
volatile unsigned int maxLoop = 0;
#endif

/*
 * High level IRQ handler called from shared_raw_irq_code_entry
 */
int C_dispatch_irq_handler (BSP_Exception_frame *frame, unsigned int excNum)
{
  register unsigned int irq;
  register unsigned cpmIntr;                  /* boolean */
  register unsigned oldMask;          /* old siu pic masks */
  register unsigned msr;
  register unsigned new_msr;
#ifdef DISPATCH_HANDLER_STAT
  unsigned loopCounter;
#endif
  /*
   * Handle decrementer interrupt
   */
  if (excNum == ASM_DEC_VECTOR) {
    _CPU_MSR_GET(msr);
    new_msr = msr | MSR_EE;
    _CPU_MSR_SET(new_msr);

    bsp_interrupt_handler_dispatch(BSP_DECREMENTER);

    _CPU_MSR_SET(msr);
    return 0;
  }
  /*
   * Handle external interrupt generated by SIU on PPC core
   */
#ifdef DISPATCH_HANDLER_STAT
  loopCounter = 0;
#endif
  while (1) {
    if ((ppc_cached_irq_mask & ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend) == 0) {
#ifdef DISPATCH_HANDLER_STAT
      if (loopCounter >  maxLoop) maxLoop = loopCounter;
#endif
      break;
    }
    irq = (((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec >> 26);
    cpmIntr = (irq == BSP_CPM_INTERRUPT);
    /*
     * Disable the interrupt of the same and lower priority.
     */
    oldMask = ppc_cached_irq_mask;
    ppc_cached_irq_mask = oldMask & SIU_IvectMask[irq];
    ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask = ppc_cached_irq_mask;
    /*
     * Acknowledge current interrupt. This has no effect on internal level interrupt.
     */
    ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend = (1 << (31 - irq));

    if (cpmIntr)  {
      /*
       * We will reenable the SIU CPM interrupt to allow nesting of CPM interrupt.
       * We must before acknowledege the current irq at CPM level to avoid trigerring
       * the interrupt again.
       */
      /*
       * Acknowledge and get the vector.
       */
      ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_civr = 1;
      irq = (((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_civr >> 11);
      /*
       * transform IRQ to normalized irq table index.
       */
      irq += BSP_CPM_IRQ_LOWEST_OFFSET;
      /*
       * Unmask CPM interrupt at SIU level
       */
      ppc_cached_irq_mask |= (1 << (31 - BSP_CPM_INTERRUPT));
      ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask = ppc_cached_irq_mask;
    }
    /*
     * make sure, that the masking operations in
     * ICTL and MSR are executed in order
     */
    __asm__ volatile("sync":::"memory");

    _CPU_MSR_GET(msr);
    new_msr = msr | MSR_EE;
    _CPU_MSR_SET(new_msr);

    bsp_interrupt_handler_dispatch(irq);

    _CPU_MSR_SET(msr);

    /*
     * make sure, that the masking operations in
     * ICTL and MSR are executed in order
     */
    __asm__ volatile("sync":::"memory");

    if (cpmIntr)  {
      irq -= BSP_CPM_IRQ_LOWEST_OFFSET;
      ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cisr = (1 << irq);
    }
    ppc_cached_irq_mask = oldMask;
    ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask = ppc_cached_irq_mask;
#ifdef DISPATCH_HANDLER_STAT
    ++ loopCounter;
#endif
  }
  return 0;
}

void BSP_SIU_irq_init(void)
{
  /*
   * In theory we should initialize two registers at least :
   * SIMASK, SIEL. SIMASK is reset at 0 value meaning no interrupt. But
   * we should take care that a monitor may have restoreed to another value.
   * If someone find a reasonnable value for SIEL, AND THE NEED TO CHANGE IT
   * please feel free to add it here.
   */
  ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask = 0;
  ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend = 0xffff0000;
  ppc_cached_irq_mask = 0;
  ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_siel = ((volatile immap_t *)IMAP_ADDR)->im_siu_conf.sc_siel;
}

/*
 * Initialize CPM interrupt management
 */
void
BSP_CPM_irq_init(void)
{
  /*
   * Initialize the CPM interrupt controller.
   */
  ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cicr =
#ifdef mpc860
    (CICR_SCD_SCC4 | CICR_SCC_SCC3 | CICR_SCB_SCC2 | CICR_SCA_SCC1) |
#else
    (CICR_SCB_SCC2 | CICR_SCA_SCC1) |
#endif
    ((BSP_CPM_INTERRUPT/2) << 13) | CICR_HP_MASK;
  ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr = 0;

  ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cicr |= CICR_IEN;
}

rtems_status_code bsp_interrupt_vector_enable( rtems_vector_number irqnum)
{
  if (is_cpm_irq(irqnum)) {
    /*
     * Enable interrupt at PIC level
     */
    BSP_irq_enable_at_cpm (irqnum);
  }

  if (is_siu_irq(irqnum)) {
    /*
     * Enable interrupt at SIU level
     */
    BSP_irq_enable_at_siu (irqnum);
  }

  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_vector_disable( rtems_vector_number irqnum)
{
  if (is_cpm_irq(irqnum)) {
    /*
     * disable interrupt at PIC level
     */
    BSP_irq_disable_at_cpm (irqnum);
  }
  if (is_siu_irq(irqnum)) {
    /*
     * disable interrupt at OPENPIC level
     */
    BSP_irq_disable_at_siu (irqnum);
  }

  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_facility_initialize()
{
  /* Install exception handler */
  if (ppc_exc_set_handler( ASM_EXT_VECTOR, C_dispatch_irq_handler)) {
    return RTEMS_IO_ERROR;
  }
  if (ppc_exc_set_handler( ASM_DEC_VECTOR, C_dispatch_irq_handler)) {
    return RTEMS_IO_ERROR;
  }

  /* Initialize the interrupt controller */
  BSP_SIU_irq_init();
  BSP_CPM_irq_init();

  /*
   * Must enable CPM interrupt on SIU. CPM on SIU Interrupt level has already been
   * set up in BSP_CPM_irq_init.
   */
  ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_cicr |= CICR_IEN;
  BSP_irq_enable_at_siu (BSP_CPM_INTERRUPT);

  return RTEMS_SUCCESSFUL;
}

void bsp_interrupt_handler_default( rtems_vector_number vector)
{
  printk( "Spurious interrupt: 0x%08x\n", vector);
}
