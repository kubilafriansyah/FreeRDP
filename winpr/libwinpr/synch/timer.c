/**
 * WinPR: Windows Portable Runtime
 * Synchronization Functions
 *
 * Copyright 2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <winpr/crt.h>
#include <winpr/sysinfo.h>

#include <winpr/synch.h>

#ifndef _WIN32
#include <sys/time.h>
#include <signal.h>
#endif

#include "synch.h"

#ifndef _WIN32

#include "../handle/handle.h"

#ifdef WITH_POSIX_TIMER

static BOOL g_WaitableTimerSignalHandlerInstalled = FALSE;

void WaitableTimerSignalHandler(int signum, siginfo_t* siginfo, void* arg)
{
	WINPR_TIMER* timer = siginfo->si_value.sival_ptr;

	if (!timer || (signum != SIGALRM))
		return;

	if (timer->pfnCompletionRoutine)
	{
		timer->pfnCompletionRoutine(timer->lpArgToCompletionRoutine, 0, 0);

		if (timer->lPeriod)
		{
			timer->timeout.it_interval.tv_sec = (timer->lPeriod / 1000); /* seconds */
			timer->timeout.it_interval.tv_nsec = ((timer->lPeriod % 1000) * 1000000); /* nanoseconds */

			if ((timer_settime(timer->tid, 0, &(timer->timeout), NULL)) != 0)
			{
				perror("timer_settime");
			}
		}
	}
}

int InstallWaitableTimerSignalHandler()
{
	if (!g_WaitableTimerSignalHandlerInstalled)
	{
		struct sigaction action;

		sigemptyset(&action.sa_mask);
		sigaddset(&action.sa_mask, SIGALRM);

		action.sa_flags = SA_RESTART | SA_SIGINFO;
		action.sa_sigaction = (void*) &WaitableTimerSignalHandler;

		sigaction(SIGALRM, &action, NULL);

		g_WaitableTimerSignalHandlerInstalled = TRUE;
	}

	return 0;
}

#endif

int InitializeWaitableTimer(WINPR_TIMER* timer)
{
	if (!timer->lpArgToCompletionRoutine)
	{
#ifdef HAVE_TIMERFD_H
		int status;
		
		timer->fd = timerfd_create(CLOCK_MONOTONIC, 0);

		if (timer->fd <= 0)
		{
			free(timer);
			return -1;
		}

		status = fcntl(timer->fd, F_SETFL, O_NONBLOCK);

		if (status)
		{
			close(timer->fd);
			return -1;
		}
#endif
	}
	else
	{
#ifdef WITH_POSIX_TIMER
		struct sigevent sigev;

		InstallWaitableTimerSignalHandler();

		ZeroMemory(&sigev, sizeof(struct sigevent));

		sigev.sigev_notify = SIGEV_SIGNAL;
		sigev.sigev_signo = SIGALRM;
		sigev.sigev_value.sival_ptr = (void*) timer;

		if ((timer_create(CLOCK_MONOTONIC, &sigev, &(timer->tid))) != 0)
		{
			perror("timer_create");
			return -1;
		}
#endif
	}

	timer->bInit = TRUE;

	return 0;
}

/**
 * Waitable Timer
 */

HANDLE CreateWaitableTimerA(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCSTR lpTimerName)
{
	HANDLE handle = NULL;
	WINPR_TIMER* timer;

	timer = (WINPR_TIMER*) malloc(sizeof(WINPR_TIMER));

	if (timer)
	{
		WINPR_HANDLE_SET_TYPE(timer, HANDLE_TYPE_TIMER);
		handle = (HANDLE) timer;

		timer->fd = -1;
		timer->lPeriod = 0;
		timer->bManualReset = bManualReset;
		timer->pfnCompletionRoutine = NULL;
		timer->lpArgToCompletionRoutine = NULL;
		timer->bInit = FALSE;
	}

	return handle;
}

HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCWSTR lpTimerName)
{
	return NULL;
}

HANDLE CreateWaitableTimerExA(LPSECURITY_ATTRIBUTES lpTimerAttributes, LPCSTR lpTimerName, DWORD dwFlags, DWORD dwDesiredAccess)
{
	BOOL bManualReset;

	bManualReset = (dwFlags & CREATE_WAITABLE_TIMER_MANUAL_RESET) ? TRUE : FALSE;

	return CreateWaitableTimerA(lpTimerAttributes, bManualReset, lpTimerName);
}

HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES lpTimerAttributes, LPCWSTR lpTimerName, DWORD dwFlags, DWORD dwDesiredAccess)
{
	return NULL;
}

BOOL SetWaitableTimer(HANDLE hTimer, const LARGE_INTEGER* lpDueTime, LONG lPeriod,
		PTIMERAPCROUTINE pfnCompletionRoutine, LPVOID lpArgToCompletionRoutine, BOOL fResume)
{
	ULONG Type;
	PVOID Object;
	int status = 0;
	WINPR_TIMER* timer;
	LONGLONG seconds = 0;
	LONGLONG nanoseconds = 0;

	if (!winpr_Handle_GetInfo(hTimer, &Type, &Object))
		return FALSE;

	if (Type != HANDLE_TYPE_TIMER)
		return FALSE;

	if (!lpDueTime)
		return FALSE;

	if (lPeriod < 0)
		return FALSE;

	timer = (WINPR_TIMER*) Object;

	timer->lPeriod = lPeriod; /* milliseconds */
	timer->pfnCompletionRoutine = pfnCompletionRoutine;
	timer->lpArgToCompletionRoutine = lpArgToCompletionRoutine;

	if (!timer->bInit)
	{
		if (InitializeWaitableTimer(timer) < 0)
			return FALSE;
	}

#ifdef WITH_POSIX_TIMER
	
	ZeroMemory(&(timer->timeout), sizeof(struct itimerspec));

	if (lpDueTime->QuadPart < 0)
	{
		LONGLONG due = lpDueTime->QuadPart * (-1);

		/* due time is in 100 nanosecond intervals */

		seconds = (due / 10000000);
		nanoseconds = ((due % 10000000) * 100);
	}
	else if (lpDueTime->QuadPart == 0)
	{
		seconds = nanoseconds = 0;
	}
	else
	{
		printf("SetWaitableTimer: implement absolute time\n");
		return FALSE;
	}

	if (lPeriod > 0)
	{
		timer->timeout.it_interval.tv_sec = (lPeriod / 1000); /* seconds */
		timer->timeout.it_interval.tv_nsec = ((lPeriod % 1000) * 1000000); /* nanoseconds */
	}

	if (lpDueTime->QuadPart != 0)
	{
		timer->timeout.it_value.tv_sec = seconds; /* seconds */
		timer->timeout.it_value.tv_nsec = nanoseconds; /* nanoseconds */
	}
	else
	{
		timer->timeout.it_value.tv_sec = timer->timeout.it_interval.tv_sec; /* seconds */
		timer->timeout.it_value.tv_nsec = timer->timeout.it_interval.tv_nsec; /* nanoseconds */
	}

	if (!timer->pfnCompletionRoutine)
	{
#ifdef HAVE_TIMERFD_H
		status = timerfd_settime(timer->fd, 0, &(timer->timeout), NULL);

		if (status)
		{
			printf("SetWaitableTimer timerfd_settime failure: %d\n", status);
			return FALSE;
		}
#endif
	}
	else
	{
		if ((timer_settime(timer->tid, 0, &(timer->timeout), NULL)) != 0)
		{
			perror("timer_settime");
			return FALSE;
		}
	}

#endif

	return TRUE;
}

BOOL SetWaitableTimerEx(HANDLE hTimer, const LARGE_INTEGER* lpDueTime, LONG lPeriod,
		PTIMERAPCROUTINE pfnCompletionRoutine, LPVOID lpArgToCompletionRoutine, PREASON_CONTEXT WakeContext, ULONG TolerableDelay)
{
	ULONG Type;
	PVOID Object;
	WINPR_TIMER* timer;

	if (!winpr_Handle_GetInfo(hTimer, &Type, &Object))
		return FALSE;

	if (Type == HANDLE_TYPE_TIMER)
	{
		timer = (WINPR_TIMER*) Object;
		return TRUE;
	}

	return TRUE;
}

HANDLE OpenWaitableTimerA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpTimerName)
{
	return NULL;
}

HANDLE OpenWaitableTimerW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpTimerName)
{
	return NULL;
}

BOOL CancelWaitableTimer(HANDLE hTimer)
{
	return TRUE;
}

/**
 * Timer-Queue Timer
 */

/**
 * Design, Performance, and Optimization of Timer Strategies for Real-time ORBs:
 * http://www.cs.wustl.edu/~schmidt/Timer_Queue.html
 */

static void timespec_add_ms(struct timespec* tspec, UINT32 ms)
{
	UINT64 ns = tspec->tv_nsec + (ms * 1000000);
	tspec->tv_sec += (ns / 1000000000);
	tspec->tv_nsec = (ns % 1000000000);
}

static void timespec_gettimeofday(struct timespec* tspec)
{
	struct timeval tval;
	gettimeofday(&tval, NULL);
	tspec->tv_sec = tval.tv_sec;
	tspec->tv_nsec = tval.tv_usec * 1000;
}

static int timespec_compare(const struct timespec* tspec1, const struct timespec* tspec2)
{
	if (tspec1->tv_sec < tspec2->tv_sec)
		return -1;

	if (tspec1->tv_sec > tspec2->tv_sec)
		return 1;

	return tspec1->tv_nsec - tspec2->tv_nsec;
}

static void timespec_copy(struct timespec* dst, struct timespec* src)
{
	dst->tv_sec = src->tv_sec;
	dst->tv_nsec = src->tv_nsec;
}

void InsertTimerQueueTimer(WINPR_TIMER_QUEUE_TIMER** pHead, WINPR_TIMER_QUEUE_TIMER* timer)
{
	WINPR_TIMER_QUEUE_TIMER* node;

	if (!(*pHead))
	{
		*pHead = timer;
		return;
	}

	node = *pHead;

	while (node->next)
	{
		if (timespec_compare(&(timer->ExpirationTime), &(node->ExpirationTime)) < 0)
			break;

		node = node->next;
	}

	if (node->next)
		timer->next = node->next->next;

	node->next = timer;
}

int FireExpiredTimerQueueTimers(WINPR_TIMER_QUEUE* timerQueue)
{
	struct timespec CurrentTime;
	WINPR_TIMER_QUEUE_TIMER* node;

	if (!timerQueue->head)
		return 0;

	timespec_gettimeofday(&CurrentTime);

	node = timerQueue->head;

	while (node)
	{
		if (timespec_compare(&CurrentTime, &(node->ExpirationTime)) >= 0)
		{
			node->Callback(node->Parameter, TRUE);
			node->FireCount++;

			if (node->Period)
			{
				timespec_add_ms(&(node->ExpirationTime), node->Period);

				timerQueue->head = node->next;
				node->next = NULL;

				InsertTimerQueueTimer(&(timerQueue->head), node);
				node = timerQueue->head;
			}
		}
		else
		{
			break;
		}
	}

	return 0;
}

static void* TimerQueueThread(void* arg)
{
	int status;
	struct timespec timeout;
	WINPR_TIMER_QUEUE* timerQueue = (WINPR_TIMER_QUEUE*) arg;

	while (1)
	{
		pthread_mutex_lock(&(timerQueue->cond_mutex));

		if (!timerQueue->head)
		{
			timespec_gettimeofday(&timeout);
			timespec_add_ms(&timeout, 20);
		}
		else
		{
			timespec_copy(&timeout, &(timerQueue->head->ExpirationTime));
		}

		status = pthread_cond_timedwait(&(timerQueue->cond), &(timerQueue->cond_mutex), &timeout);

		FireExpiredTimerQueueTimers(timerQueue);

		pthread_mutex_unlock(&(timerQueue->cond_mutex));
	}

	return NULL;
}

int StartTimerQueueThread(WINPR_TIMER_QUEUE* timerQueue)
{
	pthread_cond_init(&(timerQueue->cond), NULL);
	pthread_mutex_init(&(timerQueue->cond_mutex), NULL);

	pthread_mutex_init(&(timerQueue->mutex), NULL);

	pthread_attr_init(&(timerQueue->attr));
	timerQueue->param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	pthread_attr_setschedparam(&(timerQueue->attr), &(timerQueue->param));
	pthread_attr_setschedpolicy(&(timerQueue->attr), SCHED_FIFO);
	pthread_create(&(timerQueue->thread), &(timerQueue->attr), TimerQueueThread, timerQueue);

	return 0;
}

HANDLE CreateTimerQueue(void)
{
	HANDLE handle = NULL;
	WINPR_TIMER_QUEUE* timerQueue;

	timerQueue = (WINPR_TIMER_QUEUE*) malloc(sizeof(WINPR_TIMER_QUEUE));

	if (timerQueue)
	{
		WINPR_HANDLE_SET_TYPE(timerQueue, HANDLE_TYPE_TIMER_QUEUE);
		handle = (HANDLE) timerQueue;

		StartTimerQueueThread(timerQueue);
	}

	return handle;
}

BOOL DeleteTimerQueueEx(HANDLE TimerQueue, HANDLE CompletionEvent)
{
	WINPR_TIMER_QUEUE* timerQueue;

	if (!TimerQueue)
		return FALSE;

	timerQueue = (WINPR_TIMER_QUEUE*) TimerQueue;

	pthread_cond_destroy(&(timerQueue->cond));
	pthread_mutex_destroy(&(timerQueue->cond_mutex));

	pthread_mutex_destroy(&(timerQueue->mutex));

	pthread_attr_destroy(&(timerQueue->attr));

	free(timerQueue);

	return TRUE;
}

BOOL DeleteTimerQueue(HANDLE TimerQueue)
{
	return DeleteTimerQueueEx(TimerQueue, NULL);
}

BOOL CreateTimerQueueTimer(PHANDLE phNewTimer, HANDLE TimerQueue,
		WAITORTIMERCALLBACK Callback, PVOID Parameter, DWORD DueTime, DWORD Period, ULONG Flags)
{
	WINPR_TIMER_QUEUE* timerQueue;
	WINPR_TIMER_QUEUE_TIMER* timer;

	timerQueue = (WINPR_TIMER_QUEUE*) TimerQueue;
	timer = (WINPR_TIMER_QUEUE_TIMER*) malloc(sizeof(WINPR_TIMER_QUEUE_TIMER));

	if (!timer || !TimerQueue)
		return FALSE;

	WINPR_HANDLE_SET_TYPE(timer, HANDLE_TYPE_TIMER_QUEUE_TIMER);
	*((UINT_PTR*) phNewTimer) = (UINT_PTR) (HANDLE) timer;

	timespec_gettimeofday(&(timer->StartTime));
	timespec_add_ms(&(timer->StartTime), DueTime);
	timespec_copy(&(timer->ExpirationTime), &(timer->StartTime));

	timer->Flags = Flags;
	timer->DueTime = DueTime;
	timer->Period = Period;
	timer->Callback = Callback;
	timer->Parameter = Parameter;
	
	timer->timerQueue = (WINPR_TIMER_QUEUE*) TimerQueue;

	timer->FireCount = 0;

	InsertTimerQueueTimer(&(timerQueue->head), timer);

	return TRUE;
}

BOOL ChangeTimerQueueTimer(HANDLE TimerQueue, HANDLE Timer, ULONG DueTime, ULONG Period)
{
	WINPR_TIMER_QUEUE* timerQueue;
	WINPR_TIMER_QUEUE_TIMER* timer;

	if (!TimerQueue || !Timer)
		return FALSE;

	timerQueue = (WINPR_TIMER_QUEUE*) TimerQueue;
	timer = (WINPR_TIMER_QUEUE_TIMER*) Timer;

	return TRUE;
}

BOOL DeleteTimerQueueTimer(HANDLE TimerQueue, HANDLE Timer, HANDLE CompletionEvent)
{
	WINPR_TIMER_QUEUE* timerQueue;
	WINPR_TIMER_QUEUE_TIMER* timer;

	if (!TimerQueue || !Timer)
		return FALSE;

	timerQueue = (WINPR_TIMER_QUEUE*) TimerQueue;
	timer = (WINPR_TIMER_QUEUE_TIMER*) Timer;

	free(timer);

	return TRUE;
}

#endif
