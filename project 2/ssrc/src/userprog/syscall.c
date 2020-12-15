#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include  "process.h"
#include <string.h>
#include "devices/shutdown.h"
#define MAXCALL 21
//编号为0~19，为啥给了21（没问题，判断的是>=)
#define MaxFiles 128
//根据guide的建议，防止资源超标做出的限制，虽然并没有在测试中被体现

#define stdin 1
static void syscall_handler (struct intr_frame *);
typedef void (*Syscall_Func)(struct intr_frame*);
Syscall_Func sys_call[MAXCALL];
void close_all_file(struct thread *t);
int close_(struct thread *t,int fd);
void write_(struct intr_frame *f);
void read_(struct intr_frame *f);
void exec_(struct intr_frame *f);
void open_(struct intr_frame *f);
void sys_write(struct intr_frame*);
void sys_exit(struct intr_frame *f);
void exit_error(int status);
void sys_create(struct intr_frame *f);
void sys_open(struct intr_frame *f);
void sys_close(struct intr_frame *f);
void sys_read(struct intr_frame *f);
void sys_filesize(struct intr_frame *f);
void sys_exec(struct intr_frame *f);
void sys_wait(struct intr_frame *f);
void sys_seek(struct intr_frame *f);
void sys_remove(struct intr_frame *f);
void sys_tell(struct intr_frame *f);
void sys_halt(struct intr_frame *f);
//project2中只用实现这13个系统调用，具体参见 lib\user\syscall.h
struct file_node *get_file_by_fd(struct thread *t,int fd);

//用30号中断来响应系统调用，在此函数体中将handler注册为30号中断的handler
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  int i;
  for(i=0;i<MAXCALL;i++)
    sys_call[i]=NULL;

  sys_call[SYS_WRITE]=sys_write;
  sys_call[SYS_EXIT]=sys_exit;
  sys_call[SYS_CREATE]=sys_create;
  sys_call[SYS_OPEN]=sys_open;
  sys_call[SYS_CLOSE]=sys_close;
  sys_call[SYS_READ]=sys_read;
  sys_call[SYS_FILESIZE]=sys_filesize;
  sys_call[SYS_EXEC]=sys_exec;
  sys_call[SYS_WAIT]=sys_wait;
  sys_call[SYS_SEEK]=sys_seek;
  sys_call[SYS_REMOVE]=sys_remove;
  sys_call[SYS_TELL]=sys_tell;
  sys_call[SYS_HALT]=sys_halt;
  lock_init(&file_lock);
}

static void
syscall_handler (struct intr_frame *f /*UNUSED*/)
{
    if(!is_user_vaddr(f->esp))
      exit_error(-1);
   int No=*((int *)(f->esp));

   if(No>=MAXCALL||MAXCALL<0)
   {
   //  错误的系统调用号
       exit_error(-1);
   }
   if(sys_call[No]==NULL)
   {
   //   没有要求的 应该测试中没有
       exit_error(-1);
   }
   sys_call[No](f);
}
//依据文件句柄从进程打开文件表中找到文件指针
struct file_node *get_file_by_fd(struct thread *t,int fd)   
{
    struct list_elem *e;

    for (e = list_begin (&t->file_list); e != list_end (&t->file_list);e=list_next (e))
    {
       struct file_node *now_file_node = list_entry (e, struct file_node, elem);
       if(now_file_node->fd==fd)
            return now_file_node;
    }
    return NULL;
}

//System Call: int write (int fd, const void *buffer, unsigned size)
void sys_write(struct intr_frame *f)  //三个参数
{

    int *esp=(int *)f->esp;
    if(!is_user_vaddr(esp+6))
        exit_error(-1);
    lock_acquire(&file_lock);
    write_(f);
    lock_release(&file_lock);
   

}
void write_(struct intr_frame *f)
{
    int *esp=(int *)f->esp;
    int fd=*(esp+2);              //文件句柄
    char *buffer=(char *)*(esp+6); //输出区位置
    unsigned size=*(esp+3);       //输出大小。
    if(fd==STDOUT_FILENO)        //标准输出
    {
        putbuf (buffer, size);
        f->eax=size;//new
    }
    else                        //文件
    {
        struct thread *cur=thread_current();
        struct file_node *fn=get_file_by_fd(cur,fd); //获取文件指针
        if(fn==NULL)
        {
            f->eax=0;
            return;
        }

        f->eax=file_write(fn->f,buffer,size);

    }
}
//考虑到由内核终止的错误进程，在exception.c中的ifdef userprog中实现的
//System Call: void exit (int status)
void sys_exit(struct intr_frame *f)  //一个参数  正常退出时使用
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    struct thread *cur=thread_current();
    cur->ret=*((int *)f->esp+1);
    f->eax=0;
    thread_exit();

}
void exit_error(int status)      //错误退出，通常status=-1
{
    struct thread *cur=thread_current();
    cur->ret=status;
    thread_exit();
}

//System Call: bool create (const char *file, unsigned initial_size)
void sys_create(struct intr_frame *f)  //两个参数
{

    if(!is_user_vaddr(((int *)f->esp)+5))
      exit_error(-1);
    if((const char *)*((unsigned int *)f->esp+4)==NULL)
        {
            f->eax=-1;
            exit_error(-1);
        }
    bool ret=filesys_create((const char *)*((unsigned int *)f->esp+4),*((unsigned int *)f->esp+5));
    f->eax=ret;
}

//System Call: int open (const char *file)
void sys_open(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    struct thread *cur=thread_current();
    const char *FileName=(char *)*((int *)f->esp+1);
    if(FileName==NULL)
    {
        f->eax=-1;
        exit_error(-1);
    }
    lock_acquire(&file_lock);
    open_(f);
    lock_release(&file_lock);
}
void open_(struct intr_frame *f)
{
    struct thread *cur=thread_current();
    const char *FileName=(char *)*((int *)f->esp+1);
    struct file_node *fn=(struct file_node *)malloc(sizeof(struct file_node));

    fn->f=filesys_open(FileName);
    if(fn->f==NULL|| cur->file_cnt>=MaxFiles)//
        fn->fd=-1;
    else
        fn->fd=++cur->fd_No;

    f->eax=fn->fd;
    if(fn->fd==-1)
        free(fn);

    else
    {
        cur->file_cnt++;
        list_push_back(&cur->file_list,&fn->elem);
    }
}
//System Call: void close (int fd)
void sys_close(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    struct thread *cur=thread_current();
    int fd=*((int *)f->esp+1);
    f->eax=close_(cur,fd);
}
//关闭线程t的fd文件
int close_(struct thread *t,int fd)
{
        struct list_elem *e,*p;
    for (e = list_begin (&t->file_list); e != list_end (&t->file_list);)
    {
       struct file_node *fn = list_entry (e, struct file_node, elem);
            if(fn->fd==fd)
            {
                list_remove(e);
                if(fd==t->fd_No)
                    t->fd_No--;
                t->file_cnt--;
                file_close(fn->f);
               free(fn);

                return 0;
            }
    }        
}
//线程退出时调用，关闭所有打开文件
void close_all_file(struct thread *t)
{
    struct list_elem *e,*p;
     while(!list_empty(&t->file_list))
        {
            struct file_node *fn = list_entry (list_pop_front(&t->file_list), struct file_node, elem);
            file_close(fn->f);
             free(fn);
        }
    t->file_cnt=0;
    return ;   
}
//read和write都是，判断指针地址合法，读取写入地址合法，加锁读写解锁

void sys_read(struct intr_frame *f)
{
    int *esp=(int *)f->esp;
    if(!is_user_vaddr(esp+6))
        exit_error(-1);
    char *buffer=(char *)*(esp+6);
    unsigned size=*(esp+3);
    if(buffer==NULL||!is_user_vaddr(buffer+size))
    {
        f->eax=-1;
        exit_error(-1);
    }
    lock_acquire(&file_lock);
    read_(f);
    lock_release(&file_lock);

}
void read_(struct intr_frame *f)
{
    int *esp=(int *)f->esp;
    int fd=*(esp+2);
    char *buffer=(char *)*(esp+6);
    unsigned size=*(esp+3);
/*
     printf("fd*=%d",(esp+2));
    printf("fd2*=%d",(int *)(f->esp + 4));
    printf("buffer*=%d",(char *)(esp+6));
    printf("buffer2*=%d",(char**)(f->esp + 8));
    printf("size*=%d",*(esp+3));
    printf("size2*=%d",*(unsigned *)(f->esp + 12));   
    */
    struct thread *cur=thread_current();
    struct file_node *fn=NULL;
    unsigned int i;
    if(fd==STDIN_FILENO)               //从标准输入设备读
    {
        for(i=0;i<size;i++)
            buffer[i]=input_getc();

    }
    else                            //从文件读
    {
        fn=get_file_by_fd(cur,fd);         //获取文件指针
        if(fn==NULL)
        {
            f->eax=-1;
            return;
        }
        f->eax=file_read(fn->f,buffer,size);
    }
}


//System Call: pid_t exec (const char *cmd_line)

void sys_exec(struct intr_frame *f)
{
     if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    const char *file=(char*)*((int *)f->esp+1);
    tid_t tid=-1;
    if(file==NULL)
    {
        f->eax=-1;
        return;
    }
    lock_acquire(&file_lock);
    exec_(f);
    lock_release(&file_lock);
}
//这里newfile的设计为了通过rox-child和exec_art这三个测试点
//在execute中，传过去的fielname被改动了 所以要额外创建一个副本用于传递
void exec_(struct intr_frame *f)
{
    const char *file=(char*)*((int *)f->esp+1);
    tid_t tid;
  char *newfile=(char *)malloc(sizeof(char)*(strlen(file)+1));

    memcpy(newfile,file,strlen(file)+1);
    tid=process_execute (newfile);
    struct thread *t=GetThreadFromTid(tid);
    sema_down(&t->sema_load);
    f->eax=t->tid;
    free(newfile);
    sema_up(&t->sema_load);
}
//System Call: int wait (pid_t pid)
//主体功能在process_wait中实现了，这里仅判断等的tid是否合法
void sys_wait(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    tid_t tid=*((int *)f->esp+1);
    if(tid!=-1)
    {
        f->eax=process_wait(tid);
    }
    else
    {
        f->eax=-1;
    }

}
//System Call: int filesize (int fd)
void sys_filesize(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    struct thread *cur=thread_current();
    int fd=*((int *)f->esp+1);
    struct file_node *fn=get_file_by_fd(cur,fd);
    if(fn==NULL)
    {
        f->eax=-1;
        return;
    }
    f->eax=file_length (fn->f);
}
//System Call: void seek (int fd, unsigned position)
void sys_seek(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+5))
      exit_error(-1);

    int fd=*((int *)f->esp+4);
    unsigned int pos=*((unsigned int *)f->esp+5);
    struct file_node *file=get_file_by_fd(thread_current(),fd);
     if(file==NULL||file->f==NULL)
     {
         f->eax=-1;
         return;
     }    
    file_seek (file->f,pos);
}
//System Call: bool remove (const char *file)
void sys_remove(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    char *file_name=(char *)*((int *)f->esp+1);
    f->eax=filesys_remove (file_name);
}
//System Call: unsigned tell (int fd)
void sys_tell(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      exit_error(-1);
    int fd=*((int *)f->esp+1);
     struct file_node *file=get_file_by_fd(thread_current(),fd);
     if(file==NULL||file->f==NULL)
     {
         f->eax=-1;
         return;
     }
    f->eax=file_tell (file->f);
}
void sys_halt(struct intr_frame *f)
{
    shutdown_power_off();
    f->eax=0;

}
