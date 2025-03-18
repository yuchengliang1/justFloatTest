# std::unique_lock<std::mutex> lock(mtx)

如果互斥锁 mtx 已经被其他线程锁定，那么当前线程会被阻塞，直到持有该互斥锁的线程释放它

# notFull.wait(lock, [] { return dataQueue.size() < BUFFER_SIZE; })

1. 如果 `dataQueue.size() < BUFFER_SIZE` 这个条件成立，即缓存区还有空间。wait会立即返回，线程会继续执行 wait 之后的代码
2. 如果 dataQueue.size() >= BUFFER_SIZE，即缓冲区已满。wait函数会自动释放锁让其他线程能够获取锁，同时将当前线程阻塞直至被notify_one或notify_all唤醒。

### boost::asio::io_context

boost::asio::io_context是Boost.Asio 库的核心对象，异步操作会被注册到 io_context中，当这些操作完成或者满足特定条件时，io_context 会触发相应的回调函数。

io_context.run()会启动 `io_context` 的事件循环，开始处理注册到其中的异步操作。

我们接下来将详细介绍io_context的相关机制, 先从最简单的部分开始。

```c++
    boost::asio::io_context io_context;
    
    // udp的异步接收
    udp::socket socket(io_context, udp::endpoint(udp::v4(), 12345));
	socket.async_receive_from(
	
	// tcp的异步连接
    tcp::socket socket(io_context);
    boost::asio::async_connect(socket, endpoint_iterator, handle_tcp_connect);
    
    // 定时器的异步等待
    boost::asio::steady_timer timer(io_context, boost::asio::chrono::seconds(5));
```

注意到上面，udp、tcp、定时器等异步响应可以注册到同一个io_context上，从而实现这里对多个异步操作的管理。

当上面的这种注册操作方法时，实际在底层完成了什么操作呢？我们用Linux下的epoll来解释这个问题。

*epoll 是 Linux 中用于实现高效的 I/O 多路复用的机制。当使用 epoll 来监控多个文件描述符（如套接字、管道等）时，内核会在后台不断地检查这些文件描述符的状态。一旦某个文件描述符上发生了感兴趣的事件（例如可读、可写、异常等），内核就会将该事件相关信息添加到 epoll 实例的就绪事件列表中。*

在我们上面的操作中，实际可以视为操作系统在内核中维护了一个epoll实例，同时在内核中标识出了需要监测的连接。我想这个操作是很好理解的，这里io_context内部可能就是使用了epoll来实现的I/O事件监控。

问题的关键是下面的操作，`io_context.run` 会阻塞后面的代码，该函数会启动事件循环，它会一直运行，直到所有已注册的异步操作完成，并且没有其他待处理的事件或任务。

```c++
 io_context.run();
```

这里的阻塞是如何发生的？事件循环是怎么操作的？一直运行是在运行什么？这些问题都要求我们回到系统调用这个层面去看问题，简单来说，run方法可以对应多个epoll_wait方法。

也就是说这里实际相当于线程调用了`epoll_wait` 系统调用，开始等待 `epoll` 实例上的事件发生。当没有就绪事件时，线程会将自己挂起，进入阻塞状态。这里的阻塞状态实际上线程中用户态的代码不会在继续执行了。

当某个异步的就绪事件到来时，线程才能跳出阻塞状态。

我们以UDP数据报为例说明这一流程。

1. 当 UDP 数据报到达时，网络接口卡（NIC）会检测到数据，并通过硬件中断通知操作系统。这个中断会使处理器暂停当前正在执行的用户程序，转而执行内核中的中断处理程序。
2. 中断处理程序会对 UDP 数据报进行初步处理，如将数据从网络接口卡的缓冲区复制到内核缓冲区，然后检查数据的目的端口等信息，并将对应的文件描述符添加到 epoll 实例的就绪队列中。此时，`epoll_wait` 函数会返回，返回值表示就绪事件的数量。
3. 内核会唤醒等待队列中的线程。被唤醒的线程会竞争互斥锁，获取到锁的线程才能从就绪队列中获取事件。需要注意的是：对于同一个 `epoll` 实例，无论有多少个线程在调用 `epoll_wait` 等待事件，内核只会将事件通知一次。这是因为 `epoll` 采用了共享的就绪队列来管理就绪事件。操作系统的调度算法决定了哪个线程能够优先获得 CPU 资源来执行。



