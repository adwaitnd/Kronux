#ifndef _TIMESPEC_H_
#define _TIMESPEC_H_

#include "rk_common.h"

#define timespec_zero(time) { (time).tv_sec = 0;  (time).tv_nsec = 0; }

#define	timespec_add_nsec(result, nanos) do { \
	if (((result).tv_nsec += (nanos)) >= NANOSEC_PER_SEC) { \
		(result).tv_nsec -= NANOSEC_PER_SEC; \
		(result).tv_sec++; \
	} \
} while (0)

#define timespec_add(result, addend) do {  \
	(result).tv_nsec += (addend).tv_nsec; \
	(result).tv_sec += (addend).tv_sec; \
	if ((result).tv_nsec >= NANOSEC_PER_SEC) { \
		(result).tv_nsec -= NANOSEC_PER_SEC; \
		(result).tv_sec++; \
	} \
} while (0)

#define timespec_sub(result, subtrahend) do { \
	if ((result).tv_nsec >= (subtrahend).tv_nsec) { \
		(result).tv_nsec -= (subtrahend).tv_nsec; \
		(result).tv_sec -= (subtrahend).tv_sec; \
	} else { \
		(result).tv_nsec += NANOSEC_PER_SEC; \
		(result).tv_nsec -= (subtrahend).tv_nsec; \
		(result).tv_sec -= (subtrahend).tv_sec + 1; \
	} \
} while (0)

#define timespec_set(time, newtime) do { \
	(time).tv_sec = (newtime).tv_sec; \
	(time).tv_nsec = (newtime).tv_nsec; \
} while (0)

#define timespec_cmp(time1, time2) \
	(((time1).tv_sec < (time2).tv_sec) || \
	 (((time1).tv_sec == (time2).tv_sec) && \
	  ((time1).tv_nsec <= (time2).tv_nsec)))

#define timespec_ge(time1, time2) \
	(((time1).tv_sec > (time2).tv_sec) || \
	 (((time1).tv_sec == (time2).tv_sec) && \
	  ((time1).tv_nsec >= (time2).tv_nsec)))	

#define timespec_gt(time1, time2) \
	(((time1).tv_sec > (time2).tv_sec) || \
	 (((time1).tv_sec == (time2).tv_sec) && \
	  ((time1).tv_nsec > (time2).tv_nsec)))

#define timespec_le(time1, time2) \
	(((time1).tv_sec < (time2).tv_sec) || \
	 (((time1).tv_sec == (time2).tv_sec) && \
	  ((time1).tv_nsec <= (time2).tv_nsec)))

#define timespec_lt(time1, time2) \
	(((time1).tv_sec < (time2).tv_sec) || \
	 (((time1).tv_sec == (time2).tv_sec) && \
	  ((time1).tv_nsec  < (time2).tv_nsec)))

#define timespec_eq(time1,time2) \
	(((time1).tv_sec == (time2).tv_sec) &&	\
	 ((time1).tv_nsec == (time2).tv_nsec))

#define timespec_min(time1,time2) \
	(timespec_le((time1),(time2)) ? (time1) : (time2))	

#define timespec_max(time1,time2) \
	(timespec_ge((time1),(time2)) ? (time1) : (time2))	

#define timespec_ne(time1,time2) \
	(((time1).tv_sec != (time2).tv_sec) || \
	 ((time1).tv_nsec != (time2).tv_nsec))

#define timespec_nonzero(time) \
	((time).tv_nsec || (time).tv_sec)

#ifndef timespec_valid
#define timespec_valid(time) \
	((time).tv_sec >= 0 && \
	 (time).tv_nsec >= 0 && \
	 (time).tv_nsec <= NANOSEC_PER_SEC)
#endif

#define timespec2micro(time) \
	(((time).tv_sec * MICROSEC_PER_SEC) + ((time).tv_nsec / 1000))

#define timespec2nano(time) \
	((((long long)(time).tv_sec) * NANOSEC_PER_SEC) + ((time).tv_nsec))

#ifdef __KERNEL__
	#define nano2timespec(ts,nanos) do {\
		(ts).tv_sec  = div_s64((nanos), NANOSEC_PER_SEC); \
		(ts).tv_nsec = (nanos) - (ts).tv_sec * NANOSEC_PER_SEC; \
	} while (0)
#else
	#define nano2timespec(ts,nanos) do {\
		(ts).tv_sec  = (nanos) / NANOSEC_PER_SEC; \
		(ts).tv_nsec = (nanos) - (ts).tv_sec * NANOSEC_PER_SEC; \
	} while (0)
#endif

#endif /* _TIMESPEC_H_ */
