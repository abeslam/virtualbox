/* $Id: VBoxLA.h 40498 2012-03-16 10:45:05Z vboxsync $ */
/** @file
 * VBoxLA - Location Awareness
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBOXSERVICELA__H
#define __VBOXSERVICELA__H

int                VBoxLAInit    (const VBOXSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall VBoxLAThread  (void *pInstance);
void               VBoxLADestroy (const VBOXSERVICEENV *pEnv, void *pInstance);

#endif /* __VBOXSERVICELA__H */
