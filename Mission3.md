# Mission 3

在fixed_point.h中实现浮点运算

```c
#ifndef __THREAD_FIXED_POINT_H
#define __THREAD_FIXED_POINT_H

/* Basic definitions of fixed point. */
typedef int fixed_t;
/* 16 LSB used for fractional part. */
#define FP_SHIFT_AMOUNT 16
/* Convert a value to fixed-point value. */
#define FP_CONST(A) ((fixed_t)(A << FP_SHIFT_AMOUNT))
/* Add two fixed-point value. */
#define FP_ADD(A,B) (A + B)
/* Add a fixed-point value A and an int value B. */
#define FP_ADD_MIX(A,B) (A + (B << FP_SHIFT_AMOUNT))
/* Substract two fixed-point value. */
#define FP_SUB(A,B) (A - B)
/* Substract an int value B from a fixed-point value A */
#define FP_SUB_MIX(A,B) (A - (B << FP_SHIFT_AMOUNT))
/* Multiply a fixed-point value A by an int value B. */
#define FP_MULT_MIX(A,B) (A * B)
/* Divide a fixed-point value A by an int value B. */
#define FP_DIV_MIX(A,B) (A / B)
/* Multiply two fixed-point value. */
#define FP_MULT(A,B) ((fixed_t)(((int64_t) A) * B >> FP_SHIFT_AMOUNT))
/* Divide two fixed-point value. */
#define FP_DIV(A,B) ((fixed_t)((((int64_t) A) << FP_SHIFT_AMOUNT) / B))
/* Get integer part of a fixed-point value. */
#define FP_INT_PART(A) (A >> FP_SHIFT_AMOUNT)
/* Get rounded integer of a fixed-point value. */
#define FP_ROUND(A) (A >= 0 ? ((A + (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT) \
        : ((A - (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT))

#endif /* thread/fixed_point.h */
```

理论上来说，这个文件中的内容不需要自己写(是提供好的)，然而实际在pintos中并没有找到这个文件

>  This section explains the basics; we have provided you with a file ‘threads/fixed-point.h’ that implements the techniques described here; you can use those functions in the priority calculations for your scheduler.



thread结构体重添加成员

```c
int nice;//nice值，[-20,20]，值越高优先级越低
fixed_t recent_cpu;//一个估计值，用于描述这个线程最近占用cpu的时间
```

init_thread()中加入

```c
t->nice;
t->recent_cpu=FP_CONST(0);
```

在thread.c添加全局变量

```c
fixed_t load_avg;
```

在thread_start()中初始化

```c
load_avg=FP_CONT(0);
```

完成templates

```c
/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice)
{
  //设置nice值
  thread_current ()->nice = nice;
  //更新优先级
  thread_mlfqs_update_priority (thread_current ());
  //立即重新调度
  thread_yield ();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
//load_avg是一个估计值，用于描述在过去的一分钟内运行的线程数量的平均值
int
thread_get_load_avg (void)
{
  //返回100*load_avg的圆整值
  return FP_ROUND (FP_MULT_MIX (load_avg, 100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
  //返回100*recent_cpu的圆整值
  return FP_ROUND (FP_MULT_MIX (thread_current ()->recent_cpu, 100));
}
```

在timer_interrupt()中添加

```c
if (thread_mlfqs)
  {
    thread_mlfqs_increase_recent_cpu_by_one ();
    /*Because of assumptions made by some of the tests, load avg must be updated exactly when the system tick counter reaches a multiple of a second,that is, when timer_ticks ()% TIMER_FREQ == 0, and not at any other time.*/
    /*Assumptions made by some of the tests require that these recalculations of recent_cpu be made exactly when the system tick counter reaches a multiple of a second, that is, when timer_ticks () % TIMER_FREQ == 0, and not at any other time.*/
    //也就是说，只有满足if的时候才更新load_avg和recent_cpu
    //TIMER_FREQ实际上就是一秒ticks的次数，表达式实际上就代表1秒，即每秒执行一次更新
    if (ticks % TIMER_FREQ == 0)
      thread_mlfqs_update_load_avg_and_recent_cpu ();
     /*......It is also recalculated once every fourth clock tick, for every           thread.*/
    //每4个时钟计算一次priority
    else if (ticks % 4 == 0)
      thread_mlfqs_update_priority (thread_current ());
  }
```

实现thread_mlfqs_increase_recent_cpu_by_one()

```c
/* Increase recent_cpu by 1. */
/*Each time a timer interrupt occurs, recent cpu is incremented by 1 for
the running thread only, unless the idle thread is running.*/
void
thread_mlfqs_increase_recent_cpu_by_one (void)
{
  ASSERT (thread_mlfqs);
  ASSERT (intr_context ());//处于中断中

  struct thread *current_thread = thread_current ();
  //是idle_thread就返回
  if (current_thread == idle_thread)
    return;
  //自增1
  current_thread->recent_cpu = FP_ADD_MIX (current_thread->recent_cpu, 1);
}
```

实现thread_mlfqs_update_load_avg_and_recent_cpu ()

```c
/* Every per second to refresh load_avg and recent_cpu of all threads. */
void
thread_mlfqs_update_load_avg_and_recent_cpu (void)
{
  ASSERT (thread_mlfqs);
  ASSERT (intr_context ());

  size_t ready_threads = list_size (&ready_list);
  /*ready_threads is the number of threads that are either running or ready to run at time of update (not including the idle thread).*/
  //也就是说，ready_threads=running+ready-idle
  if (thread_current () != idle_thread)
    //当前运行的线程不是idle_thread，那就把自己加上
    ready_threads++;
    
  //实现提供的load_avg的计算公式
  load_avg = FP_ADD (FP_DIV_MIX (FP_MULT_MIX (load_avg, 59), 60), FP_DIV_MIX (FP_CONST (ready_threads), 60));

  struct thread *t;
  struct list_elem *e = list_begin (&all_list);
  //对于所有线程都执行
  for (; e != list_end (&all_list); e = list_next (e))
  {
    t = list_entry(e, struct thread, allelem);
    //不是idle_thread
    if (t != idle_thread)
    { 
      //实现提供的recent_cpu的计算公式
      t->recent_cpu = FP_ADD_MIX (FP_MULT (FP_DIV (FP_MULT_MIX (load_avg, 2), FP_ADD_MIX (FP_MULT_MIX (load_avg, 2), 1)), t->recent_cpu), t->nice);
      
      //更新优先级（其实我不懂为啥这里计算完了要更新优先级，说好的每4ticks更新的...）
      thread_mlfqs_update_priority (t);
    }
  }
}
```

实现thread_mlfqs_update_priority (struct thread *t)

```c
/* Update priority. */
//计算t线程的优先级
void
thread_mlfqs_update_priority (struct thread *t)
{
  if (t == idle_thread)
    return;

  ASSERT (thread_mlfqs);
  ASSERT (t != idle_thread);
  //实现提供的priority计算公式
  t->priority = FP_INT_PART (FP_SUB_MIX (FP_SUB (FP_CONST (PRI_MAX), FP_DIV_MIX (t->recent_cpu, 4)), 2 * t->nice));
  
   /* The calculated priority is always adjusted to lie in the valid range PRI_MIN to PRI_MAX.*/
  //防止超界
  t->priority = t->priority < PRI_MIN ? PRI_MIN : t->priority;
  t->priority = t->priority > PRI_MAX ? PRI_MAX : t->priority;
}
```

