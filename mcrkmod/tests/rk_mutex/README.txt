*************************************************

 Descriptions on rk_mutex test applications

*************************************************

0. Common

- Task priorities are determined by the RK cpu reserve scheduling policy. 
  (mcrkmod/cpu_reserve.c::cpu_reserves_scheduling_policy)
  Here it is assumed that Deadline Monotonic (DM, DEADLINE_MONOTONIC) is used.

- Each t_i is represented by five parameters: (C, T, D, R, P), where
  C is the worst-case execution time (WCET)
  T is the period
  D is the deadline of the task
  R is the release offset
  P is the Core index


*************************************************

1. mutex_pip

- Four tasks are created:
  t_0: (10, 1000, 1000,   0, 0)
  t_1: (10, 1000,  920, 110, 0)
  t_2: (10, 1000,  910, 120, 0)
  t_3: (10, 1000,  900, 130, 0)
  Under DM, t_0 has the lowest priority and t_3 has the highest priority.

  All tasks share a resource protected by RK mutex.

- Expected results:
  
  5025(prio:67/70) - 5028(prio:70) - 5027(prio:69) - 5026(prio:68)

  where, 
  the base Linux/RK task priority (BASE_LINUXRK_PRIORITY) is 70,
  PID of t_0 is 5025,
  PID of t_1 is 5026,
  PID of t_2 is 5027, and
  PID of t_3 is 5028.
  The original priority of t_0 (5025) is 67, but it inherits the priority of 
  t_3 (5028) that is waiting on the mutex held by t_0.

*************************************************

2. mutex_pcp1

- Four tasks are created:
  t_0: (10, 1000, 1000,   0, 0)
  t_1: (10, 1000,  920, 110, 0)
  t_2: (10, 1000,  910, 120, 0)
  t_3: (10, 1000,  900, 130, 0)
  Under DM, t_0 has the lowest priority and t_3 has the highest priority.

  All tasks share a resource protected by RK mutex.

- Expected results:
  
  3565(prio:67/70) - 3568(prio:70) - 3567(prio:69) - 3566(prio:68)

  where, 
  the base Linux/RK task priority (BASE_LINUXRK_PRIORITY) is 70,
  PID of t_0 is 3565,
  PID of t_1 is 3566,
  PID of t_2 is 3567, and
  PID of t_3 is 3568.
  The original priority of t_0 (3565) is 67, but it inherits the priority of 
  t_3 (3568) that is waiting on the mutex held by t_0.

*************************************************

3. mutex_pcp2

- Four tasks are created:
  t_0: (10, 1000, 1000,  0, 0)
  t_1: (10, 1000,  920, 10, 0)
  t_2: (10, 1000,  910, 20, 0)
  t_3: (10, 1000,  900, 30, 0)
  Under DM, t_0 has the lowest priority and t_3 has the highest priority.

- There are two resources, r_0 and r_1, each of which is protected by RK mutex.
  r_0: shared by t_0, t_1, t_3
  r_1: shared by t_2

- Expected results:
  
  3672(prio:67/70) - 3675(prio:70) - 3674(prio:69) - 3673(prio:68)

  where, 
  the base Linux/RK task priority (BASE_LINUXRK_PRIORITY) is 70,
  PID of t_0 is 3672,
  PID of t_1 is 3673,
  PID of t_2 is 3674, and
  PID of t_3 is 3675.
  The original priority of t_0 (3672) is 67, but it inherits the priority of 
  t_3 (3674) that is waiting on the mutex held by t_0.

*************************************************

4. mutex_hlp

- Four tasks are created:
  t_0: (10, 1000, 1000,   0, 0)
  t_1: (10, 1000,  920, 110, 0)
  t_2: (10, 1000,  910, 120, 0)
  t_3: (10, 1000,  900, 130, 0)
  Under DM, t_0 has the lowest priority and t_3 has the highest priority.

  All tasks share a resource protected by RK mutex.

- Expected results:
  
  5043(prio:70/70) - 5046(prio:70) - 5045(prio:70) - 5044(prio:70)

  where, 
  the base Linux/RK task priority (BASE_LINUXRK_PRIORITY) is 70,
  PID of t_0 is 5043,
  PID of t_1 is 5044,
  PID of t_2 is 5045, and
  PID of t_3 is 5046.
  The original priority of t_0 (5043) is 67, but it immediately inherits the 
  priority ceiling of the mutex used.

*************************************************

5. mutex_mpcp

- Three tasks are created:
  t_0: (300, 1000, 900, 100, 0)
  t_1: (300, 1000, 910,   0, 0)
  t_2: (300, 1000, 920, 100, 1)
  Under DM, t_0 has the highest priority and t_2 has the lowest priority.

  t_1 and t_2 share a global resource protected by RK mutex.
  t_0 does not use any shared resource.

- Expected results:
  
  7325(prio:69)- 7325(CRIT/prio:72)- 7326(prio:68)- 7326(CRIT/prio:71)- 7324(prio:70)- 7326(prio:68)- 7325(prio:69)

  where, 
  the base Linux/RK task priority (BASE_LINUXRK_PRIORITY) is 70,
  PID of t_0 is 7324,
  PID of t_1 is 7325, and
  PID of t_2 is 7326.

  At first, task t_1 (7325) is released on Core 0. The original priority of t_1
  is 69. When it enters a critical section corresponding to the global resource,
  the priority of t_1 is increased to 72.

  While t_1 is in its critical section, t_0 and t_2 are released on Core 0 and 
  Core 1, respectively. t_2 (7326) starts to execute on Core 1 as soon as it is 
  released. However, t_0 cannot execute until t_1 finishes its critical section.

  When t_1 finishes its critical section, t_2 can enter its crticical section. 
  Also, t_0 (7324) starts to execute by preempting t_1, because the priority of 
  t_0 is greater than the original priority of t_1. When t_0 finishes, t_1 
  continues to execute its normal execution segment.


