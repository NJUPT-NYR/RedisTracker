# RedisTracker
A redis module built to serve as a bittorrent tracker.

## 设计思路
```
announce info_hash passkey ip port numwant event
```
每个info_hash会对应到大概这样的结构体
```c
struct Peer {
    char passkey[32];
    char addr4[6];
    char addr6[18];
}

struct tmp {
    DB db[2]; // redis hashtable where stored Peer*, 2 to rehash
    timestamp when_to_die; // key to decide expire 
}

struct peer_set {
    tmp t[2]; 
}
```
假定超时时间30min
对于announce操作，找到了对应peer_set后，会将数据插入到t[1]中去
同时检查t[0]是否达到了回收要求(when_to_die>30)
如果达到，那么t[1]置为t[0]，同时创建新的tmp
根据以上信息，每个做种的超时时间在30min-60min不等。  
同时对key设置30min的超时，可以保证不会有死种存在。  
最后会返回numwant个种子，为了节省parse，直接返回compact格式，后面可以视情况加上compact选项  
关于节省内存，考虑到最后要返回compact信息，每个tmp维护一大块内存，每个Peer的addr4和addr6直接存偏移。这时候回包直接发一整块连续地址，但是需要考虑stopped产生的地址不连续性