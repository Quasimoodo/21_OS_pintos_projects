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
#define MaxFiles 200
//？咋来的

#define stdin 1
static void syscall_handler (struct intr_frame *);
typedef void (*CALL_PROC)(struct intr_frame*);
CALL_PROC pfn[MAXCALL];
void IWrite(struct intr_frame*);
void IExit(struct intr_frame *f);
void ExitStatus(int status);
void ICreate(struct intr_frame *f);
void IOpen(struct intr_frame *f);
void IClose(struct intr_frame *f);
void IRead(struct intr_frame *f);
void IFileSize(struct intr_frame *f);
void IExec(struct intr_frame *f);
void IWait(struct intr_frame *f);
void ISeek(struct intr_frame *f);
void IRemove(struct intr_frame *f);
void ITell(struct intr_frame *f);
void IHalt(struct intr_frame *f);
//project2中只用实现这13个系统调用，具体参见 lib\user\syscall.h
struct file_node *GetFile(struct thread *t,int fd);
//用30号中断来响应系统调用，在此函数体重将handler注册为30号中断的handler
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  int i;
  for(i=0;i<MAXCALL;i++)
    pfn[i]=NULL;

  pfn[SYS_WRITE]=IWrite;
  pfn[SYS_EXIT]=IExit;
  pfn[SYS_CREATE]=ICreate;
  pfn[SYS_OPEN]=IOpen;
  pfn[SYS_CLOSE]=IClose;
  pfn[SYS_READ]=IRead;
  pfn[SYS_FILESIZE]=IFileSize;
  pfn[SYS_EXEC]=IExec;
  pfn[SYS_WAIT]=IWait;
  pfn[SYS_SEEK]=ISeek;
  pfn[SYS_REMOVE]=IRemove;
  pfn[SYS_TELL]=ITell;
  pfn[SYS_HALT]=IHalt;
}

static void
syscall_handler (struct intr_frame *f /*UNUSED*/)
{
    if(!is_user_vaddr(f->esp))
      ExitStatus(-1);
   int No=*((int *)(f->esp));

   if(No>=MAXCALL||MAXCALL<0)
   {
       printf("We don't have this System Call!\n");
       ExitStatus(-1);
   }
   if(pfn[No]==NULL)
   {
       printf("this System Call %d not Implement!\n",No);
       ExitStatus(-1);
   }
   pfn[No](f);
}
//System Call: int write (int fd, const void *buffer, unsigned size)
void IWrite(struct intr_frame *f)  //三个参数
{

    int *esp=(int *)f->esp;
      if(!is_user_vaddr(esp+7))
        ExitStatus(-1);
    int fd=*(esp+2);              //文件句柄
    char *buffer=(char *)*(esp+6); //要输出缓冲
    unsigned size=*(esp+3);       //输出内容大小。

    if(fd==STDOUT_FILENO)        //标准输出设备
    {
        putbuf (buffer, size);
        f->eax=size;//new
    }
    else                        //文件
    {
        struct thread *cur=thread_current();
        struct file_node *fn=GetFile(cur,fd); //获取文件指针
        if(fn==NULL)
        {
            f->eax=0;
            return;
        }

        f->eax=file_write(fn->f,buffer,size);//写文件

    }

}
//考虑到由内核终止的错误进程，实在exception.c中的ifdef userprog中实现的
//System Call: void exit (int status)
void IExit(struct intr_frame *f)  //一个参数  正常退出时使用
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    cur->ret=*((int *)f->esp+1);
    f->eax=0;
    thread_exit();

}
void ExitStatus(int status)      //非正常退出时使用
{
    struct thread *cur=thread_current();
    cur->ret=status;
    thread_exit();
}

//System Call: bool create (const char *file, unsigned initial_size)
void ICreate(struct intr_frame *f)  //两个参数
{

    if(!is_user_vaddr(((int *)f->esp)+6))
      ExitStatus(-1);
    if((const char *)*((unsigned int *)f->esp+4)==NULL)
        {
            f->eax=-1;
            ExitStatus(-1);
        }
    bool ret=filesys_create((const char *)*((unsigned int *)f->esp+4),*((unsigned int *)f->esp+5));
    f->eax=ret;
}


void IOpen(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    const char *FileName=(char *)*((int *)f->esp+1);
    if(FileName==NULL)
    {
        f->eax=-1;
        ExitStatus(-1);
    }
    struct file_node *fn=(struct file_node *)malloc(sizeof(struct file_node));

    fn->f=filesys_open(FileName);
    if(fn->f==NULL|| cur->FileNum>=MaxFiles)//
        fn->fd=-1;
    else
        fn->fd=++cur->maxfd;

    f->eax=fn->fd;
    if(fn->fd==-1)
        free(fn);

    else
    {
        cur->FileNum++;
        list_push_back(&cur->file_list,&fn->elem);
    }
}
void IClose(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    int fd=*((int *)f->esp+1);
    f->eax=CloseFile(cur,fd,false);

}
int CloseFile(struct thread *t,int fd,int bAll)
{
    struct list_elem *e,*p;
    if(bAll)
    {
        while(!list_empty(&t->file_list))
        {
            struct file_node *fn = list_entry (list_pop_front(&t->file_list), struct file_node, elem);
            file_close(fn->f);
             free(fn);
        }
        t->FileNum=0;
        return 0;
    }

  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);)
  {
       struct file_node *fn = list_entry (e, struct file_node, elem);
            if(fn->fd==fd)
            {
                list_remove(e);
                if(fd==t->maxfd)
                    t->maxfd--;
                t->FileNum--;
                file_close(fn->f);
               free(fn);

                return 0;
            }
  }

}

void IRead(struct intr_frame *f)
{
    int *esp=(int *)f->esp;
    if(!is_user_vaddr(esp+6))
        ExitStatus(-1);
    int fd=*(esp+2);
    char *buffer=(char *)*(esp+6);
    unsigned size=*(esp+3);

    if(buffer==NULL||!is_user_vaddr(buffer+size))
    {
        f->eax=-1;
        ExitStatus(-1);
    }

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
        fn=GetFile(cur,fd);         //获取文件指针
        if(fn==NULL)
        {
            f->eax=-1;
            return;
        }
        f->eax=file_read(fn->f,buffer,size);
    }
}

struct file_node *GetFile(struct thread *t,int fd)   //依据文件句柄从进程打开文件表中找到文件指针
{
    struct list_elem *e;

    for (e = list_begin (&t->file_list); e != list_end (&t->file_list);e=list_next (e))
    {
       struct file_node *fn = list_entry (e, struct file_node, elem);
       if(fn->fd==fd)
            return fn;
    }
    return NULL;
}

void IFileSize(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    int fd=*((int *)f->esp+1);
    struct file_node *fn=GetFile(cur,fd);
    if(fn==NULL)
    {
        f->eax=-1;
        return;
    }
    f->eax=file_length (fn->f);
}
void IExec(struct intr_frame *f)
{
     if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    const char *file=(char*)*((int *)f->esp+1);
    tid_t tid=-1;
    if(file==NULL)
    {
        f->eax=-1;
        return;
    }
    char *newfile=(char *)malloc(sizeof(char)*(strlen(file)+1));

    memcpy(newfile,file,strlen(file)+1);
    tid=process_execute (newfile);
    struct thread *t=GetThreadFromTid(tid);
    sema_down(&t->SemaWaitSuccess);
    f->eax=t->tid;
    //t->father->sons++;
    free(newfile);
    sema_up(&t->SemaWaitSuccess);
}

void IWait(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    tid_t tid=*((int *)f->esp+1);
    if(tid!=-1)
    {
        f->eax=process_wait(tid);

    }
    else
        f->eax=-1;
}

void ISeek(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+6))
      ExitStatus(-1);

    int fd=*((int *)f->esp+4);
    unsigned int pos=*((unsigned int *)f->esp+5);
    struct file_node *fl=GetFile(thread_current(),fd);
    file_seek (fl->f,pos);
}

void IRemove(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    char *fl=(char *)*((int *)f->esp+1);
    f->eax=filesys_remove (fl);
}

void ITell(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);
    int fd=*((int *)f->esp+1);
     struct file_node *fl=GetFile(thread_current(),fd);
     if(fl==NULL||fl->f==NULL)
     {
         f->eax=-1;
         return;
     }
    f->eax=file_tell (fl->f);
}
void IHalt(struct intr_frame *f)
{
    shutdown_power_off();
    f->eax=0;

}
