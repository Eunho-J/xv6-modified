# pread & pwrite

- `int pwrite(int fd, void* addr, int n, int off);`
- `int pread(int fd, void* addr, int n, int off);`

### pread와 pwrite의 구현

기존 xv6에 구현되어 있는 `filewrite()`와 `fileread()`를 조금씩만 수정하면 쉽게 구현할 수 있다.

```c
// Read from file f.
int
pfileread(struct file *f, char *addr, int n, int off)
{
  //...생략...
  if(f->type == FD_INODE){
    ilock(f->ip);
    r = readi(f->ip, addr, off, n); //fileread와 다르게 offset을 업데이트하지 않는다
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

int
pfilewrite(struct file *f, char *addr, int n, int off)
{
  //...생략...
      begin_op();
      ilock(f->ip);
      r = writei(f->ip, addr + i, off, n1); //filewrite와 다르게 offset을 업데이트하지 않는다
      iunlock(f->ip);
      end_op();
  //...생략...
}
```

