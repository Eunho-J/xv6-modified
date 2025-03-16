# Sync

Sync를 구현하기에 앞서, 기존 xv6의  `sys_write()`의 동작 방식은 `filewrite()`를 호출하여 `writei()`를 통해 disk에 바로 data를 쓰는 형태이다.

과제 요구사항에 맞추어, buffer에 우선 write를 수행하고, buffer가 가득 차거나 `sys_sync()`가 호출되었을 경우에만 `install_trans()`를 수행할 수 있도록 아래의 변경사항들을 적용하였다.



### end_op 에서의 조건부 commit 호출

log가 가득 찼을 경우에만 `commit()`을 호출하고, 이외에는 Disk에 Log 작성까지만 할 수 있도록 `logging_write_log()`와 `logging_write_head()`를 호출하도록 하였다.  이때 disk에 log를 쓰는 것의 overhead를 최소화하기 위해, `logheader`에 `prevlogn` 변수를 추가하여 commit 되지 않고 log에만 쓰여져 있는buffer의 수를 기록하였다.

```c
//log.c
struct logheader {
  int n;
  int prevlogn;				//log가 어디까지 쓰였는지의 기준이 된다.
  int block[LOGSIZE];
};

static void
logging_write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = log.lh.prevlogn; i < log.lh.n; i++) { //이전에 작성한 log에 이어서 작성한다.
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
logging_write_log(void) //logging_write_head와 마찬가지로 이전 작성한 log에 이어서 작성한다.
{
  int tail;
  for (tail = log.lh.prevlogn; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

void
end_op(void)
{
  int do_commit = 0;
  int do_logging = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    if(log.lh.n + MAXOPBLOCKS > LOGSIZE) //log가 가득 찰 가능성이 있을 경우에만 commit 진행
      do_commit = 1;
    else
      do_logging = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_logging){  							//commit 없이 logging만 진행
    if(log.lh.n > log.lh.prevlogn){
      logging_write_log();
      logging_write_head();
    }
    acquire(&log.lock);
    log.lh.prevlogn = log.lh.n;
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    log.lh.prevlogn = 0; //commit 수행시 로그가 비워지므로 prevlogn도 초기화한다.
    wakeup(&log);
    release(&log.lock);
  }
}
```



### sync의 구현

sync가 호출된 시점에서 `log.committing == 1`인 경우(이미 현 buf에 대해 commit이 진행중인 경우) commit 없이 진행중인 commit이 끝날 때 까지 sleep 후 return을 호출한다. (sync가 호출된 시점에서의 buffer가 disk에 쓰여짐이 보장되기 때문)

만약 진행중인 transaction이 존재하는 경우, 무결성을 지키기 위해 transaction이 모두 종료될 때 까지 sleep후 commit을 수행한다.

```c
//log.c
int 
sync(void)
{
  acquire(&log.lock);
  if(log.committing){  //if already committing, wait for commit ends and return.
    sleep(&log, &log.lock);
    release(&log.lock);
    return 0;
  }
  while(log.outstanding){  //if transaction running, sleep for it ends.
    sleep(&log, &log.lock);
  }
  log.committing = 1;
  release(&log.lock);

  commit();
  
  acquire(&log.lock);
  log.committing = 0;
  log.lh.prevlogn = 0;
  wakeup(&log);
  release(&log.lock);
  return 0;
}
```

User 프로세스에서 `sync()`를 호출하지 않으면 프로세스가 종료되거나 `sys_close()`를 호출해도 `install_trans()`가 실행되지 않았을 수 있기 때문에, `exit()`과 `fileclose()`에서 `sync()`를 호출해 주어야 한다.

```c
//proc.c
void
exit(void)
{ 
  //...생략...

  begin_op();
  iput(curproc->cwd);
  end_op();
  sync();						//disk에 변경사항 적용
  curproc->cwd = 0;

  //...생략...
}
```

```c
//file.c
void
fileclose(struct file *f)
{
  //...생략...
    
  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
	sync();						//disk에 변경사항 적용
  }
}
```

