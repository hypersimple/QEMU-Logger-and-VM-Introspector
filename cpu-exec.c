/*
 *  emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#include "cpu.h"
#include "disas.h"
#include "tcg.h"
#include "qemu-barrier.h"
#include "qtest.h"

//newnew

#include "cpu.h"
#include "monitor.h"

#include "target-i386/mydef.h"

/*
extern void qmp_pmemsave(int64_t addr, int64_t size, const char *filename,
                Error **errp);
*/
/*
#include "qmp-commands.h"
#include "hmp.h"
#include "qemu-timer.h"
*/

/*
#include "qemu-common.h"
//#include "cpu.h"
//#include "tcg.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "osdep.h"
#include "kvm.h"
#include "hw/xen.h"
#include "qemu-timer.h"
#include "memory.h"
#include "exec-memory.h"
*/

/*
#include "config-host.h"

#include "monitor.h"
#include "sysemu.h"
#include "gdbstub.h"
#include "dma.h"
#include "kvm.h"
#include "qmp-commands.h"

#include "qemu-thread.h"
#include "cpus.h"
#include "qtest.h"
#include "main-loop.h"
*/

/*
#include "qemu-common.h"
#include "sysemu.h"
#include "qmp-commands.h"
#include "ui/qemu-spice.h"
#include "ui/vnc.h"
#include "kvm.h"
#include "arch_init.h"
#include "hw/qdev.h"
#include "blockdev.h"
#include "qemu/qom-qobject.h"
*/

//newend

int tb_invalidated_flag;

//#define CONFIG_DEBUG_EXEC

bool qemu_cpu_has_work(CPUArchState *env)
{
    return cpu_has_work(env);
}

void cpu_loop_exit(CPUArchState *env)
{
    env->current_tb = NULL;
    longjmp(env->jmp_env, 1);
}

/* exit the current TB from a signal handler. The host registers are
   restored in a state compatible with the CPU emulator
 */
#if defined(CONFIG_SOFTMMU)
void cpu_resume_from_signal(CPUArchState *env, void *puc)
{
    /* XXX: restore cpu registers saved in host registers */

    env->exception_index = -1;
    longjmp(env->jmp_env, 1);
}
#endif

/* Execute the code without caching the generated code. An interpreter
   could be used if available. */
static void cpu_exec_nocache(CPUArchState *env, int max_cycles,
                             TranslationBlock *orig_tb)
{
    tcg_target_ulong next_tb;
    TranslationBlock *tb;

    /* Should never happen.
       We only end up here when an existing TB is too long.  */
    if (max_cycles > CF_COUNT_MASK)
        max_cycles = CF_COUNT_MASK;

    tb = tb_gen_code(env, orig_tb->pc, orig_tb->cs_base, orig_tb->flags,
                     max_cycles);
    env->current_tb = tb;
    /* execute the generated code */
    next_tb = tcg_qemu_tb_exec(env, tb->tc_ptr);
    env->current_tb = NULL;

    if ((next_tb & 3) == 2) {
        /* Restore PC.  This may happen if async event occurs before
           the TB starts executing.  */
        cpu_pc_from_tb(env, tb);
    }
    tb_phys_invalidate(tb, -1);
    tb_free(tb);
}

static TranslationBlock *tb_find_slow(CPUArchState *env,
                                      target_ulong pc,
                                      target_ulong cs_base,
                                      uint64_t flags)
{
    TranslationBlock *tb, **ptb1;
    unsigned int h;
    tb_page_addr_t phys_pc, phys_page1;
    target_ulong virt_page2;

    tb_invalidated_flag = 0;

    /* find translated block using physical mappings */
    phys_pc = get_page_addr_code(env, pc);
    phys_page1 = phys_pc & TARGET_PAGE_MASK;
    h = tb_phys_hash_func(phys_pc);
    ptb1 = &tb_phys_hash[h];
    for(;;) {
        tb = *ptb1;
        if (!tb)
            goto not_found;
        if (tb->pc == pc &&
            tb->page_addr[0] == phys_page1 &&
            tb->cs_base == cs_base &&
            tb->flags == flags) {
            /* check next page if needed */
            if (tb->page_addr[1] != -1) {
                tb_page_addr_t phys_page2;

                virt_page2 = (pc & TARGET_PAGE_MASK) +
                    TARGET_PAGE_SIZE;
                phys_page2 = get_page_addr_code(env, virt_page2);
                if (tb->page_addr[1] == phys_page2)
                    goto found;
            } else {
                goto found;
            }
        }
        ptb1 = &tb->phys_hash_next;
    }
 not_found:
   /* if no translated code available, then translate it now */
    tb = tb_gen_code(env, pc, cs_base, flags, 0);

 found:
    /* Move the last found TB to the head of the list */
    if (likely(*ptb1)) {
        *ptb1 = tb->phys_hash_next;
        tb->phys_hash_next = tb_phys_hash[h];
        tb_phys_hash[h] = tb;
    }
    /* we add the TB in the virtual pc hash table */
    env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)] = tb;
    return tb;
}

static inline TranslationBlock *tb_find_fast(CPUArchState *env)
{
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    int flags;

    /* we record a subset of the CPU state. It will
       always be the same before a given translated block
       is executed. */
    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    tb = env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)];
    if (unlikely(!tb || tb->pc != pc || tb->cs_base != cs_base ||
                 tb->flags != flags)) {
        tb = tb_find_slow(env, pc, cs_base, flags);
    }
    return tb;
}

static CPUDebugExcpHandler *debug_excp_handler;

CPUDebugExcpHandler *cpu_set_debug_excp_handler(CPUDebugExcpHandler *handler)
{
    CPUDebugExcpHandler *old_handler = debug_excp_handler;

    debug_excp_handler = handler;
    return old_handler;
}

static void cpu_handle_debug_exception(CPUArchState *env)
{
    CPUWatchpoint *wp;

    if (!env->watchpoint_hit) {
        QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }
    if (debug_excp_handler) {
        debug_excp_handler(env);
    }
}

/* main execution loop */

volatile sig_atomic_t exit_request;





//newnew
/*
void qmp_pmemsave(int64_t addr, int64_t size, const char *filename,
                  Error **errp)
{
    FILE *f;
    uint32_t l;
    uint8_t buf[1024];

    f = fopen(filename, "wb");
    if (!f) {
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }

    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_rw(addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            error_set(errp, QERR_IO_ERROR);
            goto exit;
        }
        addr += l;
        size -= l;
    }

exit:
    fclose(f);
}

*/

/*
void new_pmemsave(filename1)
{
    uint32_t size = 536870912;
    const char *filename = filename1;
    uint64_t addr = 0;
    Error *errp = NULL;

    (*pmemsave_ptr)(addr, size, filename, &errp);
    //hmp_handle_error(mon, &errp);
}
*/


int get_vir_addr_value(CPUArchState *env, int addr)
{
    int format = 'x';
    int count = 1;
    int wsize = 4;
    //target_phys_addr_t addr = 0x80550f50;
    //int addr = 0x80550f50;
    int is_physical = 0;
    
    //CPUArchState *env;
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;

    if (format == 'i') {
        int flags;
        flags = 0;
        //env = mon_get_cpu();
#ifdef TARGET_I386
        if (wsize == 2) {
            flags = 1;
        } else if (wsize == 4) {
            flags = 0;
        } else {
            /* as default we use the current CS size */
            flags = 0;
            if (env) {
#ifdef TARGET_X86_64
                if ((env->efer & MSR_EFER_LMA) &&
                    (env->segs[R_CS].flags & DESC_L_MASK))
                    flags = 2;
                else
#endif
                if (!(env->segs[R_CS].flags & DESC_B_MASK))
                    flags = 1;
            }
        }
#endif
        //monitor_disas(mon, env, addr, count, is_physical, flags);
        return;
    }

    len = wsize * count;
    if (wsize == 1)
        line_size = 8;
    else
        line_size = 16;
    max_digits = 0;

    switch(format) {
    case 'o':
        max_digits = (wsize * 8 + 2) / 3;
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = (wsize * 8 * 10 + 32) / 33;
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        /*
        if (is_physical)
            monitor_printf(mon, TARGET_FMT_plx ":", addr);
        else
            monitor_printf(mon, TARGET_FMT_lx ":", (target_ulong)addr);
        */
        l = len;
        if (l > line_size)
            l = line_size;
        if (is_physical) {
            //cpu_physical_memory_read(addr, buf, l);
            ;
        } else {
            //env = mon_get_cpu();
            if (cpu_memory_rw_debug(env, addr, buf, l, 0) < 0) {
                //monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_raw(buf + i);
                break;
            case 2:
                v = lduw_raw(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_raw(buf + i);
                break;
            case 8:
                v = ldq_raw(buf + i);
                break;
            }
            //monitor_printf(mon, " ");
            switch(format) {
            case 'o':
                //monitor_printf(mon, "%#*" PRIo64, max_digits, v);
                break;
            case 'x':
                //monitor_printf(mon, "0x%0*" PRIx64, max_digits, v);
                break;
            case 'u':
                //monitor_printf(mon, "%*" PRIu64, max_digits, v);
                break;
            case 'd':
                //monitor_printf(mon, "%*" PRId64, max_digits, v);
                break;
            case 'c':
                //monitor_printc(mon, v);
                break;
            }
            i += wsize;
        }
        //monitor_printf(mon, "\n");
        addr += l;
        len -= l;
    }
    
    return v;
}




//newend









//newnew

//CPUX86State pre_env;
CPUArchState pre_env;

int temp1 = 0;
int mycount = 0;


int i=0;
int stopflag1 = 0;
int stopflag2 = 0;
int stopflag3 = 0;

int poffset = 0;

logstruct logbuf[1000000];

log_state_on = 0;

temp2 = 0;

int OP_FLAG = 1;

example_user_t *user, *users=NULL;


//newend


//newnew

#if defined(TARGET_I386)
int cpu_exec(CPUArchState *env, void (*pmemsave_ptr)())
#else
int cpu_exec(CPUArchState *env)
#endif

//newend

{

    int ret, interrupt_request;
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    tcg_target_ulong next_tb;

    if (env->halted) {
        if (!cpu_has_work(env)) {
            return EXCP_HALTED;
        }

        env->halted = 0;
    }

    cpu_single_env = env;

    if (unlikely(exit_request)) {
        env->exit_request = 1;
    }

#if defined(TARGET_I386)
    /* put eflags in CPU temporary format */
    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_SPARC)
#elif defined(TARGET_M68K)
    env->cc_op = CC_OP_FLAGS;
    env->cc_dest = env->sr & 0xf;
    env->cc_x = (env->sr >> 4) & 1;
#elif defined(TARGET_ALPHA)
#elif defined(TARGET_ARM)
#elif defined(TARGET_UNICORE32)
#elif defined(TARGET_PPC)
    env->reserve_addr = -1;
#elif defined(TARGET_LM32)
#elif defined(TARGET_MICROBLAZE)
#elif defined(TARGET_MIPS)
#elif defined(TARGET_SH4)
#elif defined(TARGET_CRIS)
#elif defined(TARGET_S390X)
#elif defined(TARGET_XTENSA)
    /* XXXXX */
#else
#error unsupported target CPU
#endif
    env->exception_index = -1;

    /* prepare setjmp context for exception handling */
    for(;;) {
        if (setjmp(env->jmp_env) == 0) {
            /* if an exception is pending, we execute it here */
            if (env->exception_index >= 0) {
                if (env->exception_index >= EXCP_INTERRUPT) {
                    /* exit request from the cpu execution loop */
                    ret = env->exception_index;
                    if (ret == EXCP_DEBUG) {
                        cpu_handle_debug_exception(env);
                    }
                    break;
                } else {
#if defined(CONFIG_USER_ONLY)
                    /* if user mode only, we simulate a fake exception
                       which will be handled outside the cpu execution
                       loop */
#if defined(TARGET_I386)
                    do_interrupt(env);
#endif
                    ret = env->exception_index;
                    break;
#else
                    do_interrupt(env);
                    env->exception_index = -1;
#endif
                }
            }



//newnew back

            //next_tb = 0x4158e260;
//	next_tb=1;
            next_tb = 0; /* force lookup of first TB */


//newend




            for(;;) {
            
            
            
                //newnew
                //newhere
                #if 0
                
                #if defined(TARGET_I386)
                    //if ( (loglevel & CPU_LOG_EXEC) && (log_state_on == 1) ) {
                    if (loglevel & CPU_LOG_EXEC) {
                    
                        if (temp1 == 0) {    
                            //if (env->eip == 0x40c1f7) {
                            //if (env->eip == 0xc5274c) {
                            //if (env->eip == 0x00c4d1e0 && env->regs[R_ESP]==0x0012aa6c) {
                            if (1) {
                            //7c924ea1
                                //if (env->regs[R_EDI] == 0x156) {
                                    printf("Here we go!\n");
                                    
                                        if (stopflag3 == 0) {
                                            printf("TEMP_FLAG\n");
                                            //(*pmemsave_ptr)(NULL);
                                            stopflag3 = 1;
                                        }
                                        do_interrupt_x86_hardirq(env, 0x88, 1);
	                                    next_tb = 0;
	                                    temp1 = 1;
                                    
                                //}
                            }
                        }

                            //if (env->eip == 0x40c230) {
                            
                            if (tb->pc == 0x013f60b3) {
                            //7c810ea6
                                if (temp2 == 0 && temp1 ==1) {
                                    do_interrupt_x86_hardirq(env, 0x89, 1);
                                    next_tb = 0;
                                    temp2 = 1;
                                    
                                    /*
                                    if (temp2 == 0) {
                                        *env = pre_env;
                                        env->eip = 0x804df107;
                                        //env->eip = 0x7c924ea1;
                                        next_tb = 0;
                                        temp2 = 1;
                                    }
                                    */
                                }
                            }

                        
                    }
                #endif
                
                #endif

                //newend
            
            
            
                interrupt_request = env->interrupt_request;
                if (unlikely(interrupt_request)) {
                    if (unlikely(env->singlestep_enabled & SSTEP_NOIRQ)) {
                        /* Mask out external interrupts for this step. */
                        interrupt_request &= ~CPU_INTERRUPT_SSTEP_MASK;
                    }
                    if (interrupt_request & CPU_INTERRUPT_DEBUG) {
                        env->interrupt_request &= ~CPU_INTERRUPT_DEBUG;
                        env->exception_index = EXCP_DEBUG;
                        cpu_loop_exit(env);
                    }
#if defined(TARGET_ARM) || defined(TARGET_SPARC) || defined(TARGET_MIPS) || \
    defined(TARGET_PPC) || defined(TARGET_ALPHA) || defined(TARGET_CRIS) || \
    defined(TARGET_MICROBLAZE) || defined(TARGET_LM32) || defined(TARGET_UNICORE32)
                    if (interrupt_request & CPU_INTERRUPT_HALT) {
                        env->interrupt_request &= ~CPU_INTERRUPT_HALT;
                        env->halted = 1;
                        env->exception_index = EXCP_HLT;
                        cpu_loop_exit(env);
                    }
#endif
#if defined(TARGET_I386)

                    /*
                    //newnewadd back
                    if (loglevel & CPU_LOG_EXEC) {
                        
                        if (temp1 == 0) {
                            //0xbf803ccc When starting up	        
	                        //tb->pc = 0x78b0379c;
	                        env->eip = 0xbf803ccc;
	                        printf("%08x\n",tb->pc);
	                        next_tb = 0;
	                        //current_pc = 0x1;
	                        //new_eip = 0x1;
	                        //env->regs[R_EAX] = 0x54321;
	                        temp1 = 1;
                        }
                        
                    }
                    //newendadd
                    */
                    
                    /*
                    //newnew back
                    if (loglevel & CPU_LOG_EXEC) {
                        if (temp1 == 0) {
                            CPUArchState *env2;
                            env2 = env;
                            env2->regs[R_EAX] = 0x54321;
                            env2->eip = 0xbf803ccc;
                            do_interrupt_x86_hardirq(env2, 0x88, 1);
                            next_tb = 0;
                            //0xbf803ccc When starting up	        
	                        //tb->pc = 0x78b0379c;
	                        //env->eip = 0xbf803ccc;
	                        //printf("%08x\n",tb->pc);
	                        //current_pc = 0x1;
	                        //new_eip = 0x1;
	                        //env->regs[R_EAX] = 0x54321;
	                        temp1 = 1;
                        }
                        
                    }
                    //newend
                    */





                    if (interrupt_request & CPU_INTERRUPT_INIT) {
                            svm_check_intercept(env, SVM_EXIT_INIT);
                            do_cpu_init(env);
                            env->exception_index = EXCP_HALTED;
                            cpu_loop_exit(env);
                    } else if (interrupt_request & CPU_INTERRUPT_SIPI) {
                            do_cpu_sipi(env);
                    } else if (env->hflags2 & HF2_GIF_MASK) {
                        if ((interrupt_request & CPU_INTERRUPT_SMI) &&
                            !(env->hflags & HF_SMM_MASK)) {
                            svm_check_intercept(env, SVM_EXIT_SMI);
                            env->interrupt_request &= ~CPU_INTERRUPT_SMI;
                            do_smm_enter(env);
                            next_tb = 0;
                        } else if ((interrupt_request & CPU_INTERRUPT_NMI) &&
                                   !(env->hflags2 & HF2_NMI_MASK)) {
                            env->interrupt_request &= ~CPU_INTERRUPT_NMI;
                            env->hflags2 |= HF2_NMI_MASK;
                            do_interrupt_x86_hardirq(env, EXCP02_NMI, 1);
                            next_tb = 0;
                        } else if (interrupt_request & CPU_INTERRUPT_MCE) {
                            env->interrupt_request &= ~CPU_INTERRUPT_MCE;
                            do_interrupt_x86_hardirq(env, EXCP12_MCHK, 0);
                            next_tb = 0;
                        } else if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                                   (((env->hflags2 & HF2_VINTR_MASK) && 
                                     (env->hflags2 & HF2_HIF_MASK)) ||
                                    (!(env->hflags2 & HF2_VINTR_MASK) && 
                                     (env->eflags & IF_MASK && 
                                      !(env->hflags & HF_INHIBIT_IRQ_MASK))))) {
                            int intno;
                            svm_check_intercept(env, SVM_EXIT_INTR);
                            env->interrupt_request &= ~(CPU_INTERRUPT_HARD | CPU_INTERRUPT_VIRQ);
                            intno = cpu_get_pic_interrupt(env);
                            qemu_log_mask(CPU_LOG_TB_IN_ASM, "Servicing hardware INT=0x%02x\n", intno);
                            do_interrupt_x86_hardirq(env, intno, 1);
                            /* ensure that no TB jump will be modified as
                               the program flow was changed */
                            next_tb = 0;
#if !defined(CONFIG_USER_ONLY)
                        } else if ((interrupt_request & CPU_INTERRUPT_VIRQ) &&
                                   (env->eflags & IF_MASK) && 
                                   !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
                            int intno;
                            /* FIXME: this should respect TPR */
                            svm_check_intercept(env, SVM_EXIT_VINTR);
                            intno = ldl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_vector));
                            qemu_log_mask(CPU_LOG_TB_IN_ASM, "Servicing virtual hardware INT=0x%02x\n", intno);
                            do_interrupt_x86_hardirq(env, intno, 1);
                            env->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
                            next_tb = 0;
#endif
                        }
                    }
#elif defined(TARGET_PPC)
                    if ((interrupt_request & CPU_INTERRUPT_RESET)) {
                        cpu_state_reset(env);
                    }
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        ppc_hw_interrupt(env);
                        if (env->pending_interrupts == 0)
                            env->interrupt_request &= ~CPU_INTERRUPT_HARD;
                        next_tb = 0;
                    }
#elif defined(TARGET_LM32)
                    if ((interrupt_request & CPU_INTERRUPT_HARD)
                        && (env->ie & IE_IE)) {
                        env->exception_index = EXCP_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_MICROBLAZE)
                    if ((interrupt_request & CPU_INTERRUPT_HARD)
                        && (env->sregs[SR_MSR] & MSR_IE)
                        && !(env->sregs[SR_MSR] & (MSR_EIP | MSR_BIP))
                        && !(env->iflags & (D_FLAG | IMM_FLAG))) {
                        env->exception_index = EXCP_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_MIPS)
                    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                        cpu_mips_hw_interrupts_pending(env)) {
                        /* Raise it */
                        env->exception_index = EXCP_EXT_INTERRUPT;
                        env->error_code = 0;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_SPARC)
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        if (cpu_interrupts_enabled(env) &&
                            env->interrupt_index > 0) {
                            int pil = env->interrupt_index & 0xf;
                            int type = env->interrupt_index & 0xf0;

                            if (((type == TT_EXTINT) &&
                                  cpu_pil_allowed(env, pil)) ||
                                  type != TT_EXTINT) {
                                env->exception_index = env->interrupt_index;
                                do_interrupt(env);
                                next_tb = 0;
                            }
                        }
                    }
#elif defined(TARGET_ARM)
                    if (interrupt_request & CPU_INTERRUPT_FIQ
                        && !(env->uncached_cpsr & CPSR_F)) {
                        env->exception_index = EXCP_FIQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
                    /* ARMv7-M interrupt return works by loading a magic value
                       into the PC.  On real hardware the load causes the
                       return to occur.  The qemu implementation performs the
                       jump normally, then does the exception return when the
                       CPU tries to execute code at the magic address.
                       This will cause the magic PC value to be pushed to
                       the stack if an interrupt occurred at the wrong time.
                       We avoid this by disabling interrupts when
                       pc contains a magic address.  */
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && ((IS_M(env) && env->regs[15] < 0xfffffff0)
                            || !(env->uncached_cpsr & CPSR_I))) {
                        env->exception_index = EXCP_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_UNICORE32)
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && !(env->uncached_asr & ASR_I)) {
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_SH4)
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_ALPHA)
                    {
                        int idx = -1;
                        /* ??? This hard-codes the OSF/1 interrupt levels.  */
                        switch (env->pal_mode ? 7 : env->ps & PS_INT_MASK) {
                        case 0 ... 3:
                            if (interrupt_request & CPU_INTERRUPT_HARD) {
                                idx = EXCP_DEV_INTERRUPT;
                            }
                            /* FALLTHRU */
                        case 4:
                            if (interrupt_request & CPU_INTERRUPT_TIMER) {
                                idx = EXCP_CLK_INTERRUPT;
                            }
                            /* FALLTHRU */
                        case 5:
                            if (interrupt_request & CPU_INTERRUPT_SMP) {
                                idx = EXCP_SMP_INTERRUPT;
                            }
                            /* FALLTHRU */
                        case 6:
                            if (interrupt_request & CPU_INTERRUPT_MCHK) {
                                idx = EXCP_MCHK;
                            }
                        }
                        if (idx >= 0) {
                            env->exception_index = idx;
                            env->error_code = 0;
                            do_interrupt(env);
                            next_tb = 0;
                        }
                    }
#elif defined(TARGET_CRIS)
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && (env->pregs[PR_CCS] & I_FLAG)
                        && !env->locked_irq) {
                        env->exception_index = EXCP_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
                    if (interrupt_request & CPU_INTERRUPT_NMI
                        && (env->pregs[PR_CCS] & M_FLAG)) {
                        env->exception_index = EXCP_NMI;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_M68K)
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && ((env->sr & SR_I) >> SR_I_SHIFT)
                            < env->pending_level) {
                        /* Real hardware gets the interrupt vector via an
                           IACK cycle at this point.  Current emulated
                           hardware doesn't rely on this, so we
                           provide/save the vector when the interrupt is
                           first signalled.  */
                        env->exception_index = env->pending_vector;
                        do_interrupt_m68k_hardirq(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_S390X) && !defined(CONFIG_USER_ONLY)
                    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                        (env->psw.mask & PSW_MASK_EXT)) {
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_XTENSA)
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        env->exception_index = EXC_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#endif
                   /* Don't use the cached interrupt_request value,
                      do_interrupt may have updated the EXITTB flag. */
                    if (env->interrupt_request & CPU_INTERRUPT_EXITTB) {
                        env->interrupt_request &= ~CPU_INTERRUPT_EXITTB;
                        /* ensure that no TB jump will be modified as
                           the program flow was changed */
                        next_tb = 0;
                    }
                }
                if (unlikely(env->exit_request)) {
                    env->exit_request = 0;
                    env->exception_index = EXCP_INTERRUPT;
                    cpu_loop_exit(env);
                }
#if defined(DEBUG_DISAS) || defined(CONFIG_DEBUG_EXEC)

                //newnew
                //only  when "log exec" is ON, "log cpu"

            if ( (loglevel & CPU_LOG_EXEC) && (log_state_on == 1) ) {
            
                if (qemu_loglevel_mask(CPU_LOG_TB_CPU)) {
                    /* restore flags in standard format */
#if defined(TARGET_I386)
                    env->eflags = env->eflags | cpu_cc_compute_all(env, CC_OP)
                        | (DF & DF_MASK);
                    log_cpu_state(env, X86_DUMP_CCOP);
                    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_M68K)
                    cpu_m68k_flush_flags(env, env->cc_op);
                    env->cc_op = CC_OP_FLAGS;
                    env->sr = (env->sr & 0xffe0)
                              | env->cc_dest | (env->cc_x << 4);
                    log_cpu_state(env, 0);
#else
                    log_cpu_state(env, 0);
#endif
                }
                
            }
                //newend
                
                
#endif /* DEBUG_DISAS || CONFIG_DEBUG_EXEC */
                spin_lock(&tb_lock);
                tb = tb_find_fast(env);
                /* Note: we do it here to avoid a gcc bug on Mac OS X when
                   doing it in tb_find_slow */
                if (tb_invalidated_flag) {
                    /* as some TB could have been invalidated because
                       of memory exceptions while generating the code, we
                       must recompute the hash index here */
                    next_tb = 0;
                    tb_invalidated_flag = 0;
                }
#ifdef CONFIG_DEBUG_EXEC

//newnew
//from qemu-log.h line 34
#if defined(TARGET_I386)



    if (loglevel & CPU_LOG_EXEC) {
            //When GetFileAttributesExW == Y: start logging, Z: stop logging
            
            //if (tb->pc == 0x7c9100a4) {
            if (tb->pc == 0x78b0379c) {
                
                
                // if the function is getfileattributeexw, no need to +6
                int path = get_vir_addr_value(env,get_vir_addr_value(env,env->regs[R_ESP]+0x4)+6);
                
                int theString_addr = get_vir_addr_value(env,env->regs[R_ESP]+0x4);
                
                int ii = 0;
                
                for(ii=0; (0xff & get_vir_addr_value(env,theString_addr+ii)) != 0xa;ii++) {
                    printf("%c",(0xff & (get_vir_addr_value(env,theString_addr+ii))));
                }
                printf("\n");
                
                //newhere
                
                // XXX: the path should be reverse order, little endian
                if (path == 0x3155454e) {
                
                    if (stopflag1 == 0) {
                        log_state_on = 1;

                        printf("STOPPED_1\n");
                        //(*pmemsave_ptr)(NULL);  // Actually, point to qmp_stop(NULL);
                        stopflag1 = 1;
                    }
                    else {
                        stopflag1 = 0;
                    }
                }
                else if (path == 0x3255454e) {
                    if (stopflag2 == 0) { 
                        log_state_on = 0;

                        printf("STOPPED_2\n");
                        (*pmemsave_ptr)(NULL);
                        stopflag2 = 1;
                    }
                    else {
                        stopflag2 = 0;
                    }
                }
               
                
                
            #if 0
            // XXX: the path should be reverse order, little endian
                
                if (path == 0x3255454e) {
                    if (stopflag2 == 0) { 
                        log_state_on = 0;

                        printf("STOPPED_2\n");
                        (*pmemsave_ptr)(NULL);
                        stopflag2 = 1;
                    }
                    else {
                        stopflag2 = 0;
                    }
                }
             #endif  
                
                
            
                
            }   //this brace usually leads to error
            
            #if 0    
                log_state_on = 1;
                
                //printf("STOPPED_1\n");
                //(*pmemsave_ptr)(NULL);  // Actually, point to qmp_stop(NULL);
                
                
                
            }
            
            
            //else if (tb->pc == 0x7c81cafa) {
            //else if (tb->pc == 0x10008720) {
            else if (tb->pc == 0x12121212) {
                log_state_on = 0;
                //printf("STOPPED_2\n");
                //(*pmemsave_ptr)(NULL);  // Actually, point to qmp_stop(NULL);
            }
            
            #endif
            
            if (/*log_state_on == 1*/1) {    //XXX:Changed
             








// single step logging start
#if 0

                    poffset = 0; // this is ensured for single step logging
                
                    logbuf[poffset].type = 123;
                    //logbuf[poffset].eip = tb->pc;
                    logbuf[poffset].eip = env->eip;
                    logbuf[poffset].cr3 = (uint32_t)env->cr[3];
                    logbuf[poffset].eax = (uint32_t)env->regs[R_EAX];
                    logbuf[poffset].ebx = (uint32_t)env->regs[R_EBX];
                    logbuf[poffset].ecx = (uint32_t)env->regs[R_ECX];
                    logbuf[poffset].edx = (uint32_t)env->regs[R_EDX];
                    logbuf[poffset].esi = (uint32_t)env->regs[R_ESI];
                    logbuf[poffset].edi = (uint32_t)env->regs[R_EDI];
                    logbuf[poffset].ebp = (uint32_t)env->regs[R_EBP];
                    logbuf[poffset].esp = (uint32_t)env->regs[R_ESP];
                    logbuf[poffset].fs = (uint32_t)env->segs[4].base;
                    
                    env->eflags = env->eflags | cpu_cc_compute_all(env, CC_OP)
                        | (DF & DF_MASK);
                    logbuf[poffset].eflags = env->eflags;
                    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
                    
                    i = 0; // this is ensured for single step logging
                    
                    fprintf(logfile,"@ EIP=%08x CR3=%08x EAX=%08x EBX=%08x ECX=%08x EDX=%08x ESI=%08x EDI=%08x EBP=%08x ESP=%08x EFLAGS=%08x FS_BASE=%08x\n",
                            logbuf[i].eip,
                            logbuf[i].cr3,
                            logbuf[i].eax,
                            logbuf[i].ebx,
                            logbuf[i].ecx,
                            logbuf[i].edx,
                            logbuf[i].esi,
                            logbuf[i].edi,
                            logbuf[i].ebp,
                            logbuf[i].esp,
                            logbuf[i].eflags,
                            logbuf[i].fs);
   
#endif
// single step logging end





// buffer logging start
//#if 0
                    
                    logbuf[poffset].type = 123;
                    //logbuf[poffset].eip = tb->pc;
                    logbuf[poffset].eip = env->eip;
                    logbuf[poffset].cr3 = (uint32_t)env->cr[3];
                    logbuf[poffset].eax = (uint32_t)env->regs[R_EAX];
                    logbuf[poffset].ebx = (uint32_t)env->regs[R_EBX];
                    logbuf[poffset].ecx = (uint32_t)env->regs[R_ECX];
                    logbuf[poffset].edx = (uint32_t)env->regs[R_EDX];
                    logbuf[poffset].esi = (uint32_t)env->regs[R_ESI];
                    logbuf[poffset].edi = (uint32_t)env->regs[R_EDI];
                    logbuf[poffset].ebp = (uint32_t)env->regs[R_EBP];
                    logbuf[poffset].esp = (uint32_t)env->regs[R_ESP];
                    logbuf[poffset].fs = (uint32_t)env->segs[4].base;
                    
                    env->eflags = env->eflags | cpu_cc_compute_all(env, CC_OP)
                        | (DF & DF_MASK);
                    logbuf[poffset].eflags = env->eflags;
                    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
                    
                    


                    
                    poffset++;
                    
                    if(poffset>=1000000) { 
                    
                        for(i=0;i<=999999;i++)
                        {
                            if(logbuf[i].type == 123) {
                        
                                fprintf(logfile,"@ EIP=%08x CR3=%08x EAX=%08x EBX=%08x ECX=%08x EDX=%08x ESI=%08x EDI=%08x EBP=%08x ESP=%08x EFLAGS=%08x FS_BASE=%08x\n",
                                logbuf[i].eip,
                                logbuf[i].cr3,
                                logbuf[i].eax,
                                logbuf[i].ebx,
                                logbuf[i].ecx,
                                logbuf[i].edx,
                                logbuf[i].esi,
                                logbuf[i].edi,
                                logbuf[i].ebp,
                                logbuf[i].esp,
                                logbuf[i].eflags,
                                logbuf[i].fs
                                );
                                
                                
                                if (OP_FLAG == 1) {
                                    example_user_t *tmp1;
                                    uint64_t eip_key = logbuf[i].eip;
                                    HASH_FIND_INT(users,&eip_key,tmp1);
                                    //printf("%s",tmp1->string);
                                    fprintf(logfile,"%s",tmp1->string);
                                }
                                
                            }
                            
                            else if (logbuf[i].type == 456) {
                                fprintf(logfile,"%6d: v=%02x e=%04x i=%d cpl=%d IP=%04x:" TARGET_FMT_lx " pc=" TARGET_FMT_lx " SP=%04x:" TARGET_FMT_lx "\n",
                                
                                logbuf[i].count,
                                logbuf[i].intno,
                                logbuf[i].error_code,
                                logbuf[i].is_int,
                                logbuf[i].intflag,
                                logbuf[i].intcs,
                                logbuf[i].inteip,
                                logbuf[i].pc,
                                logbuf[i].intss,
                                logbuf[i].intesp
                                );
                            }
                        }
                        poffset = 0;
                    }


//#endif
// buffer logging end










    
            }
        }
#endif

/*
                qemu_log_mask(CPU_LOG_EXEC, "Trace %p [" TARGET_FMT_lx "] %s\n",
                             tb->tc_ptr, tb->pc,
                             lookup_symbol(tb->pc));
*/
                           
//newend


#endif

//newnew

                /* see if we can patch the calling TB. When the TB
                   spans two pages, we cannot safely do a direct
                   jump. */
                /*
                if (next_tb != 0 && tb->page_addr[1] == -1) {
                    tb_add_jump((TranslationBlock *)(next_tb & ~3), next_tb & 3, tb);
                }
                */
                
//newend
                spin_unlock(&tb_lock);

                /* cpu_interrupt might be called while translating the
                   TB, but before it is linked into a potentially
                   infinite loop and becomes env->current_tb. Avoid
                   starting execution if there is a pending interrupt. */
                env->current_tb = tb;
                barrier();
                if (likely(!env->exit_request)) {
                    tc_ptr = tb->tc_ptr;
                    /* execute the generated code */
                    next_tb = tcg_qemu_tb_exec(env, tc_ptr);
                    if ((next_tb & 3) == 2) {
                        /* Instruction counter expired.  */
                        int insns_left;
                        tb = (TranslationBlock *)(next_tb & ~3);
                        /* Restore PC.  */
                        cpu_pc_from_tb(env, tb);
                        insns_left = env->icount_decr.u32;
                        if (env->icount_extra && insns_left >= 0) {
                            /* Refill decrementer and continue execution.  */
                            env->icount_extra += insns_left;
                            if (env->icount_extra > 0xffff) {
                                insns_left = 0xffff;
                            } else {
                                insns_left = env->icount_extra;
                            }
                            env->icount_extra -= insns_left;
                            env->icount_decr.u16.low = insns_left;
                        } else {
                            if (insns_left > 0) {
                                /* Execute remaining instructions.  */
                                cpu_exec_nocache(env, insns_left, tb);
                            }
                            env->exception_index = EXCP_INTERRUPT;
                            next_tb = 0;
                            cpu_loop_exit(env);
                        }
                    }
                }
                env->current_tb = NULL;
                /* reset soft MMU for next block (it can currently
                   only be set by a memory fault) */
            } /* for(;;) */
        } else {
            /* Reload env after longjmp - the compiler may have smashed all
             * local variables as longjmp is marked 'noreturn'. */
            env = cpu_single_env;
        }
    } /* for(;;) */


#if defined(TARGET_I386)
    /* restore flags in standard format */
    env->eflags = env->eflags | cpu_cc_compute_all(env, CC_OP)
        | (DF & DF_MASK);
#elif defined(TARGET_ARM)
    /* XXX: Save/restore host fpu exception state?.  */
#elif defined(TARGET_UNICORE32)
#elif defined(TARGET_SPARC)
#elif defined(TARGET_PPC)
#elif defined(TARGET_LM32)
#elif defined(TARGET_M68K)
    cpu_m68k_flush_flags(env, env->cc_op);
    env->cc_op = CC_OP_FLAGS;
    env->sr = (env->sr & 0xffe0)
              | env->cc_dest | (env->cc_x << 4);
#elif defined(TARGET_MICROBLAZE)
#elif defined(TARGET_MIPS)
#elif defined(TARGET_SH4)
#elif defined(TARGET_ALPHA)
#elif defined(TARGET_CRIS)
#elif defined(TARGET_S390X)
#elif defined(TARGET_XTENSA)
    /* XXXXX */
#else
#error unsupported target CPU
#endif

    /* fail safe : never use cpu_single_env outside cpu_exec() */
    cpu_single_env = NULL;
    return ret;
}
