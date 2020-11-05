# Mission 2

### 一、描述

实现线程的优先级调度、优先级捐赠

具体要求

1.线程请求锁时，若这个锁被别的线程占用，则比较占用者和请求者优先级的高低。如果占用者优先级低于请求者优先级，则将占用者优先级提升到请求者优先级。

2.如果占用锁的线程又被其他线程锁住，则继续依次向前捐赠优先级。

3.在释放锁的时候，恢复之前的优先级

4.如果多个线程同时请求用一个锁，则发生优先级捐赠时，取优先级中的最大值进行捐赠

5.在对处于被捐赠状态的线程进行优先级捐赠的时候，需要先设置该线程的初始优先级。

6.释放锁的时候需要考虑对其他线程的优先级影响，且这个时候可以进行抢占

8.将信号量、条件变量的等待队列实现为优先级队列

### 二、实现步骤

在thread数据结构中添加成员

```c
int base_priority;//记录原始优先级
struct list locks;//记录当前线程占用的锁
struct lock *lock_waiting;//记录当前线程正在等待的锁
```

在lock数据结构中添加成员

```c
struct list_elem;//用于捐赠队列
int max_priority//请求这个锁的线程拥有的最大优先级
```

修改lock_acquire

```c
void
lock_acquire (struct lock *lock)
{
  struct thread *current_thread = thread_current ();//获取当前运行的线程
  struct lock *l;
  enum intr_level old_level;

  ASSERT (lock != NULL);//锁不为空
  ASSERT (!intr_context ());//当前未处于外中断
  ASSERT (!lock_held_by_current_thread (lock));//当前线程未占有这个锁
	
  //如果锁被其他线程占用，并且不是mlfqs设置
  //此处用于向前捐赠优先级
  if (lock->holder != NULL && !thread_mlfqs)
  {
    //当前线程正在等待这个锁
    current_thread->lock_waiting = lock;
	l = lock;
    //锁不为空，且当前线程的优先级高于这个锁的请求者中的最大优先级
    while (l && current_thread->priority > l->max_priority)
    {
      //将锁的请求者中的最大优先级修改为当前线程的优先级
      l->max_priority = current_thread->priority;
      //向这个锁的占用者捐赠优先级
      thread_donate_priority (l->holder);
      //看看这个锁的拥有者是否在等待其他锁
      l = l->holder->lock_waiting;
    }
  }
  
  //进行一次P操作
  sema_down (&lock->semaphore);
	
  //禁用中断
  old_level = intr_disable ();
  
    
  current_thread = thread_current ();
  if (!thread_mlfqs)
  {
    //现在没有等待的锁了
    current_thread->lock_waiting = NULL;
    //当前线程抢到了这个锁，那么这个锁的优先级必然是最大的
    lock->max_priority = current_thread->priority;
    //当前线程获得锁
    thread_hold_the_lock (lock);
  }
  //将锁的占用者设置为当前这个线程
  lock->holder = current_thread;
	
  //恢复中断
  intr_set_level (old_level);
}
```

实现thread_hold_the_lock(struct lock *lock)

```c
void
thread_hold_the_lock(struct lock *lock)
{
  //禁用中断
  enum intr_level old_level = intr_disable ();
  //将这个锁按照优先级插入当前线程占用的锁的队列
  list_insert_ordered (&current_thread->locks, &lock->elem, 									   lock_cmp_priority, NULL);
	
  //如果这个锁的请求者的最大优先级大于当前线程的优先级
  if (lock->max_priority > thread_current ()->priority)
  {
    //发生优先级捐赠
    thread_current ()->priority = lock->max_priority;
    //立即按优先级重新调度
    thread_yield ();
  }
  //恢复中断
  intr_set_level (old_level);
}
```

实现thread_donate_priority(struct thread *t)

```c
/* Donate current priority to thread t. */
void
thread_donate_priority (struct thread *t)
{
  //禁用中断
  enum intr_level old_level = intr_disable ();
  //更新t线程的优先级
  thread_update_priority (t);
  //如果t处于就绪状态
  if (t->status == THREAD_READY)
  {
    //将t移出就绪队列
    list_remove (&t->elem);
    //再将t重新插入
    list_insert_ordered (&ready_list, &t->elem, thread_cmp_priority, NULL);
    //这两步实际上就是在t改变了优先级后重新排了一下序
  }
    
  //恢复中断
  intr_set_level (old_level);
}
```

实现lock_cmp_priority()

```c
 bool
lock_cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	return list_entry (a, struct lock, elem)->max_priority > list_entry (b, 						struct lock, elem)->max_priority;
}
```

lock_release()中添加

```c
if (!thread_mlfqs)
     thread_remove_lock (lock);
```

实现thread_remove_lock()

```c
/* Remove a lock. */
void
thread_remove_lock (struct lock *lock)
{
  enum intr_level old_level = intr_disable ();
  //将lock移出线程当前占有的锁队列中
  list_remove (&lock->elem);
  //更新当前线程的优先级
  thread_update_priority (thread_current ());
  
  intr_set_level (old_level);
}
```

实现thread_update_priority()

```c
/* Update priority. */
void
thread_update_priority (struct thread *t)
{
  enum intr_level old_level = intr_disable ();
  
  int max_priority = t->base_priority;//获取初始优先级
  int lock_priority;//获取锁的请求者中具有的最大优先级
  //线程t占有锁
  if (!list_empty (&t->locks))
  { //对这些锁的按照lock->max_priority排序
    list_sort (&t->locks, lock_cmp_priority, NULL);
    //获取所有lock->max_priority中的最大值
    lock_priority = list_entry (list_front (&t->locks), struct lock, elem)->max_priority;
    //如果这个最大值大于线程t的初始优先级
    if (lock_priority > max_priority)
      //将这个值保存
      max_priority = lock_priority;
  }
  //将t的优先级修改为最大值
  t->priority = max_priority;
  
   intr_set_level (old_level);
}
```

在init_thread()中加入

```c
t->base_priority = priority;
list_init (&t->locks);
t->lock_waiting = NULL;
```

修改thread_set_priority()为

```c
//此函数只在test文件中被直接调用，用于修改线程初始优先级。
void
thread_set_priority (int new_priority)
{
  if (thread_mlfqs)
    return;

  enum intr_level old_level = intr_disable ();

  struct thread *current_thread = thread_current ();//获取当前线程
  int old_priority = current_thread->priority;//保存当前线程的优先级
  current_thread->base_priority = new_priority;//修改当前线程的初始优先级
  //如果这个线程不占有锁或者new>old时
  if (list_empty (&current_thread->locks) || new_priority > old_priority)
  {
    current_thread->priority = new_priority;//更新线程优先级
    thread_yield ();//立即发起调度
  }

  intr_set_level (old_level);
}
```

修改cond_signal()

```c
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
  {
    //对条件变量的等待者队列进行排序
    list_sort (&cond->waiters, cond_sema_cmp_priority, NULL);
    sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
  }
}
```

实现cond_sema_cmp_priority()

```c
/* cond sema comparation function */
bool
cond_sema_cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
  return list_entry(list_front(&sa->semaphore.waiters), struct thread, elem)->priority > list_entry(list_front(&sb->semaphore.waiters), struct thread, elem)->priority;
}
```

修改sema_up()为

```c
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
  {	//对信号量的等待者队列进行排序
    list_sort (&sema->waiters, thread_cmp_priority, NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
  }

  sema->value++;
  thread_yield ();
  intr_set_level (old_level);
}
```

修改sema_down()为

```c
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    { //按优先级插入信号量等待者队列
      list_insert_ordered (&sema->waiters, &thread_current ()->elem, thread_cmp_priority, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}
```

