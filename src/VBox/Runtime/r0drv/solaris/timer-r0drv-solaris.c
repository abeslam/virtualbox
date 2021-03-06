/* $Id: timer-r0drv-solaris.c 53347 2014-11-18 16:09:52Z vboxsync $ */
/** @file
 * IPRT - Timer, Ring-0 Driver, Solaris.
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/timer.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/spinlock.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include "internal/magics.h"

#define SOL_TIMER_ANY_CPU       (-1)

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Single-CPU timer handle.
 */
typedef struct RTR0SINGLETIMERSOL
{
    /** Cyclic handler. */
    cyc_handler_t           hHandler;
    /** Cyclic time and interval representation. */
    cyc_time_t              hFireTime;
    /** Timer ticks. */
    uint64_t                u64Tick;
} RTR0SINGLETIMERSOL;
typedef RTR0SINGLETIMERSOL *PRTR0SINGLETIMERSOL;

/**
 * Omni-CPU timer handle.
 */
typedef struct RTR0OMNITIMERSOL
{
    /** Absolute timestamp of when the timer should fire next. */
    uint64_t                u64When;
    /** Array of timer ticks per CPU. Reinitialized when a CPU is online'd. */
    uint64_t               *au64Ticks;
} RTR0OMNITIMERSOL;
typedef RTR0OMNITIMERSOL *PRTR0OMNITIMERSOL;

/**
 * The internal representation of a Solaris timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Flag indicating that the timer is suspended. */
    uint8_t volatile        fSuspended;
    /** Whether the timer must run on all CPUs or not. */
    uint8_t                 fAllCpu;
    /** Whether the timer must run on a specific CPU or not. */
    uint8_t                 fSpecificCpu;
    /** The CPU it must run on if fSpecificCpu is set. */
    uint8_t                 iCpu;
    /** The nano second interval for repeating timers. */
    uint64_t                interval;
    /** Cyclic timer Id. */
    cyclic_id_t             hCyclicId;
    /** @todo Make this a union unless we intend to support omni<=>single timers
     *        conversions. */
    /** Single-CPU timer handle. */
    PRTR0SINGLETIMERSOL     pSingleTimer;
    /** Omni-CPU timer handle. */
    PRTR0OMNITIMERSOL       pOmniTimer;
    /** The user callback. */
    PFNRTTIMER              pfnTimer;
    /** The argument for the user callback. */
    void                   *pvUser;
} RTTIMER;


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Validates that the timer is valid. */
#define RTTIMER_ASSERT_VALID_RET(pTimer) \
    do \
    { \
        AssertPtrReturn(pTimer, VERR_INVALID_HANDLE); \
        AssertMsgReturn((pTimer)->u32Magic == RTTIMER_MAGIC, ("pTimer=%p u32Magic=%x expected %x\n", (pTimer), (pTimer)->u32Magic, RTTIMER_MAGIC), \
            VERR_INVALID_HANDLE); \
    } while (0)


/**
 * Callback wrapper for specific timers if they happened to have been fired on
 * the wrong CPU. See rtTimerSolCallbackWrapper().
 *
 * @param   idCpu       The CPU this is fired on.
 * @param   pvUser1     Opaque pointer to the timer.
 * @param   pvUser2     Not used, NULL.
 */
static void rtTimerSolMpCallbackWrapper(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PRTTIMER pTimer = (PRTTIMER)pvUser1;
    AssertPtrReturnVoid(pTimer);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(pTimer->iCpu == RTMpCpuId());    /* ASSUMES: index == cpuid */
    Assert(pTimer->pSingleTimer);
    NOREF(pvUser2);

    /* Make sure one-shots do not fire another time. */
    Assert(   !pTimer->fSuspended
           || pTimer->interval != 0);

    /* For one-shot specific timers, allow RTTimer to restart them. */
    if (pTimer->interval == 0)
        pTimer->fSuspended = true;

    uint64_t u64Tick = ++pTimer->pSingleTimer->u64Tick;
    pTimer->pfnTimer(pTimer, pTimer->pvUser, u64Tick);
}


/**
 * Callback wrapper for Omni-CPU and single-CPU timers.
 *
 * @param    pvArg              Opaque pointer to the timer.
 *
 * @remarks This will be executed in interrupt context but only at the specified
 *          level i.e. CY_LOCK_LEVEL in our case. We -CANNOT- call into the
 *          cyclic subsystem here, neither should pfnTimer().
 */
static void rtTimerSolCallbackWrapper(void *pvArg)
{
    PRTTIMER pTimer = (PRTTIMER)pvArg;
    AssertPtrReturnVoid(pTimer);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    if (pTimer->pSingleTimer)
    {
        /* Make sure one-shots do not fire another time. */
        Assert(   !pTimer->fSuspended
               || pTimer->interval != 0);

        /* For specific timers, we might fire on the wrong CPU between cyclic_add() and cyclic_bind().
           Redirect these shots to the right CPU as we are temporarily rebinding to the right CPU. */
        if (   pTimer->fSpecificCpu
            && pTimer->iCpu != RTMpCpuId())          /* ASSUMES: index == cpuid */
        {
            RTMpOnSpecific(pTimer->iCpu, rtTimerSolMpCallbackWrapper, pTimer, NULL);
            return;
        }

        /* For one-shot any-cpu timers, allow RTTimer to restart them. */
        if (pTimer->interval == 0)
            pTimer->fSuspended = true;

        uint64_t u64Tick = ++pTimer->pSingleTimer->u64Tick;
        pTimer->pfnTimer(pTimer, pTimer->pvUser, u64Tick);
    }
    else if (pTimer->pOmniTimer)
    {
        uint64_t u64Tick = ++pTimer->pOmniTimer->au64Ticks[CPU->cpu_id];
        pTimer->pfnTimer(pTimer, pTimer->pvUser, u64Tick);
    }
}


/**
 * Omni-CPU cyclic online event. This is called before the omni cycle begins to
 * fire on the specified CPU.
 *
 * @param    pvArg              Opaque pointer to the timer.
 * @param    pCpu               Pointer to the CPU on which it will fire.
 * @param    pCyclicHandler     Pointer to a cyclic handler to add to the CPU
 *                              specified in @a pCpu.
 * @param    pCyclicTime        Pointer to the cyclic time and interval object.
 *
 * @remarks We -CANNOT- call back into the cyclic subsystem here, we can however
 *          block (sleep).
 */
static void rtTimerSolOmniCpuOnline(void *pvArg, cpu_t *pCpu, cyc_handler_t *pCyclicHandler, cyc_time_t *pCyclicTime)
{
    PRTTIMER pTimer = (PRTTIMER)pvArg;
    AssertPtrReturnVoid(pTimer);
    AssertPtrReturnVoid(pCpu);
    AssertPtrReturnVoid(pCyclicHandler);
    AssertPtrReturnVoid(pCyclicTime);

    pTimer->pOmniTimer->au64Ticks[pCpu->cpu_id] = 0;
    pCyclicHandler->cyh_func  = rtTimerSolCallbackWrapper;
    pCyclicHandler->cyh_arg   = pTimer;
    pCyclicHandler->cyh_level = CY_LOCK_LEVEL;

    uint64_t u64Now = RTTimeSystemNanoTS();
    if (pTimer->pOmniTimer->u64When < u64Now)
        pCyclicTime->cyt_when = u64Now + pTimer->interval / 2;
    else
        pCyclicTime->cyt_when = pTimer->pOmniTimer->u64When;

    pCyclicTime->cyt_interval = pTimer->interval;
}


RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    RT_ASSERT_PREEMPTIBLE();
    *ppTimer = NULL;

    /*
     * Validate flags.
     */
    if (!RTTIMER_FLAGS_ARE_VALID(fFlags))
        return VERR_INVALID_PARAMETER;

    if (    (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        &&  (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL
        &&  !RTMpIsCpuPossible(RTMpCpuIdFromSetIndex(fFlags & RTTIMER_FLAGS_CPU_MASK)))
        return VERR_CPU_NOT_FOUND;

    /* One-shot omni timers are not supported by the cyclic system. */
    if (   (fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL
        && u64NanoInterval == 0)
    {
        return VERR_NOT_SUPPORTED;
    }

    /*
     * Allocate and initialize the timer handle.
     */
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (!pTimer)
        return VERR_NO_MEMORY;

    pTimer->u32Magic = RTTIMER_MAGIC;
    pTimer->fSuspended = true;
    if ((fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL)
    {
        pTimer->fAllCpu = true;
        pTimer->fSpecificCpu = false;
        pTimer->iCpu = 255;
    }
    else if (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
    {
        pTimer->fAllCpu = false;
        pTimer->fSpecificCpu = true;
        pTimer->iCpu = fFlags & RTTIMER_FLAGS_CPU_MASK; /* ASSUMES: index == cpuid */
    }
    else
    {
        pTimer->fAllCpu = false;
        pTimer->fSpecificCpu = false;
        pTimer->iCpu = 255;
    }
    pTimer->interval = u64NanoInterval;
    pTimer->pfnTimer = pfnTimer;
    pTimer->pvUser = pvUser;
    pTimer->pSingleTimer = NULL;
    pTimer->pOmniTimer = NULL;
    pTimer->hCyclicId = CYCLIC_NONE;

    *ppTimer = pTimer;
    return VINF_SUCCESS;
}


RTDECL(int) RTTimerDestroy(PRTTIMER pTimer)
{
    if (pTimer == NULL)
        return VINF_SUCCESS;
    RTTIMER_ASSERT_VALID_RET(pTimer);
    RT_ASSERT_INTS_ON();

    /*
     * Free the associated resources.
     */
    RTTimerStop(pTimer);

    /** @remarks Do -not- call this function from a timer callback,
     *           cyclic_remove() will deadlock the system. */
    if (pTimer->pSingleTimer)
    {
        mutex_enter(&cpu_lock);
        cyclic_remove(pTimer->hCyclicId);
        mutex_exit(&cpu_lock);
        RTMemFree(pTimer->pSingleTimer);
    }
    else if (pTimer->pOmniTimer)
    {
        mutex_enter(&cpu_lock);
        cyclic_remove(pTimer->hCyclicId);
        mutex_exit(&cpu_lock);
        RTMemFree(pTimer->pOmniTimer->au64Ticks);
        RTMemFree(pTimer->pOmniTimer);
    }

    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);
    RTMemFree(pTimer);
    return VINF_SUCCESS;
}


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    RTTIMER_ASSERT_VALID_RET(pTimer);
    RT_ASSERT_INTS_ON();

    if (!pTimer->fSuspended)
        return VERR_TIMER_ACTIVE;

    pTimer->fSuspended = false;
    if (pTimer->fAllCpu)
    {
        Assert(pTimer->interval);
        PRTR0OMNITIMERSOL pOmniTimer = RTMemAllocZ(sizeof(RTR0OMNITIMERSOL));
        if (RT_UNLIKELY(!pOmniTimer))
            return VERR_NO_MEMORY;

        pOmniTimer->au64Ticks = RTMemAllocZ(RTMpGetCount() * sizeof(uint64_t));
        if (RT_UNLIKELY(!pOmniTimer->au64Ticks))
        {
            RTMemFree(pOmniTimer);
            return VERR_NO_MEMORY;
        }

        /*
         * Setup omni (all CPU) timer. The Omni-CPU online event will fire
         * and from there we setup periodic timers per CPU.
         */
        pTimer->pOmniTimer = pOmniTimer;
        pOmniTimer->u64When     = pTimer->interval + RTTimeSystemNanoTS();

        cyc_omni_handler_t hOmni;
        hOmni.cyo_online        = rtTimerSolOmniCpuOnline;
        hOmni.cyo_offline       = NULL;
        hOmni.cyo_arg           = pTimer;

        mutex_enter(&cpu_lock);
        pTimer->hCyclicId = cyclic_add_omni(&hOmni);
        mutex_exit(&cpu_lock);
    }
    else
    {
        int iCpu = SOL_TIMER_ANY_CPU;
        if (pTimer->fSpecificCpu)
        {
            iCpu = pTimer->iCpu;
            if (!RTMpIsCpuOnline(iCpu))    /* ASSUMES: index == cpuid */
                return VERR_CPU_OFFLINE;
        }

        PRTR0SINGLETIMERSOL pSingleTimer = RTMemAllocZ(sizeof(RTR0SINGLETIMERSOL));
        if (RT_UNLIKELY(!pSingleTimer))
            return VERR_NO_MEMORY;

        pTimer->pSingleTimer = pSingleTimer;
        pSingleTimer->hHandler.cyh_func  = rtTimerSolCallbackWrapper;
        pSingleTimer->hHandler.cyh_arg   = pTimer;
        pSingleTimer->hHandler.cyh_level = CY_LOCK_LEVEL;

        mutex_enter(&cpu_lock);
        if (   iCpu != SOL_TIMER_ANY_CPU
            && !cpu_is_online(cpu[iCpu]))
        {
            mutex_exit(&cpu_lock);
            RTMemFree(pSingleTimer);
            pTimer->pSingleTimer = NULL;
            return VERR_CPU_OFFLINE;
        }

        pSingleTimer->hFireTime.cyt_when = u64First + RTTimeSystemNanoTS();
        if (pTimer->interval == 0)
        {
            /*
             * cylic_add() comment: "The caller is responsible for assuring that cyt_when + cyt_interval <= INT64_MAX"
             * but it contradicts itself because cyclic_reprogram() updates only the interval and accepts CY_INFINITY as
             * a valid, special value. See cyclic_fire().
             */
            pSingleTimer->hFireTime.cyt_interval = CY_INFINITY;
        }
        else
            pSingleTimer->hFireTime.cyt_interval = pTimer->interval;

        pTimer->hCyclicId = cyclic_add(&pSingleTimer->hHandler, &pSingleTimer->hFireTime);
        if (iCpu != SOL_TIMER_ANY_CPU)
            cyclic_bind(pTimer->hCyclicId, cpu[iCpu], NULL /* cpupart */);

        mutex_exit(&cpu_lock);
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    RTTIMER_ASSERT_VALID_RET(pTimer);
    RT_ASSERT_INTS_ON();

    if (pTimer->fSuspended)
        return VERR_TIMER_SUSPENDED;

    /*
     * Solaris does not allow removing cyclics from the timer callback but it does allow
     * reprogramming the cyclic. Reprogram such that it never expires.
     */
    int rc = RTTimerChangeInterval(pTimer, CY_INFINITY);
    if (RT_SUCCESS(rc))
        pTimer->fSuspended = true;
    return rc;
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    RTTIMER_ASSERT_VALID_RET(pTimer);

    if (pTimer->hCyclicId != CYCLIC_NONE)
    {
        uint64_t uNanoTS = RTTimeSystemNanoTS();
        if (   u64NanoInterval >= CY_INFINITY
            || uNanoTS >= CY_INFINITY - u64NanoInterval)
            cyclic_reprogram(pTimer->hCyclicId, CY_INFINITY);
        else
            cyclic_reprogram(pTimer->hCyclicId, (hrtime_t)(u64NanoInterval + uNanoTS));

        return VINF_SUCCESS;
    }
    return VERR_INVALID_STATE;
}


RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
    return nsec_per_tick;
}


RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    return VERR_NOT_SUPPORTED;
}


RTDECL(bool) RTTimerCanDoHighResolution(void)
{
    /** @todo return true; - when missing bits have been implemented and tested*/
    return false;
}

