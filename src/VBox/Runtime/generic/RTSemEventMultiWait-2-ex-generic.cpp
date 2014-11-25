/* $Id: RTSemEventMultiWait-2-ex-generic.cpp 44529 2013-02-04 15:54:15Z vboxsync $ */
/** @file
 * IPRT - RTSemEventMultiWait, implementation based on RTSemEventMultiWaitEx.
 */

/*
 * Copyright (C) 2010-2011 Oracle Corporation
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
#define LOG_GROUP RTLOGGROUP_SEM
#define RTSEMEVENTMULTI_WITHOUT_REMAPPING
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/err.h>
#include <iprt/assert.h>


RTDECL(int)  RTSemEventMultiWait(RTSEMEVENTMULTI hEventMultiSem, RTMSINTERVAL cMillies)
{
    int rc;
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = RTSemEventMultiWaitEx(hEventMultiSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0);
    else
        rc = RTSemEventMultiWaitEx(hEventMultiSem,
                                   RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                   cMillies);
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemEventMultiWait);

