### Prerequesites

> To run this project, below situations are required:
>
> - Server: Ubuntu 18.04
> - gcc installed

### How to run

1. Clone this repository
2. make diskfile with linux dd command

```
$ dd if=/dev/zero of=diskfile bs=1024 count=102400
```

2. Compile the source code

```
$ gcc -pthread -o fifo buffer.c -lm
```

3. Run the executable file

```
$ ./fifo
```
