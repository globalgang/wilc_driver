/*
 * Atmel WILC3000 802.11 b/g/n and Bluetooth Combo driver
 *
 * Copyright (c) 2015 Atmel Corportation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "atl_error_support.h"
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/version.h>
#include "linux/string.h"
#include "atl_msg_queue.h"

signed int ATL_MsgQueueCreate(struct MsgQueueHandle *pHandle)
{
	spin_lock_init(&pHandle->strCriticalSection);
	sema_init(&pHandle->hSem, 0);

	pHandle->pstrMessageList = NULL;
	pHandle->u32ReceiversCount = 0;
	pHandle->bExiting = false;

	return ATL_SUCCESS;
}
EXPORT_SYMBOL(ATL_MsgQueueCreate);

signed int ATL_MsgQueueDestroy(struct MsgQueueHandle *pHandle)
{
	pHandle->bExiting = true;

	/* Release any waiting receiver thread.*/
	while (pHandle->u32ReceiversCount > 0) {
		up(&pHandle->hSem);
		pHandle->u32ReceiversCount--;
	}

	while (NULL != pHandle->pstrMessageList) {
		struct Message *pstrMessge = pHandle->pstrMessageList->pstrNext;

		kfree(pHandle->pstrMessageList);
		pHandle->pstrMessageList = pstrMessge;
	}

	return ATL_SUCCESS;
}
EXPORT_SYMBOL(ATL_MsgQueueDestroy);

signed int ATL_MsgQueueSend(struct MsgQueueHandle *pHandle,
			    const void *pvSendBuffer,
			    unsigned int u32SendBufferSize)
{
	signed int s32RetStatus = ATL_SUCCESS;
	unsigned long flags;
	struct Message *pstrMessage = NULL;

	if ((NULL == pHandle)
			|| (u32SendBufferSize == 0)
			|| (pvSendBuffer == NULL))
		ATL_ERRORREPORT(s32RetStatus, ATL_INVALID_ARGUMENT);

	if (pHandle->bExiting == true)
		ATL_ERRORREPORT(s32RetStatus, ATL_FAIL);

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);

	/* construct a new message */
	pstrMessage = kmalloc(sizeof(struct Message), GFP_ATOMIC);
	ATL_NULLCHECK(s32RetStatus, pstrMessage);
	pstrMessage->u32Length = u32SendBufferSize;
	pstrMessage->pstrNext = NULL;
	pstrMessage->pvBuffer = kmalloc(u32SendBufferSize, GFP_ATOMIC);
	ATL_NULLCHECK(s32RetStatus, pstrMessage->pvBuffer);
	memcpy(pstrMessage->pvBuffer, pvSendBuffer, u32SendBufferSize);


	/* add it to the message queue */
	if (NULL == pHandle->pstrMessageList) {
		pHandle->pstrMessageList  = pstrMessage;
	} else {
		struct Message *pstrTailMsg = pHandle->pstrMessageList;

		while (NULL != pstrTailMsg->pstrNext)
			pstrTailMsg = pstrTailMsg->pstrNext;
		pstrTailMsg->pstrNext = pstrMessage;
	}

	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	up(&pHandle->hSem);

	ATL_CATCH(s32RetStatus){
		/* error occured, free any allocations */
		if (NULL != pstrMessage) {
			kfree(pstrMessage->pvBuffer);
			kfree(pstrMessage);
		}
	}

	return s32RetStatus;
}
EXPORT_SYMBOL(ATL_MsgQueueSend);
	
signed int ATL_MsgQueueRecv(struct MsgQueueHandle *pHandle,
			   void *pvRecvBuffer, unsigned int u32RecvBufferSize,
			   unsigned int *pu32ReceivedLength)
{

	struct Message *pstrMessage;
	signed int s32RetStatus = ATL_SUCCESS;
	unsigned long flags;

	if ((NULL == pHandle) || (u32RecvBufferSize == 0)
	    || (NULL == pvRecvBuffer) || (NULL == pu32ReceivedLength))
		ATL_ERRORREPORT(s32RetStatus, ATL_INVALID_ARGUMENT);

	if (pHandle->bExiting == true)
		ATL_ERRORREPORT(s32RetStatus, ATL_FAIL);

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);
	pHandle->u32ReceiversCount++;

		/* timed out, just exit without consumeing the message */
	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
	down(&(pHandle->hSem));

	ATL_ERRORCHECK(s32RetStatus);

	if (pHandle->bExiting)
		ATL_ERRORREPORT(s32RetStatus, ATL_FAIL);

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);

	pstrMessage = pHandle->pstrMessageList;
	if (NULL == pstrMessage) {
		spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
		ATL_ERRORREPORT(s32RetStatus, ATL_FAIL);
	}

	/* check buffer size */
	if (u32RecvBufferSize < pstrMessage->u32Length) {
		spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
		up(&pHandle->hSem);
		ATL_ERRORREPORT(s32RetStatus, ATL_BUFFER_OVERFLOW);
	}

	/* consume the message */
	pHandle->u32ReceiversCount--;
	memcpy(pvRecvBuffer, pstrMessage->pvBuffer, pstrMessage->u32Length);
	*pu32ReceivedLength = pstrMessage->u32Length;

	pHandle->pstrMessageList = pstrMessage->pstrNext;

	kfree(pstrMessage->pvBuffer);
	kfree(pstrMessage);


	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	ATL_CATCH(s32RetStatus)
	{
	}
	return s32RetStatus;
}
EXPORT_SYMBOL(ATL_MsgQueueRecv);